from pydantic import BaseModel, EmailStr, Field
from typing import Optional, List
from datetime import datetime
from uuid import UUID


# ─── Auth Schemas ───────────────────────────────────────
class UserCreate(BaseModel):
    username: str = Field(..., min_length=3, max_length=50)
    email: str
    password: str = Field(..., min_length=6)
    full_name: Optional[str] = None
    phone: Optional[str] = None


class UserLogin(BaseModel):
    username: str
    password: str


class TokenResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"
    user_id: UUID
    username: str


# ─── OTP Auth Schemas ──────────────────────────────────
class OtpRequest(BaseModel):
    device_uid: str
    phone: str = Field(..., description="Phone number for SMS OTP")


class OtpVerify(BaseModel):
    device_uid: str
    code: str = Field(..., min_length=4, max_length=6)


class CodeOnly(BaseModel):
    code: str = Field(..., min_length=1, max_length=6)


# ─── Device Schemas ─────────────────────────────────────
class DeviceCreate(BaseModel):
    device_uid: str = Field(..., description="Unique ID from Android app")
    name: str
    model: Optional[str] = None


class DeviceResponse(BaseModel):
    id: UUID
    device_uid: str
    name: str
    model: Optional[str]
    os_version: Optional[str]
    app_version: Optional[str]
    is_online: bool
    last_seen: Optional[datetime]
    battery_level: Optional[int]
    mqtt_topic: str
    created_at: datetime

    class Config:
        from_attributes = True


class DeviceListResponse(BaseModel):
    devices: List[DeviceResponse]
    total: int


# ─── GPS Location Schemas ───────────────────────────────
class LocationSubmit(BaseModel):
    lat: float = Field(..., ge=-90, le=90)
    lon: float = Field(..., ge=-180, le=180)
    alt: Optional[float] = None
    speed: Optional[float] = None
    bearing: Optional[float] = None
    accuracy: Optional[float] = None
    battery: Optional[int] = None
    network_type: Optional[str] = None
    timestamp: Optional[float] = None
    session_id: Optional[str] = None


class LocationResponse(BaseModel):
    id: int
    latitude: float
    longitude: float
    altitude: Optional[float]
    speed: Optional[float]
    bearing: Optional[float]
    accuracy: Optional[float]
    battery_level: Optional[int]
    signal: Optional[str]
    satellites: Optional[int]
    engine: Optional[bool]
    seq: Optional[int]
    timestamp_device: datetime
    received_at: datetime

    class Config:
        from_attributes = True


class LocationHistory(BaseModel):
    locations: List[LocationResponse]
    total: int
    device_id: UUID


# ─── Trip Schemas ───────────────────────────────────────
class TripResponse(BaseModel):
    id: UUID
    device_id: UUID
    start_time: datetime
    end_time: Optional[datetime]
    start_latitude: Optional[float]
    start_longitude: Optional[float]
    end_latitude: Optional[float]
    end_longitude: Optional[float]
    distance_meters: float
    duration_seconds: int
    avg_speed: Optional[float]
    max_speed: Optional[float]
    status: str

    class Config:
        from_attributes = True


class TripListResponse(BaseModel):
    trips: List[TripResponse]
    total: int


# ─── Geofence Schemas ──────────────────────────────────
class GeofenceCreate(BaseModel):
    name: str
    description: Optional[str] = None
    latitude: float = Field(..., ge=-90, le=90)
    longitude: float = Field(..., ge=-180, le=180)
    radius_meters: float = Field(..., gt=0, le=50000)
    device_id: Optional[UUID] = None
    alert_on_enter: bool = True
    alert_on_exit: bool = True


class GeofenceResponse(BaseModel):
    id: UUID
    name: str
    description: Optional[str]
    latitude: float
    longitude: float
    radius_meters: float
    alert_on_enter: bool
    alert_on_exit: bool
    is_active: bool
    created_at: datetime

    class Config:
        from_attributes = True


# ─── Alert Schemas ──────────────────────────────────────
class AlertResponse(BaseModel):
    id: UUID
    device_id: UUID
    alert_type: str
    message: Optional[str]
    latitude: Optional[float]
    longitude: Optional[float]
    is_read: bool
    created_at: datetime

    class Config:
        from_attributes = True


class AlertListResponse(BaseModel):
    alerts: List[AlertResponse]
    total: int
    unread_count: int


# ─── Stats Schemas ──────────────────────────────────────
class DeviceStats(BaseModel):
    device_id: UUID
    total_distance_km: float
    total_trips: int
    total_locations: int
    avg_speed_kmh: float
    max_speed_kmh: float
    first_seen: Optional[datetime]
    last_seen: Optional[datetime]


class DailyStats(BaseModel):
    date: str
    distance_km: float
    points: int
    avg_speed: float
    max_speed: float
    active_minutes: float
