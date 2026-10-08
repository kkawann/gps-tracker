"""
GPS-shape Geofence Routes — exact contract of Android GeofenceViewModel:
  GET    /api/gps/geofences?device_uid={uid}  → {"ok": true, "geofences": [...]}
  POST   /api/gps/geofences                   → {"ok": true, "geofence": {...}} (or legacy {id})
  DELETE /api/gps/geofences/{gf_id}           → {"ok": true}

DB row fields (latitude/longitude/...) — Android Room naming.
Pushes FULL config (alertPhone/globalSpeedLimit + per-zone methods) to ESP32.
"""
from uuid import UUID

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field
from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import AsyncSession
from typing import Optional

from database import get_db
from models import User, Device, Geofence, Alert
from auth import get_current_user
from mqtt_handler import mqtt_handler

router = APIRouter(prefix="/api/gps/geofences", tags=["Geofence (GPS App)"])


# ─── Schemas ─────────────────────────────────────────────

class GpsGeofenceCreate(BaseModel):
    name: str
    latitude: float = Field(..., ge=-90, le=90)
    longitude: float = Field(..., ge=-180, le=180)
    radius_meters: float = Field(..., gt=0, le=50000)
    device_uid: str
    alertOnExit: bool = True
    alertOnEnter: bool = True
    exit_method: str = "call"
    enter_method: str = "sms"
    speed_limit: int = 0
    is_active: bool = True


# ─── Helpers ─────────────────────────────────────────────

def _gf_to_gps(gf: Geofence) -> dict:
    """DB row → Android GPS shape."""
    return {
        "id": str(gf.id),
        "name": gf.name,
        "latitude": gf.latitude,
        "longitude": gf.longitude,
        "radius_meters": gf.radius_meters,
        "is_active": gf.is_active,
        "alert_on_exit": gf.alert_on_exit,
        "alert_on_enter": gf.alert_on_enter,
        "exit_method": getattr(gf, "exit_method", None) or "call",
        "enter_method": getattr(gf, "enter_method", None) or "sms",
        "speed_limit": getattr(gf, "speed_limit", None) or 0,
    }


async def _get_device(device_uid: str, user: User, db: AsyncSession) -> Device:
    result = await db.execute(select(Device).where(Device.device_uid == device_uid))
    device = result.scalar_one_or_none()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    return device


async def _push_config(device_uid: str, db: AsyncSession):
    """Push FULL active config to ESP32 (retained):

    alertPhone ← owner phone, globalSpeedLimit ← device row,
    per-zone exit/enter methods + speed limits.
    """
    result = await db.execute(
        select(Device).where(Device.device_uid == device_uid)
    )
    device = result.scalar_one_or_none()
    if not device:
        return

    gf_result = await db.execute(
        select(Geofence).where(
            Geofence.is_active == True,
            (Geofence.device_id == device.id) | (Geofence.device_id.is_(None)),
        )
    )
    geofences = gf_result.scalars().all()

    owner_phone = ""
    if device.owner_id:
        user_result = await db.execute(select(User).where(User.id == device.owner_id))
        owner = user_result.scalar_one_or_none()
        owner_phone = owner.phone if owner and owner.phone else ""

    config_list = [
        {
            "id": str(gf.id),
            "lat": gf.latitude,
            "lng": gf.longitude,
            "radius": gf.radius_meters,
            "alertOnExit": gf.alert_on_exit,
            "alertOnEnter": gf.alert_on_enter,
            "exitMethod": getattr(gf, "exit_method", None) or "call",
            "enterMethod": getattr(gf, "enter_method", None) or "sms",
            "speedLimit": getattr(gf, "speed_limit", None) or 0,
            "enabled": True,
        }
        for gf in geofences
    ]
    mqtt_handler.publish_geofence_config(
        device_uid, config_list, owner_phone, device.speed_limit or 0
    )


# ─── Routes ──────────────────────────────────────────────

@router.get("")
async def list_geofences_gps(
    device_uid: str,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    device = await _get_device(device_uid, user, db)
    result = await db.execute(
        select(Geofence).where(
            Geofence.user_id == user.id,
            (Geofence.device_id == device.id) | (Geofence.device_id.is_(None)),
        ).order_by(Geofence.created_at.desc())
    )
    geofences = result.scalars().all()
    return {"ok": True, "geofences": [_gf_to_gps(gf) for gf in geofences]}


@router.post("")
async def create_geofence_gps(
    data: GpsGeofenceCreate,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    device = await _get_device(data.device_uid, user, db)

    gf = Geofence(
        user_id=user.id,
        device_id=device.id,
        name=data.name,
        latitude=data.latitude,
        longitude=data.longitude,
        radius_meters=data.radius_meters,
        alert_on_enter=data.alertOnEnter,
        alert_on_exit=data.alertOnExit,
        is_active=data.is_active,
    )
    if hasattr(Geofence, "exit_method"):
        gf.exit_method = data.exit_method
    if hasattr(Geofence, "enter_method"):
        gf.enter_method = data.enter_method
    if hasattr(Geofence, "speed_limit"):
        gf.speed_limit = data.speed_limit
    db.add(gf)
    await db.commit()
    await db.refresh(gf)

    await _push_config(data.device_uid, db)

    shaped = _gf_to_gps(gf)
    return {"ok": True, "success": True, "id": str(gf.id), "geofence": shaped}


@router.delete("/{gf_id}")
async def delete_geofence_gps(
    gf_id: UUID,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(
        select(Geofence).where(Geofence.id == gf_id, Geofence.user_id == user.id)
    )
    gf = result.scalar_one_or_none()
    if not gf:
        raise HTTPException(status_code=404, detail="Geofence not found")

    device_uid: Optional[str] = None
    if gf.device_id:
        dev_result = await db.execute(select(Device).where(Device.id == gf.device_id))
        device = dev_result.scalar_one_or_none()
        device_uid = device.device_uid if device else None

    # Keep alert history, but unlink it — otherwise the FK on
    # alerts.geofence_id blocks this DELETE (500) for any fence that
    # has ever fired an alert.
    await db.execute(
        update(Alert).where(Alert.geofence_id == gf.id).values(geofence_id=None)
    )

    await db.delete(gf)
    await db.commit()

    if device_uid:
        await _push_config(device_uid, db)

    return {"ok": True, "success": True}
