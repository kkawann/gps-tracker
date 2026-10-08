from sqlalchemy import (
    Column, String, Integer, Float, Boolean, DateTime,
    ForeignKey, Text, Enum as SAEnum, BigInteger
)
from sqlalchemy.dialects.postgresql import UUID, JSONB
from sqlalchemy.orm import relationship
from sqlalchemy.sql import func
from database import Base
import uuid


class User(Base):
    __tablename__ = "users"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    username = Column(String(50), unique=True, nullable=False)
    email = Column(String(255), unique=True, nullable=False)
    password_hash = Column(Text, nullable=False)
    full_name = Column(String(100))
    phone = Column(String(20))
    role = Column(String(20), default="user")
    is_active = Column(Boolean, default=True)
    created_at = Column(DateTime, server_default=func.now())
    updated_at = Column(DateTime, server_default=func.now(), onupdate=func.now())

    devices = relationship("Device", back_populates="owner")
    geofences = relationship("Geofence", back_populates="user")


class Device(Base):
    __tablename__ = "devices"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    device_uid = Column(String(100), unique=True, nullable=False)
    name = Column(String(100), nullable=False)
    model = Column(String(100))
    os_version = Column(String(20))
    app_version = Column(String(20))
    owner_id = Column(UUID(as_uuid=True), ForeignKey("users.id", ondelete="CASCADE"))
    is_online = Column(Boolean, default=False)
    last_seen = Column(DateTime)
    battery_level = Column(Integer)
    mqtt_topic = Column(String(255), unique=True, nullable=False)
    # Android Settings config
    engine = Column(Boolean, default=False)              # last known relay state (gps/{uid}/relay/status)
    speed_limit = Column(Integer, default=0)             # global speed limit pushed to ESP32
    authorized_phones = Column(JSONB, default=list)      # authorized phones list
    created_at = Column(DateTime, server_default=func.now())
    updated_at = Column(DateTime, server_default=func.now(), onupdate=func.now())

    owner = relationship("User", back_populates="devices")
    locations = relationship("GpsLocation", back_populates="device")
    trips = relationship("Trip", back_populates="device")
    alerts = relationship("Alert", back_populates="device")


class GpsLocation(Base):
    __tablename__ = "gps_locations"

    id = Column(BigInteger, primary_key=True, autoincrement=True)
    device_id = Column(UUID(as_uuid=True), ForeignKey("devices.id", ondelete="CASCADE"), nullable=False)
    latitude = Column(Float, nullable=False)
    longitude = Column(Float, nullable=False)
    altitude = Column(Float)
    speed = Column(Float)               # km/h
    bearing = Column(Float)             # degrees
    accuracy = Column(Float)            # meters
    battery_level = Column(Integer)
    network_type = Column(String(20))
    signal = Column(String(10), default="ok")
    satellites = Column(Integer)
    engine = Column(Boolean, default=False)
    seq = Column(BigInteger)  # For backlog sync ordering
    timestamp_device = Column(DateTime, nullable=False)
    received_at = Column(DateTime, server_default=func.now())
    session_id = Column(UUID(as_uuid=True))

    device = relationship("Device", back_populates="locations")


class Trip(Base):
    __tablename__ = "trips"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    device_id = Column(UUID(as_uuid=True), ForeignKey("devices.id", ondelete="CASCADE"), nullable=False)
    start_time = Column(DateTime, nullable=False)
    end_time = Column(DateTime)
    start_latitude = Column(Float)
    start_longitude = Column(Float)
    end_latitude = Column(Float)
    end_longitude = Column(Float)
    distance_meters = Column(Float, default=0)
    duration_seconds = Column(Integer, default=0)
    avg_speed = Column(Float)
    max_speed = Column(Float)
    status = Column(String(20), default="active")
    created_at = Column(DateTime, server_default=func.now())

    device = relationship("Device", back_populates="trips")


class Geofence(Base):
    __tablename__ = "geofences"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    user_id = Column(UUID(as_uuid=True), ForeignKey("users.id", ondelete="CASCADE"), nullable=False)
    device_id = Column(UUID(as_uuid=True), ForeignKey("devices.id", ondelete="SET NULL"), nullable=True)
    name = Column(String(100), nullable=False)
    description = Column(Text)
    latitude = Column(Float, nullable=False)
    longitude = Column(Float, nullable=False)
    radius_meters = Column(Float, nullable=False)
    geofence_type = Column(String(20), default="circle")
    polygon_points = Column(JSONB)
    alert_on_enter = Column(Boolean, default=True)
    alert_on_exit = Column(Boolean, default=True)
    is_active = Column(Boolean, default=True)
    # Android app per-fence options (pushed to ESP32 geofence/config)
    exit_method = Column(String(16), default="call")
    enter_method = Column(String(16), default="sms")
    speed_limit = Column(Integer, default=0)
    created_at = Column(DateTime, server_default=func.now())

    user = relationship("User", back_populates="geofences")


class Alert(Base):
    __tablename__ = "alerts"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    device_id = Column(UUID(as_uuid=True), ForeignKey("devices.id", ondelete="CASCADE"), nullable=False)
    geofence_id = Column(UUID(as_uuid=True), ForeignKey("geofences.id"))
    alert_type = Column(String(30), nullable=False)
    message = Column(Text)
    latitude = Column(Float)
    longitude = Column(Float)
    is_read = Column(Boolean, default=False)
    created_at = Column(DateTime, server_default=func.now())

    device = relationship("Device", back_populates="alerts")
