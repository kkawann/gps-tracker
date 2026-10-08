-- ═══════════════════════════════════════════════════════
-- GPS Tracker - Database Schema
-- PostgreSQL 16
-- ═══════════════════════════════════════════════════════

-- ─── UUID Extension ────────────────────────────────────
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- ─── Users Table ───────────────────────────────────────
CREATE TABLE users (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    username VARCHAR(50) UNIQUE NOT NULL,
    email VARCHAR(255) UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    full_name VARCHAR(100),
    phone VARCHAR(20),
    role VARCHAR(20) DEFAULT 'user' CHECK (role IN ('admin', 'user', 'viewer')),
    is_active BOOLEAN DEFAULT true,
    created_at TIMESTAMP DEFAULT NOW(),
    updated_at TIMESTAMP DEFAULT NOW()
);

-- ─── Devices Table ─────────────────────────────────────
CREATE TABLE devices (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    device_uid VARCHAR(100) UNIQUE NOT NULL,  -- Unique device identifier from Android
    name VARCHAR(100) NOT NULL,
    model VARCHAR(100),
    os_version VARCHAR(20),
    app_version VARCHAR(20),
    owner_id UUID REFERENCES users(id) ON DELETE CASCADE,
    is_online BOOLEAN DEFAULT false,
    last_seen TIMESTAMP,
    battery_level INTEGER CHECK (battery_level >= 0 AND battery_level <= 100),
    mqtt_topic VARCHAR(255) UNIQUE NOT NULL,
    created_at TIMESTAMP DEFAULT NOW(),
    updated_at TIMESTAMP DEFAULT NOW()
);

-- ─── GPS Locations Table (Time-Series Data) ────────────
CREATE TABLE gps_locations (
    id BIGSERIAL PRIMARY KEY,
    device_id UUID NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    latitude DOUBLE PRECISION NOT NULL,
    longitude DOUBLE PRECISION NOT NULL,
    altitude DOUBLE PRECISION,
    speed DOUBLE PRECISION,           -- km/h
    bearing DOUBLE PRECISION,         -- degrees (0-360)
    accuracy DOUBLE PRECISION,        -- meters
    battery_level INTEGER,
    network_type VARCHAR(20),         -- wifi, mobile, gps
    timestamp_device TIMESTAMP NOT NULL,  -- Device timestamp
    received_at TIMESTAMP DEFAULT NOW(),  -- Server timestamp
    session_id UUID                   -- Group locations into trips
);

-- ─── Indexes for GPS Data ──────────────────────────────
CREATE INDEX idx_gps_device_id ON gps_locations(device_id);
CREATE INDEX idx_gps_timestamp ON gps_locations(timestamp_device DESC);
CREATE INDEX idx_gps_session ON gps_locations(session_id);
CREATE INDEX idx_gps_device_time ON gps_locations(device_id, timestamp_device DESC);

