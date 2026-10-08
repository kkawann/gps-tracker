from uuid import UUID

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from database import get_db
from models import User, Device, Geofence
from schemas import GeofenceCreate, GeofenceResponse
from auth import get_current_user
from mqtt_handler import mqtt_handler

router = APIRouter(prefix="/api/geofences", tags=["Geofences"])


async def _push_geofence_config_to_device(device_uid: str, db: AsyncSession):
    result = await db.execute(select(Geofence).where(Geofence.is_active == True))
    geofences = result.scalars().all()
    config_list = [
        {
            "id": str(gf.id),
            "latitude": gf.latitude,
            "longitude": gf.longitude,
            "radius_meters": gf.radius_meters,
            "alert_on_exit": gf.alert_on_exit,
            "alert_on_enter": gf.alert_on_enter,
            "enabled": True,
        }
        for gf in geofences
    ]
    mqtt_handler.publish_geofence_config(device_uid, config_list)


async def _get_device_uid(device_id: UUID, db: AsyncSession) -> str:
    result = await db.execute(select(Device).where(Device.id == device_id))
    device = result.scalar_one_or_none()
    return device.device_uid if device else None


@router.get("/", response_model=list[GeofenceResponse])
async def list_geofences(
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(
        select(Geofence).where(Geofence.user_id == user.id).order_by(Geofence.created_at.desc())
    )
    return result.scalars().all()


@router.post("/", response_model=GeofenceResponse)
async def create_geofence(
    data: GeofenceCreate,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    geofence = Geofence(
        user_id=user.id,
        device_id=data.device_id,
        name=data.name,
        description=data.description,
        latitude=data.latitude,
        longitude=data.longitude,
        radius_meters=data.radius_meters,
        alert_on_enter=data.alert_on_enter,
        alert_on_exit=data.alert_on_exit,
    )
    db.add(geofence)
    await db.commit()
    await db.refresh(geofence)

    if data.device_id:
        device_uid = await _get_device_uid(data.device_id, db)
        if device_uid:
            await _push_geofence_config_to_device(device_uid, db)

    return geofence


@router.put("/{geofence_id}", response_model=GeofenceResponse)
async def update_geofence(
    geofence_id: UUID,
    data: GeofenceCreate,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(
        select(Geofence).where(Geofence.id == geofence_id, Geofence.user_id == user.id)
    )
    gf = result.scalar_one_or_none()
    if not gf:
        raise HTTPException(status_code=404, detail="Geofence not found")

    gf.name = data.name
    gf.description = data.description
    gf.latitude = data.latitude
    gf.longitude = data.longitude
    gf.radius_meters = data.radius_meters
    gf.alert_on_enter = data.alert_on_enter
    gf.alert_on_exit = data.alert_on_exit
    gf.device_id = data.device_id or gf.device_id
    await db.commit()
    await db.refresh(gf)

    if gf.device_id:
        device_uid = await _get_device_uid(gf.device_id, db)
        if device_uid:
            await _push_geofence_config_to_device(device_uid, db)

    return gf


@router.delete("/{geofence_id}")
async def delete_geofence(
    geofence_id: UUID,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(
        select(Geofence).where(Geofence.id == geofence_id, Geofence.user_id == user.id)
    )
    gf = result.scalar_one_or_none()
    if not gf:
        raise HTTPException(status_code=404, detail="Geofence not found")

    device_id = gf.device_id
    await db.delete(gf)
    await db.commit()

    if device_id:
        device_uid = await _get_device_uid(device_id, db)
        if device_uid:
            await _push_geofence_config_to_device(device_uid, db)

    return {"success": True}
