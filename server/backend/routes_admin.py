from uuid import UUID
from datetime import datetime, timezone

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select, func, update
from sqlalchemy.ext.asyncio import AsyncSession

from database import get_db
from models import User, Device, GpsLocation, Trip, Alert
from schemas import UserCreate, UserLogin, TokenResponse
from auth import hash_password, verify_password, create_access_token, require_admin

router = APIRouter(prefix="/api/admin", tags=["Admin"])


# ─── Dashboard Stats ────────────────────────────────────
@router.get("/stats")
async def admin_stats(
    user: User = Depends(require_admin),
    db: AsyncSession = Depends(get_db),
):
    """آمار کلی سیستم"""
    users_count = (await db.execute(select(func.count(User.id)))).scalar() or 0
    devices_count = (await db.execute(select(func.count(Device.id)))).scalar() or 0
    online_devices = (await db.execute(
        select(func.count(Device.id)).where(Device.is_online == True)
    )).scalar() or 0
    locations_count = (await db.execute(select(func.count(GpsLocation.id)))).scalar() or 0
    trips_count = (await db.execute(select(func.count(Trip.id)))).scalar() or 0
    alerts_count = (await db.execute(
        select(func.count(Alert.id)).where(Alert.is_read == False)
    )).scalar() or 0

    return {
        "users": users_count,
        "devices": devices_count,
        "online_devices": online_devices,
        "total_locations": locations_count,
        "trips": trips_count,
        "unread_alerts": alerts_count,
    }


# ─── Users CRUD ─────────────────────────────────────────
@router.get("/users")
async def list_users(
    user: User = Depends(require_admin),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(select(User).order_by(User.created_at.desc()))
    users = result.scalars().all()
    return [
        {
            "id": str(u.id),
            "username": u.username,
            "email": u.email,
            "full_name": u.full_name,
            "role": u.role,
            "is_active": u.is_active,
            "created_at": u.created_at.isoformat() if u.created_at else None,
        }
        for u in users
    ]


@router.post("/users")
async def create_user(
    data: UserCreate,
    user: User = Depends(require_admin),
    db: AsyncSession = Depends(get_db),
):
    existing = await db.execute(
        select(User).where((User.username == data.username) | (User.email == data.email))
    )
    if existing.scalar_one_or_none():
        raise HTTPException(status_code=409, detail="Username or email already exists")

    new_user = User(
        username=data.username,
        email=data.email,
        password_hash=hash_password(data.password),
        full_name=data.full_name,
        phone=data.phone,
        role="user",
    )
    db.add(new_user)
    await db.commit()
    await db.refresh(new_user)
    return {"id": str(new_user.id), "username": new_user.username, "message": "User created"}


@router.put("/users/{user_id}/toggle")
async def toggle_user(
    user_id: UUID,
    user: User = Depends(require_admin),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(select(User).where(User.id == user_id))
    target = result.scalar_one_or_none()
    if not target:
        raise HTTPException(status_code=404, detail="User not found")

    target.is_active = not target.is_active
    await db.commit()
    return {"id": str(target.id), "is_active": target.is_active}


@router.delete("/users/{user_id}")
async def delete_user(
    user_id: UUID,
    user: User = Depends(require_admin),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(select(User).where(User.id == user_id))
    target = result.scalar_one_or_none()
    if not target:
        raise HTTPException(status_code=404, detail="User not found")

    await db.delete(target)
    await db.commit()
    return {"message": "User deleted"}


# ─── Devices CRUD ───────────────────────────────────────
@router.get("/devices")
async def list_all_devices(
    user: User = Depends(require_admin),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(
        select(Device).order_by(Device.created_at.desc())
    )
    devices = result.scalars().all()
    return [
        {
            "id": str(d.id),
            "device_uid": d.device_uid,
            "name": d.name,
            "model": d.model,
            "is_online": d.is_online,
            "last_seen": d.last_seen.isoformat() if d.last_seen else None,
            "battery_level": d.battery_level,
            "mqtt_topic": d.mqtt_topic,
            "owner_id": str(d.owner_id) if d.owner_id else None,
            "created_at": d.created_at.isoformat() if d.created_at else None,
        }
        for d in devices
    ]


@router.delete("/devices/{device_id}")
async def delete_device(
    device_id: UUID,
    user: User = Depends(require_admin),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(select(Device).where(Device.id == device_id))
    device = result.scalar_one_or_none()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")

    await db.delete(device)
    await db.commit()
    return {"message": "Device deleted"}
