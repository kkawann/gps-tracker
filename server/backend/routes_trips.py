from uuid import UUID
from datetime import datetime

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select, func
from sqlalchemy.ext.asyncio import AsyncSession

from database import get_db
from models import User, Device, Trip
from schemas import TripResponse, TripListResponse
from auth import get_current_user

router = APIRouter(prefix="/api/trips", tags=["Trips"])


@router.get("/", response_model=TripListResponse)
async def list_trips(
    device_id: UUID = None,
    limit: int = 50,
    offset: int = 0,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    """List trips, optionally filtered by device"""
    query = select(Trip).join(Device).where(Device.owner_id == user.id)

    if device_id:
        query = query.where(Trip.device_id == device_id)

    count_result = await db.execute(
        select(func.count()).select_from(query.subquery())
    )
    total = count_result.scalar()

    query = query.order_by(Trip.start_time.desc()).offset(offset).limit(limit)
    result = await db.execute(query)
    trips = result.scalars().all()

    return TripListResponse(trips=trips, total=total)


@router.get("/{trip_id}", response_model=TripResponse)
async def get_trip(
    trip_id: UUID,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    """Get trip details"""
    result = await db.execute(
        select(Trip)
        .join(Device)
        .where(Trip.id == trip_id, Device.owner_id == user.id)
    )
    trip = result.scalar_one_or_none()
    if not trip:
        raise HTTPException(status_code=404, detail="Trip not found")
    return trip


@router.get("/{trip_id}/route")
async def get_trip_route(
    trip_id: UUID,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    """Get all GPS points for a trip (for map rendering)"""
    # Verify ownership via trip -> device -> user
    trip_result = await db.execute(
        select(Trip)
        .join(Device)
        .where(Trip.id == trip_id, Device.owner_id == user.id)
    )
    trip = trip_result.scalar_one_or_none()
    if not trip:
        raise HTTPException(status_code=404, detail="Trip not found")

    # Import here to avoid circular
    from models import GpsLocation

    loc_result = await db.execute(
        select(GpsLocation)
        .where(GpsLocation.session_id == trip_id)
        .order_by(GpsLocation.timestamp_device.asc())
    )
    locations = loc_result.scalars().all()

    return {
        "trip_id": str(trip_id),
        "distance_meters": trip.distance_meters,
        "duration_seconds": trip.duration_seconds,
        "start_time": trip.start_time.isoformat() if trip.start_time else None,
        "end_time": trip.end_time.isoformat() if trip.end_time else None,
        "points": [
            {
                "lat": loc.latitude,
                "lon": loc.longitude,
                "speed": loc.speed,
                "timestamp": loc.timestamp_device.isoformat(),
            }
            for loc in locations
        ],
    }


@router.post("/{trip_id}/end")
async def end_trip(
    trip_id: UUID,
    user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    """Manually end an active trip"""
    trip_result = await db.execute(
        select(Trip)
        .join(Device)
        .where(Trip.id == trip_id, Device.owner_id == user.id, Trip.status == "active")
    )
    trip = trip_result.scalar_one_or_none()
    if not trip:
        raise HTTPException(status_code=404, detail="Active trip not found")

    trip.end_time = datetime.utcnow()
    trip.status = "completed"
    trip.duration_seconds = int(
        (trip.end_time - trip.start_time).total_seconds()
    )
    # Update end coordinates from last location
    from models import GpsLocation
    last_loc_result = await db.execute(
        select(GpsLocation)
        .where(GpsLocation.session_id == trip_id)
        .order_by(GpsLocation.timestamp_device.desc())
        .limit(1)
    )
    last_loc = last_loc_result.scalar_one_or_none()
    if last_loc:
        trip.end_latitude = last_loc.latitude
        trip.end_longitude = last_loc.longitude
        if trip.start_latitude is None:
            trip.start_latitude = last_loc.latitude
            trip.start_longitude = last_loc.longitude

    await db.commit()
    await db.refresh(trip)

    return {"message": "Trip ended", "trip_id": str(trip.id)}
