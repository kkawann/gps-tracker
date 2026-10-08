"""
Mobile App Geofence Routes — matches Android app GeofenceViewModel expected API:
  GET    /api/geofence/{device_uid}              → list geofences for device
  POST   /api/geofence/{device_uid}              → create geofence
  PUT    /api/geofence/{device_uid}/{gf_id}      → update geofence
  DELETE /api/geofence/{device_uid}/{gf_id}      → delete geofence

Response format matches app expectations:
  {id, name, center: {lat, lng}, radius, enabled, alertOnExit, alertOnEnter,
   exitMethod, enterMethod, speedLimit}
"""
from uuid import UUID

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import AsyncSession
from typing import Optional

from database import get_db
from models import User, Device, Geofence, Alert
from auth import get_current_user
from mqtt_handler import mqtt_handler

router = APIRouter(prefix="/api/geofence", tags=["Geofence (Mobile App)"])


# ─── Schemas matching app format ───────────────────────

class MobileGeofenceCreate(BaseModel):
    name: str
    center: dict  # {lat, lng}
    radius: int = 100
    alertOnExit: bool = True
    alertOnEnter: bool = True
    exitMethod: str = "call"
    enterMethod: str = "sms"
    speedLimit: int = 0


class MobileGeofenceUpdate(BaseModel):
    name: Optional[str] = None
    enabled: Optional[bool] = None
    alertOnExit: Optional[bool] = None
    alertOnEnter: Optional[bool] = None
    exitMethod: Optional[str] = None
    enterMethod: Optional[str] = None
    speedLimit: Optional[int] = None


# ─── Helpers ───────────────────────────────────────────

def _gf_to_mobile(gf: Geofence) -> dict:
    """Convert DB Geofence to mobile app format."""
    return {
        "id": str(gf.id),
        "name": gf.name,
        "center": {"lat": gf.latitude, "lng": gf.longitude},
        "radius": gf.radius_meters,
        "enabled": gf.is_active,
        "alertOnExit": gf.alert_on_exit,
        "alertOnEnter": gf.alert_on_enter,
        "exitMethod": getattr(gf, "exit_method", None) or "call",
        "enterMethod": getattr(gf, "enter_method", None) or "sms",
        "speedLimit": getattr(gf, "speed_limit", None) or 0,
    }


async def _find_user_device(device_uid: str, user: User, db: AsyncSession) -> Device:
    """Find device owned by this user."""
    result = await db.execute(
        select(Device).where(Device.device_uid == device_uid, Device.owner_id == user.id)
    )
    device = result.scalar_one_or_none()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found or not yours")
    return device


async def _find_user_geofence(gf_id: UUID, user: User, db: AsyncSession) -> Geofence:
    """Find geofence owned by this user."""
    result = await db.execute(
        select(Geofence).where(Geofence.id == gf_id, Geofence.user_id == user.id)
    )
    gf = result.scalar_one_or_none()
    if not gf:
        raise HTTPException(status_code=404, detail="Geofence not found")
    return gf


async def _push_config(device_uid: str, db: AsyncSession):
    """Push all active geofences config to device via MQTT."""
    result = await db.execute(
        select(Geofence).where(Geofence.is_active == True)
    )
    geofences = result.scalars().all()
    config_list = [
        {
            "id": str(gf.id),
            "lat": gf.latitude,
            "lng": gf.longitude,
            "radius": gf.radius_meters,
            "alertOnExit": gf.alert_on_exit,
            "alertOnEnter": gf.alert_on_enter,
            "enabled": True,
        }
        for gf in geofences
    ]
    mqtt_handler.publish_geofence_config(device_uid, config_list)


# ─── Routes ────────────────────────────────────────────

@router.get("/{device_uid}")
async def list_geofences_mobile(
    device_uid: str,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    """List geofences — returns array directly (app expects JSONArray)."""
    device = await _find_user_device(device_uid, user, db)

    # Get geofences for this user (optionally filtered by device)
    result = await db.execute(
        select(Geofence).where(
            Geofence.user_id == user.id,
            (Geofence.device_id == device.id) | (Geofence.device_id.is_(None))
        ).order_by(Geofence.created_at.desc())
    )
    geofences = result.scalars().all()
    return [_gf_to_mobile(gf) for gf in geofences]


@router.post("/{device_uid}")
async def create_geofence_mobile(
    device_uid: str,
    data: MobileGeofenceCreate,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    """Create a geofence — returns {success: true}."""
    device = await _find_user_device(device_uid, user, db)

    center = data.center
    lat = center.get("lat", 0)
    lng = center.get("lng", 0)

    gf = Geofence(
        user_id=user.id,
        device_id=device.id,
        name=data.name,
        latitude=lat,
        longitude=lng,
        radius_meters=float(data.radius),
        alert_on_enter=data.alertOnEnter,
        alert_on_exit=data.alertOnExit,
        is_active=True,
    )
    db.add(gf)
    await db.commit()
    await db.refresh(gf)

    # Push updated config to device
    await _push_config(device_uid, db)

    return {"success": True, "id": str(gf.id)}


@router.put("/{device_uid}/{gf_id}")
async def update_geofence_mobile(
    device_uid: str,
    gf_id: UUID,
    data: MobileGeofenceUpdate,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    """Update geofence — returns {success: true}."""
    device = await _find_user_device(device_uid, user, db)
    gf = await _find_user_geofence(gf_id, user, db)

    if data.name is not None:
        gf.name = data.name
    if data.enabled is not None:
        gf.is_active = data.enabled
    if data.alertOnExit is not None:
        gf.alert_on_exit = data.alertOnExit
    if data.alertOnEnter is not None:
        gf.alert_on_enter = data.alertOnEnter

    # Store extra fields if model has them
    if hasattr(gf, "exit_method") and data.exitMethod is not None:
        gf.exit_method = data.exitMethod
    if hasattr(gf, "enter_method") and data.enterMethod is not None:
        gf.enter_method = data.enterMethod
    if hasattr(gf, "speed_limit") and data.speedLimit is not None:
        gf.speed_limit = data.speedLimit

    await db.commit()
    await db.refresh(gf)

    # Push updated config to device
    await _push_config(device_uid, db)

    return {"success": True}


@router.delete("/{device_uid}/{gf_id}")
async def delete_geofence_mobile(
    device_uid: str,
    gf_id: UUID,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    """Delete geofence."""
    device = await _find_user_device(device_uid, user, db)
    gf = await _find_user_geofence(gf_id, user, db)

    # Unlink alerts first — otherwise the FK on alerts.geofence_id
    # blocks this DELETE (500) for any fence that has fired an alert.
    await db.execute(
        update(Alert).where(Alert.geofence_id == gf.id).values(geofence_id=None)
    )

    await db.delete(gf)
    await db.commit()

    # Push updated config to device
    await _push_config(device_uid, db)

    return {"success": True}