-- ─── Trips / Sessions Table ────────────────────────────
CREATE TABLE trips (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    device_id UUID NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    start_time TIMESTAMP NOT NULL,
    end_time TIMESTAMP,
    start_latitude DOUBLE PRECISION,
    start_longitude DOUBLE PRECISION,
    end_latitude DOUBLE PRECISION,
    end_longitude DOUBLE PRECISION,
    distance_meters DOUBLE PRECISION DEFAULT 0,  -- Total distance in meters
    duration_seconds INTEGER DEFAULT 0,
    avg_speed DOUBLE PRECISION,
    max_speed DOUBLE PRECISION,
    status VARCHAR(20) DEFAULT 'active' CHECK (status IN ('active', 'completed', 'paused')),
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE INDEX idx_trips_device ON trips(device_id);
CREATE INDEX idx_trips_time ON trips(start_time DESC);

-- ─── Geofences Table ───────────────────────────────────
CREATE TABLE geofences (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name VARCHAR(100) NOT NULL,
    description TEXT,
    latitude DOUBLE PRECISION NOT NULL,
    longitude DOUBLE PRECISION NOT NULL,
    radius_meters DOUBLE PRECISION NOT NULL,  -- Circle radius
    geofence_type VARCHAR(20) DEFAULT 'circle' CHECK (geofence_type IN ('circle', 'polygon')),
    polygon_points JSONB,                    -- For polygon geofences
    alert_on_enter BOOLEAN DEFAULT true,
    alert_on_exit BOOLEAN DEFAULT true,
    is_active BOOLEAN DEFAULT true,
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE INDEX idx_geofences_user ON geofences(user_id);

-- ─── Alerts Table ──────────────────────────────────────
CREATE TABLE alerts (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    device_id UUID NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    geofence_id UUID REFERENCES geofences(id) ON DELETE SET NULL,
    alert_type VARCHAR(30) NOT NULL CHECK (alert_type IN (
        'geofence_enter', 'geofence_exit', 'speed_limit',
        'low_battery', 'offline', 'sos', 'overspeed'
    )),
    message TEXT,
    latitude DOUBLE PRECISION,
    longitude DOUBLE PRECISION,
    is_read BOOLEAN DEFAULT false,
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE INDEX idx_alerts_device ON alerts(device_id);
CREATE INDEX idx_alerts_unread ON alerts(is_read) WHERE is_read = false;

-- ─── Daily Stats Materialized View ─────────────────────
CREATE MATERIALIZED VIEW daily_device_stats AS
SELECT
    device_id,
    DATE(timestamp_device) as stat_date,
    COUNT(*) as total_points,
    SUM(distance_meters) as total_distance,  -- This is approximate
    AVG(speed) as avg_speed,
    MAX(speed) as max_speed,
    MIN(battery_level) as min_battery,
    MAX(battery_level) as max_battery,
    EXTRACT(EPOCH FROM (MAX(timestamp_device) - MIN(timestamp_device))) / 60 as active_minutes
FROM (
    SELECT
        device_id,
        timestamp_device,
        speed,
        battery_level,
        0 as distance_meters
    FROM gps_locations
) sub
GROUP BY device_id, DATE(timestamp_device);

CREATE UNIQUE INDEX idx_daily_stats ON daily_device_stats(device_id, stat_date);

-- ─── Function: Calculate Distance (Haversine) ──────────
CREATE OR REPLACE FUNCTION haversine_distance(
    lat1 DOUBLE PRECISION, lon1 DOUBLE PRECISION,
    lat2 DOUBLE PRECISION, lon2 DOUBLE PRECISION
) RETURNS DOUBLE PRECISION AS $$
DECLARE
    R DOUBLE PRECISION := 6371000;  -- Earth radius in meters
    dlat DOUBLE PRECISION;
    dlon DOUBLE PRECISION;
    a DOUBLE PRECISION;
    c DOUBLE PRECISION;
BEGIN
    dlat := RADIANS(lat2 - lat1);
    dlon := RADIANS(lon2 - lon1);
    a := SIN(dlat/2) * SIN(dlat/2) +
         COS(RADIANS(lat1)) * COS(RADIANS(lat2)) *
         SIN(dlon/2) * SIN(dlon/2);
    c := 2 * ATAN2(SQRT(a), SQRT(1-a));
    RETURN R * c;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

-- ─── Function: Calculate Trip Distance ─────────────────
CREATE OR REPLACE FUNCTION calculate_trip_distance(p_trip_id UUID)
RETURNS DOUBLE PRECISION AS $$
DECLARE
    total_dist DOUBLE PRECISION := 0;
    prev_lat DOUBLE PRECISION;
    prev_lon DOUBLE PRECISION;
    cur_lat DOUBLE PRECISION;
    cur_lon DOUBLE PRECISION;
    rec RECORD;
BEGIN
    FOR rec IN
        SELECT latitude, longitude
        FROM gps_locations
        WHERE session_id = p_trip_id
        ORDER BY timestamp_device ASC
    LOOP
        cur_lat := rec.latitude;
        cur_lon := rec.longitude;

        IF prev_lat IS NOT NULL THEN
            total_dist := total_dist + haversine_distance(
                prev_lat, prev_lon, cur_lat, cur_lon
            );
        END IF;

        prev_lat := cur_lat;
        prev_lon := cur_lon;
    END LOOP;

    RETURN total_dist;
END;
$$ LANGUAGE plpgsql;

-- ─── Seed Data (Optional - Admin User) ─────────────────
-- Default account seed removed from the public snapshot.

-- ─── Refresh Function ──────────────────────────────────
CREATE OR REPLACE FUNCTION refresh_daily_stats()
RETURNS void AS $$
BEGIN
    REFRESH MATERIALIZED VIEW CONCURRENTLY daily_device_stats;
END;
$$ LANGUAGE plpgsql;
