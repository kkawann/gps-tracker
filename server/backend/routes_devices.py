from datetime import datetime, date, time
from typing import Optional

from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy import select, func, and_
from sqlalchemy.ext.asyncio import AsyncSession

from database import get_db
from models import User, Device, GpsLocation
from schemas import DeviceCreate, DeviceResponse, DeviceListResponse, CodeOnly
from auth import get_current_user
from mqtt_handler import mqtt_handler
from pydantic import BaseModel, Field
from typing import List

router = APIRouter(prefix="/api/devices", tags=["Devices"])


# ─── Device Config (Android Settings) ────────────────────
# SettingsViewModel: GET/PUT /api/devices/{device_uid}/config
class ConfigUpdate(BaseModel):
    customName: Optional[str] = None
    speedLimit: Optional[int] = Field(default=None, ge=0, le=500)
    authorizedPhones: Optional[List[str]] = None


@router.get("/{device_uid}/config")
async def get_device_config(
    device_uid: str,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    device = await _get_device_or_404(device_uid, user, db)
    is_owner = str(device.owner_id) == str(user.id) if device.owner_id else True
    return {
        "customName": device.name or "",
        "ownerPhone": user.phone or "",
        "authorizedPhones": device.authorized_phones or [],
        "speedLimit": device.speed_limit or 0,
        "isOwner": is_owner,
        "maxPhones": 10,
    }


@router.put("/{device_uid}/config")
async def update_device_config(
    device_uid: str,
    data: ConfigUpdate,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    from sqlalchemy import update as sa_update

    device = await _get_device_or_404(device_uid, user, db)
    updates: dict = {}
    if data.customName is not None and data.customName.strip():
        updates["name"] = data.customName.strip()[:100]
    if data.speedLimit is not None:
        updates["speed_limit"] = data.speedLimit
    if data.authorizedPhones is not None:
        phones = [p.strip() for p in data.authorizedPhones if p and p.strip()][:10]
        updates["authorized_phones"] = phones
    if updates:
        await db.execute(
            sa_update(Device).where(Device.id == device.id).values(**updates)
        )
        await db.commit()
        await db.refresh(device)
    return {"success": True}


async def _get_device_or_404(device_uid: str, user: User, db: AsyncSession) -> Device:
    result = await db.execute(
        select(Device).where(Device.device_uid == device_uid)
    )
    device = result.scalar_one_or_none()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    return device


# ─── Device List ─────────────────────────────────────────
@router.get("/", response_model=DeviceListResponse)
async def list_devices(
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(
        select(Device).where(Device.owner_id == user.id).order_by(Device.created_at.desc())
    )
    devices = result.scalars().all()
    return DeviceListResponse(devices=devices, total=len(devices))


# ─── Device Register ─────────────────────────────────────
@router.post("/", response_model=DeviceResponse)
async def register_device(
    data: DeviceCreate,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    existing = await db.execute(select(Device).where(Device.device_uid == data.device_uid))
    claimed = existing.scalar_one_or_none()
    if claimed:
        # Placeholder row auto-created by request-otp (owner=None) → claim it
        if claimed.owner_id is None:
            claimed.owner_id = user.id
            if data.name:
                claimed.name = data.name
            if data.model:
                claimed.model = data.model
            await db.commit()
            await db.refresh(claimed)
            return claimed
        raise HTTPException(status_code=409, detail="Device already registered")

    device = Device(
        device_uid=data.device_uid,
        name=data.name,
        model=data.model,
        owner_id=user.id,
        mqtt_topic=f"gps/{data.device_uid}",
    )
    db.add(device)
    await db.commit()
    await db.refresh(device)
    return device


# ─── Latest Location (by device_uid string) ──────────────
@router.get("/{device_uid}/location/latest")
async def get_latest_location(
    device_uid: str,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    device = await _get_device_or_404(device_uid, user, db)

    loc_result = await db.execute(
        select(GpsLocation)
        .where(GpsLocation.device_id == device.id)
        .order_by(GpsLocation.timestamp_device.desc())
        .limit(1)
    )
    location = loc_result.scalar_one_or_none()
    if not location:
        return {"error": "No location data"}

    return {
        "latitude": location.latitude,
        "longitude": location.longitude,
        "speed": location.speed,
        "battery": location.battery_level,
        "accuracy": location.accuracy,
        "engine": device.engine or location.engine or False,
        "satellites": location.satellites,
        "device_name": device.name,
        "timestamp": location.timestamp_device.isoformat() if location.timestamp_device else None,
    }


# ─── Location History ────────────────────────────────────
@router.get("/{device_uid}/locations")
async def get_location_history(
    device_uid: str,
    limit: int = Query(default=5000, le=50000),
    offset: int = 0,
    from_date: Optional[str] = Query(default=None, description="Start date (YYYY-MM-DD)"),
    to_date: Optional[str] = Query(default=None, description="End date (YYYY-MM-DD), inclusive"),
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    device = await _get_device_or_404(device_uid, user, db)

    query = select(GpsLocation).where(GpsLocation.device_id == device.id)

    # Date filtering — server-side for efficiency
    if from_date:
        try:
            start = datetime.combine(date.fromisoformat(from_date), time.min)
            query = query.where(GpsLocation.timestamp_device >= start)
        except ValueError:
            raise HTTPException(status_code=400, detail="Invalid from_date format, use YYYY-MM-DD")
    if to_date:
        try:
            end = datetime.combine(date.fromisoformat(to_date), time.max)
            query = query.where(GpsLocation.timestamp_device <= end)
        except ValueError:
            raise HTTPException(status_code=400, detail="Invalid to_date format, use YYYY-MM-DD")

    query = query.order_by(GpsLocation.timestamp_device.desc()).offset(offset).limit(limit)

    loc_result = await db.execute(query)
    locations = loc_result.scalars().all()
    return {
        "device_id": device_uid,
        "locations": [
            {
                "lat": loc.latitude,
                "lon": loc.longitude,
                "speed": loc.speed,
                "satellites": loc.satellites or 0,
                "accuracy": loc.accuracy,
                "engine": loc.engine or False,
                "bearing": loc.bearing,
                "battery": loc.battery_level,
                "timestamp": loc.timestamp_device.isoformat() if loc.timestamp_device else None,
            }
            for loc in locations
        ],
        "total": len(locations),
    }


# ─── Relay Control ───────────────────────────────────────
@router.post("/{device_uid}/relay/start")
async def relay_start(
    device_uid: str,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    device = await _get_device_or_404(device_uid, user, db)
    if not mqtt_handler._connected:
        raise HTTPException(status_code=503, detail="MQTT disconnected")
    mqtt_handler.publish_relay_command(device.device_uid, "start")
    return {"success": True, "message": "Engine start command sent"}


@router.post("/{device_uid}/relay/kill/request-otp")
async def relay_kill_request_otp(
    device_uid: str,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    device = await _get_device_or_404(device_uid, user, db)
    if not mqtt_handler._connected:
        raise HTTPException(status_code=503, detail="MQTT disconnected")
    mqtt_handler.publish_auth_req(device.device_uid, user.phone or "")
    return {"success": True, "message": "OTP request sent for relay kill"}


@router.post("/{device_uid}/relay/kill/verify")
async def relay_kill_verify(
    device_uid: str,
    data: CodeOnly,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    device = await _get_device_or_404(device_uid, user, db)
    if not mqtt_handler._connected:
        raise HTTPException(status_code=503, detail="MQTT disconnected")
    if not user.phone:
        raise HTTPException(status_code=400, detail="No phone number on your account")

    future = mqtt_handler.publish_auth_verify(device.device_uid, user.phone, data.code)
    try:
        verified = await future
    except Exception:
        verified = False

    if not verified:
        raise HTTPException(status_code=401, detail="Invalid OTP code")

    mqtt_handler.publish_relay_command(device.device_uid, "kill")
    return {"success": True, "message": "Engine kill command sent"}
