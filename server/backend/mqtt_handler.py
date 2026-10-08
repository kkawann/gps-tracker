"""
MQTT Handler — Full topic bridge between ESP32 devices and the backend.
Handles: location, auth, relay, sync/backlog, geofence config push.
"""

import json
import asyncio
import logging
from datetime import datetime
from typing import Optional, Dict
from uuid import UUID, uuid5, NAMESPACE_URL

import paho.mqtt.client as mqtt
from sqlalchemy import select, update, func, or_
from sqlalchemy.ext.asyncio import AsyncSession

from config import get_settings
from database import async_session
from models import Device, GpsLocation, Trip, Geofence, Alert
from gps_filter import (
    is_implausible_jump, parking_detector, filter_gps_outliers,
    haversine_distance, get_signal_bars
)

logger = logging.getLogger("gps.mqtt")
settings = get_settings()


class MQTTHandler:
    def __init__(self):
        self.client = mqtt.Client(
            client_id="gps-backend-service",
            protocol=mqtt.MQTTv311,
        )
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect
        self._connected = False
        self._loop: Optional[asyncio.AbstractEventLoop] = None

        # In-memory state
        self._last_good_position: Dict[str, Dict] = {}
        self._pending_auth: Dict[str, Dict] = {}  # car_id → {phone, expires, purpose}
        self._pending_verify: Dict[str, Dict] = {}
        self._last_seq: Dict[str, int] = {}
        self._geofence_state: Dict[str, Dict[str, bool]] = {}

    # ─── Connection ────────────────────────────────────────
    def _on_connect(self, client, userdata, flags, rc, properties=None):
        if rc == 0:
            self._connected = True
            logger.info("Connected to MQTT broker")
            client.subscribe("gps/+/location", qos=1)
            client.subscribe("gps/+/auth/code", qos=1)
            client.subscribe("gps/+/auth/result", qos=1)
            client.subscribe("gps/+/sync/hello", qos=1)
            client.subscribe("gps/+/location/backlog", qos=1)
            client.subscribe("gps/+/heartbeat", qos=1)
            client.subscribe("gps/+/status", qos=1)
            client.subscribe("gps/+/relay/status", qos=1)
            logger.info("Subscribed to all GPS topics")
        else:
            logger.error(f"MQTT connection failed with code {rc}")

    def _on_disconnect(self, client, userdata, rc, properties=None):
        self._connected = False
        logger.warning(f"Disconnected from MQTT broker (rc={rc})")

    # ─── Message Router ────────────────────────────────────
    def _on_message(self, client, userdata, msg):
        try:
            topic_parts = msg.topic.split("/")
            if len(topic_parts) < 3:
                return
            car_id = topic_parts[1]
            # Handle 4-segment topics like gps/{uid}/auth/code
            msg_type = "/".join(topic_parts[2:])
            payload = json.loads(msg.payload.decode("utf-8"))

            if msg_type == "location":
                asyncio.run_coroutine_threadsafe(self._process_location(car_id, payload), self._loop)
            elif msg_type == "auth/code":
                self._process_auth_code(car_id, payload)
            elif msg_type == "auth/result":
                self._process_auth_result(car_id, payload)
            elif msg_type == "sync/hello":
                asyncio.run_coroutine_threadsafe(self._process_sync_hello(car_id, payload), self._loop)
            elif msg_type == "location/backlog":
                asyncio.run_coroutine_threadsafe(self._process_backlog(car_id, payload), self._loop)
            elif msg_type == "heartbeat":
                asyncio.run_coroutine_threadsafe(self._process_heartbeat(car_id), self._loop)
            elif msg_type == "status":
                asyncio.run_coroutine_threadsafe(self._process_status(car_id, payload), self._loop)
            elif msg_type == "relay/status":
                asyncio.run_coroutine_threadsafe(self._process_relay_status(car_id, payload), self._loop)
        except json.JSONDecodeError:
            logger.error(f"Invalid JSON on topic {msg.topic}")
        except Exception as e:
            logger.error(f"Error processing message: {e}")

    # ─── Location Processing ──────────────────────────────
    async def _process_location(self, car_id: str, payload: dict):
        async with async_session() as session:
            try:
                device = await self._find_device(session, car_id)
                if not device:
                    logger.warning(f"Unknown device: {car_id}")
                    return

                now = datetime.utcnow()
                lat = payload.get("lat")
                lng = payload.get("lng") or payload.get("lon")  # App sends "lon", ESP32 sends "lng"
                signal = payload.get("signal", "ok")
                satellites = payload.get("satellites", 0)

                if lat and lng and signal == "ok":
                    candidate = {"lat": lat, "lng": lng, "timestamp": int(now.timestamp() * 1000), "satellites": satellites}
                    prev_good = self._last_good_position.get(car_id)

                    is_jump = prev_good and is_implausible_jump(prev_good, candidate)

                    display_lat, display_lng = lat, lng
                    filtered_outlier = False
                    is_parked = False
                    parking_radius = 0

                    if is_jump:
                        display_lat = prev_good["lat"]
                        display_lng = prev_good["lng"]
                        filtered_outlier = True
                        logger.warning(f"GPS jump filtered — car:{car_id} lat:{lat} lng:{lng}")
                    else:
                        self._last_good_position[car_id] = candidate
                        parking = parking_detector.update(car_id, {"lat": lat, "lng": lng, "satellites": satellites, "timestamp": int(now.timestamp() * 1000)})
                        if parking:
                            display_lat = parking["lat"]
                            display_lng = parking["lng"]
                            is_parked = True
                            parking_radius = parking["radius"]

                    location = GpsLocation(
                        device_id=device.id,
                        latitude=display_lat,
                        longitude=display_lng,
                        altitude=payload.get("alt"),
                        speed=payload.get("speed"),
                        bearing=payload.get("bearing"),
                        accuracy=payload.get("accuracy"),
                        battery_level=payload.get("battery"),
                        network_type=payload.get("network_type"),
                        signal=signal,
                        satellites=satellites,
                        engine=payload.get("engine", device.engine or False),
                        timestamp_device=now,
                    )
                    session.add(location)

                    await session.execute(
                        update(Device).where(Device.id == device.id).values(
                            is_online=True, last_seen=now, battery_level=payload.get("battery")
                        )
                    )

                    session_id_raw = payload.get("session_id", "")
                    if session_id_raw:
                        try:
                            try:
                                trip_uuid = UUID(session_id_raw)
                            except ValueError:
                                # ESP32 sends DEVICE_ID (not a UUID) — stable uuid5 mapping
                                trip_uuid = uuid5(NAMESPACE_URL, f"gps:{session_id_raw}")
                            # Auto-create trip if it doesn't exist yet
                            existing_trip = await session.execute(
                                select(Trip).where(Trip.id == trip_uuid)
                            )
                            if not existing_trip.scalar_one_or_none():
                                trip = Trip(
                                    id=trip_uuid,
                                    device_id=device.id,
                                    start_time=now,
                                    start_latitude=lat,
                                    start_longitude=lng,
                                    status="active",
                                )
                                session.add(trip)
                                logger.info(f"New trip created: {trip_uuid} for device {car_id}")
                            await self._update_trip_distance(session, trip_uuid, location)
                        except Exception:
                            pass

                    if not filtered_outlier:
                        await self._check_geofences(session, device, location)
                        await self._check_speed_limit(session, device, location)

                else:
                    # No valid fix — still update device online status
                    await session.execute(
                        update(Device).where(Device.device_uid == car_id).values(
                            is_online=True, last_seen=datetime.utcnow()
                        )
                    )

                await session.commit()
            except Exception as e:
                await session.rollback()
                logger.error(f"Error processing location: {e}")

    # ─── Auth Code (SMS status from ESP32) ────────────────
    def _process_auth_code(self, car_id: str, payload: dict):
        logger.info(f"📨 auth/code [{car_id}]: sms_status={payload.get('sms_status')} phone={payload.get('phone')}")

    # ─── Auth Result (OTP verification from ESP32) ────────
    def _process_auth_result(self, car_id: str, payload: dict):
        logger.info(f"📨 auth/result [{car_id}]: payload={payload}")
        resolver = self._pending_verify.get(car_id)
        if not resolver:
            logger.warning(f"auth/result [{car_id}]: no pending verify found (already timed out?)")
            return
        timer = resolver.get("timer")
        if timer:
            timer.cancel()
        status = payload.get("status")
        result = status == "approved"
        logger.info(f"✅ auth/result [{car_id}]: status={status} → result={result}")
        future = resolver.get("future")
        if future and not future.done():
            future.set_result(result)
        del self._pending_verify[car_id]

    # ─── Sync Hello (backlog handshake) ───────────────────
    async def _db_last_seq(self, car_id: str) -> int:
        """max(seq) from DB — in-memory _last_seq is lost on restart (avoids duplicate backlog re-insert)."""
        async with async_session() as session:
            try:
                device = await self._find_device(session, car_id)
                if not device:
                    return 0
                result = await session.execute(
                    select(func.max(GpsLocation.seq)).where(GpsLocation.device_id == device.id)
                )
                return result.scalar() or 0
            except Exception as e:
                logger.error(f"db_last_seq error [{car_id}]: {e}")
                return 0

    async def _process_sync_hello(self, car_id: str, payload: dict):
        last_seq = self._last_seq.get(car_id)
        if last_seq is None:
            last_seq = await self._db_last_seq(car_id)
            self._last_seq[car_id] = last_seq
        self.client.publish(
            f"gps/{car_id}/sync/ack",
            json.dumps({"lastKnownSeq": last_seq}),
            qos=1
        )
        logger.info(
            f"sync/hello from {car_id} "
            f"(oldest:{payload.get('oldestSeq')} newest:{payload.get('newestSeq')} count:{payload.get('count')}) "
            f"→ ack lastKnownSeq:{last_seq}"
        )

    # ─── Backlog Batch (historical points from ESP32) ─────
    async def _process_backlog(self, car_id: str, payload: dict):
        points = payload.get("points", [])
        if not isinstance(points, list):
            return
        last_seq = self._last_seq.get(car_id)
        if last_seq is None:
            last_seq = await self._db_last_seq(car_id)
        max_seq = last_seq
        saved_count = 0

        async with async_session() as session:
            try:
                device = await self._find_device(session, car_id)
                if not device:
                    return

                for pt in points:
                    seq = pt.get("seq", 0)
                    lng = pt.get("lng") or pt.get("lon")
                    if seq > last_seq and pt.get("lat") and lng:
                        ts = pt.get("ts", 0)
                        date_obj = datetime.utcfromtimestamp(ts) if ts else datetime.utcnow()
                        location = GpsLocation(
                            device_id=device.id,
                            latitude=pt["lat"],
                            longitude=lng,
                            speed=pt.get("speed"),
                            signal="ok",
                            seq=seq,
                            timestamp_device=date_obj,
                        )
                        session.add(location)
                        saved_count += 1
                    if seq > max_seq:
                        max_seq = seq

                if max_seq > last_seq:
                    self._last_seq[car_id] = max_seq

                await session.commit()
                logger.info(
                    f"backlog from {car_id}: {len(points)} received, "
                    f"{saved_count} new saved (lastSeq: {max_seq})"
                )
            except Exception as e:
                await session.rollback()
                logger.error(f"Error processing backlog: {e}")

    # ─── Heartbeat ────────────────────────────────────────
    async def _process_heartbeat(self, car_id: str):
        async with async_session() as session:
            try:
                await session.execute(
                    update(Device).where(Device.device_uid == car_id).values(
                        is_online=True, last_seen=datetime.utcnow()
                    )
                )
                await session.commit()
            except Exception as e:
                logger.error(f"Error processing heartbeat: {e}")

    # ─── Status ───────────────────────────────────────────
    async def _process_status(self, car_id: str, payload: dict):
        async with async_session() as session:
            try:
                await session.execute(
                    update(Device).where(Device.device_uid == car_id).values(
                        is_online=payload.get("online", True),
                        last_seen=datetime.utcnow(),
                        battery_level=payload.get("battery"),
                        model=payload.get("model"),
                        os_version=payload.get("os_version"),
                        app_version=payload.get("app_version"),
                    )
                )
                await session.commit()
            except Exception as e:
                logger.error(f"Error processing status: {e}")

    # ─── Relay Status (engine state from ESP32) ────────────
    async def _process_relay_status(self, car_id: str, payload: dict):
        if "engine" not in payload:
            return  # error payload (invalid_action) — no engine field
        async with async_session() as session:
            try:
                await session.execute(
                    update(Device).where(Device.device_uid == car_id).values(
                        engine=bool(payload.get("engine")), last_seen=datetime.utcnow()
                    )
                )
                await session.commit()
                logger.info(f"relay/status [{car_id}] engine={payload.get('engine')}")
            except Exception as e:
                await session.rollback()
                logger.error(f"Error processing relay status: {e}")

    # ─── Trip Distance Update ─────────────────────────────
    async def _update_trip_distance(self, session: AsyncSession, trip_id: UUID, new_location: GpsLocation):
        try:
            result = await session.execute(
                select(GpsLocation)
                .where(GpsLocation.session_id == trip_id, GpsLocation.id != new_location.id)
                .order_by(GpsLocation.timestamp_device.desc())
                .limit(1)
            )
            last_location = result.scalar_one_or_none()
            if last_location:
                distance = haversine_distance(
                    last_location.latitude, last_location.longitude,
                    new_location.latitude, new_location.longitude
                )
                if distance > 5.0:
                    await session.execute(
                        update(Trip).where(Trip.id == trip_id).values(
                            distance_meters=Trip.distance_meters + distance
                        )
                    )
            else:
                await session.execute(
                    update(Trip).where(Trip.id == trip_id).values(
                        start_latitude=new_location.latitude,
                        start_longitude=new_location.longitude,
                    )
                )
        except Exception as e:
            logger.error(f"Error updating trip distance: {e}")

    # ─── Geofence Check ───────────────────────────────────
    async def _check_geofences(self, session: AsyncSession, device: Device, location: GpsLocation):
        # Only this device's owner's fences: bound to this device, or global
        # (device_id NULL). Without the owner filter, one user's fences would
        # fire alerts on every other user's device.
        result = await session.execute(
            select(Geofence).where(
                Geofence.is_active == True,
                Geofence.user_id == device.owner_id,
                or_(Geofence.device_id == device.id, Geofence.device_id.is_(None)),
            )
        )
        geofences = result.scalars().all()
        device_id_str = str(device.id)

        if device_id_str not in self._geofence_state:
            self._geofence_state[device_id_str] = {}

        for gf in geofences:
            distance = haversine_distance(gf.latitude, gf.longitude, location.latitude, location.longitude)
            is_inside = distance <= gf.radius_meters
            gf_id_str = str(gf.id)
            was_inside = self._geofence_state[device_id_str].get(gf_id_str, False)

            if is_inside and not was_inside and gf.alert_on_enter:
                await self._create_alert(session, device.id, gf.id, "geofence_enter",
                    f"'{device.name}' entered '{gf.name}'", location.latitude, location.longitude)
                self.publish_geofence_alert(device.device_uid, gf_id_str, gf.name, "enter")
            elif not is_inside and was_inside and gf.alert_on_exit:
                await self._create_alert(session, device.id, gf.id, "geofence_exit",
                    f"'{device.name}' exited '{gf.name}'", location.latitude, location.longitude)
                self.publish_geofence_alert(device.device_uid, gf_id_str, gf.name, "exit")

            self._geofence_state[device_id_str][gf_id_str] = is_inside

    async def _check_speed_limit(self, session: AsyncSession, device: Device, location: GpsLocation):
        if location.speed and location.speed > settings.SPEED_LIMIT_KMH:
            await self._create_alert(session, device.id, None, "overspeed",
                f"'{device.name}' speed {location.speed:.1f} km/h exceeds limit",
                location.latitude, location.longitude)

    async def _create_alert(self, session: AsyncSession, device_id: UUID,
                            geofence_id: Optional[UUID], alert_type: str,
                            message: str, latitude: float, longitude: float):
        alert = Alert(device_id=device_id, geofence_id=geofence_id,
                      alert_type=alert_type, message=message,
                      latitude=latitude, longitude=longitude)
        session.add(alert)
        logger.info(f"Alert: {alert_type} — {message}")

    # ─── Public Methods (called by routes) ────────────────
    async def _find_device(self, session: AsyncSession, car_id: str) -> Optional[Device]:
        result = await session.execute(select(Device).where(Device.device_uid == car_id))
        return result.scalar_one_or_none()

    def publish_auth_req(self, car_id: str, phone: str):
        if not self._connected:
            logger.error(f"auth/req FAILED [{car_id}]: MQTT not connected")
            return False
        try:
            self._pending_auth[car_id] = {"phone": phone}
            self.client.publish(f"gps/{car_id}/auth/req", json.dumps({"phone": phone}), qos=1)
            logger.info(f"📤 auth/req → {car_id} (phone:{phone})")
            return True
        except Exception as e:
            logger.error(f"auth/req FAILED [{car_id}]: {e}")
            return False

    def get_pending_auth_phone(self, car_id: str) -> str:
        auth = self._pending_auth.get(car_id)
        if auth:
            return auth.get("phone", "")
        return ""

    def publish_auth_verify(self, car_id: str, phone: str, code: str, timeout_sec: int = 30) -> asyncio.Future:
        loop = self._loop
        future = loop.create_future()

        # Cancel previous pending verify
        old = self._pending_verify.get(car_id)
        if old:
            old_timer = old.get("timer")
            if old_timer:
                old_timer.cancel()
            old_future = old.get("future")
            if old_future and not old_future.done():
                old_future.set_result(False)

        def on_timeout():
            if not future.done():
                logger.warning(f"⏱️ auth/verify timeout [{car_id}] after {timeout_sec}s — ESP32 did not respond")
                future.set_result(False)
                self._pending_verify.pop(car_id, None)

        timer = loop.call_later(timeout_sec, on_timeout)
        self._pending_verify[car_id] = {"future": future, "timer": timer}

        self.client.publish(f"gps/{car_id}/auth/verify", json.dumps({"phone": phone, "code": code}), qos=1)
        logger.info(f"📤 auth/verify → {car_id} (phone:{phone} code:{code}) timeout:{timeout_sec}s")
        return future

    def publish_relay_command(self, car_id: str, action: str) -> bool:
        if not self._connected:
            return False
        try:
            # firmware isValidRelayAction() accepts only start/kill/toggle
            self.client.publish(f"gps/{car_id}/relay/command",
                                json.dumps({"action": action, "timestamp": int(datetime.utcnow().timestamp() * 1000)}), qos=1)
            logger.info(f"relay/command → {car_id} action:{action}")
            return True
        except Exception as e:
            logger.error(f"relay/command FAILED [{car_id}]: {e}")
            return False

    def publish_geofence_config(self, car_id: str, geofences: list, alert_phone: str = "", global_speed_limit: int = 0):
        if not self._connected:
            return
        payload = {
            "geofences": [
                {
                    "id": gf.get("id"),
                    "lat": gf.get("lat") or gf.get("latitude"),
                    "lng": gf.get("lng") or gf.get("longitude"),
                    "radius": gf.get("radius") or gf.get("radius_meters"),
                    "alertOnExit": gf.get("alertOnExit") or gf.get("alert_on_exit", False),
                    "alertOnEnter": gf.get("alertOnEnter") or gf.get("alert_on_enter", False),
                    "exitMethod": gf.get("exitMethod") or gf.get("exit_method") or "call",
                    "enterMethod": gf.get("enterMethod") or gf.get("enter_method") or "sms",
                    "speedLimit": gf.get("speedLimit") or gf.get("speed_limit") or 0,
                }
                for gf in geofences if gf.get("enabled", True)
            ],
            "alertPhone": alert_phone,
            "globalSpeedLimit": global_speed_limit,
        }
        self.client.publish(f"gps/{car_id}/geofence/config", json.dumps(payload), qos=1, retain=True)
        logger.info(f"geofence/config → {car_id} ({len(payload['geofences'])} zones)")

    def publish_geofence_alert(self, car_id: str, geofence_id: str, geofence_name: str, action: str):
        if not self._connected:
            return
        self.client.publish(f"gps/{car_id}/geofence/alert", json.dumps({
            "action": action,
            "geofence": {"id": geofence_id, "name": geofence_name},
            "timestamp": int(datetime.utcnow().timestamp() * 1000)
        }), qos=1)

    # ─── Lifecycle ────────────────────────────────────────
    def start(self, loop: asyncio.AbstractEventLoop):
        self._loop = loop
        self.client.username_pw_set(settings.MQTT_USERNAME, settings.MQTT_PASSWORD)
        try:
            self.client.connect(settings.MQTT_BROKER, settings.MQTT_PORT, keepalive=60)
            self.client.loop_start()
            logger.info("MQTT client started")
        except Exception as e:
            logger.error(f"Failed to start MQTT client: {e}")

    def stop(self):
        self.client.loop_stop()
        self.client.disconnect()
        logger.info("MQTT client stopped")


mqtt_handler = MQTTHandler()
