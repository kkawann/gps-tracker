# Server — API, MQTT ingestion and dashboard

The server uses Python 3.12, FastAPI, SQLAlchemy's async engine and PostgreSQL 16. Paho MQTT dispatches messages from devices into backend handlers. Nginx hosts the static dashboard and proxies API traffic. Docker Compose supplies a Redis service, although pending OTP and verification state is held in memory in this source version.

## Run locally

Install Docker with Compose and run from this directory:

```sh
python scripts/init_config.py
docker compose up -d --build
docker compose exec backend python create_admin.py
docker compose logs -f backend mosquitto
```

The initialization script creates random private database/API/JWT values in `.env` and refuses to overwrite an existing file. The administrator helper prompts for a new password and refuses to replace an existing username/email. The dashboard has no published default account.

| URL | Purpose |
| --- | --- |
| `http://localhost:8081` | Browser dashboard |
| `http://localhost:8081/docs` | Interactive API documentation |
| `http://localhost:8081/openapi.json` | Generated API contract |
| `http://localhost:8081/health` | Backend process health |

PostgreSQL and Redis have no host port mappings. MQTT 1883/WebSocket 9001 and HTTP 8081 are mapped to `127.0.0.1` by default. Android emulator access uses `10.0.2.2`; physical devices require a reachable address and configured broker access. `/health` reports process availability, not a comprehensive database/MQTT health assessment.

## Source modules

| File | Responsibility |
| --- | --- |
| `backend/main.py` | App construction, CORS, route registration and MQTT lifespan |
| `backend/config.py` | Environment settings; database URL, API key and JWT secret are required |
| `backend/database.py` | Async engine/session factory and model-based initialization |
| `backend/models.py` | User, Device, GpsLocation, Trip, Geofence and Alert tables |
| `backend/schemas.py` | Request/response validation and serialization |
| `backend/auth.py` | Password hashing, JWT creation/verification, current-user and admin dependencies |
| `backend/mqtt_handler.py` | MQTT ingestion, location/backlog persistence, OTP request/result coordination and geofence alerts |
| `backend/gps_filter.py` | Distance, signal/outlier filtering, parking detection and history simplification |
| `backend/routes_auth.py` | Password login, device-mediated SMS OTP, logout, refresh and session check |
| `backend/routes_devices.py` | Owned devices, names/authorized phones/speed settings, locations and relay endpoints |
| `backend/routes_trips.py` | Trip listing, detail, routes and trip end |
| `backend/routes_geofences.py` | Generic geofence CRUD |
| `backend/routes_geofence_app.py` | Android-compatible geofence fields and device routes |
| `backend/routes_gps_geofences.py` | Compatibility routes using the GPS frontend's geofence format |
| `backend/routes_admin.py` | Admin statistics, user operations and device operations |
| `backend/create_admin.py` | Explicit local administrator bootstrap |
| `dashboard/index.html` | Existing browser dashboard; API requests use the page's origin |
| `nginx/nginx.conf` | Same-origin API proxy and auth/API rate limits |
| `mosquitto/config/` | Development broker and alternative authenticated broker configuration |

See [API_ROUTES.md](../docs/API_ROUTES.md) for a table generated from the actual route declarations. Swagger describes the concrete input/output schemas.

## Configure authenticated MQTT before remote access

The development broker permits anonymous local clients. For a physical device, create a backend account and a device-specific account **before** opening a host binding. From this directory:

```sh
# The first command creates a new password file. Run it only for first setup.
docker compose run --rm --no-deps mosquitto mosquitto_passwd -c /mosquitto/config/password.txt backend
# Add device/app account without -c, which would overwrite the previous account.
docker compose run --rm --no-deps mosquitto mosquitto_passwd /mosquitto/config/password.txt device_1
```

Copy `mosquitto.secure.conf` over `mosquitto.conf` locally. Set `MQTT_USERNAME=backend` and the selected backend password in private `.env`. Set the firmware and Android MQTT username to `device_1` and supply that account's password in private configuration. The ACL grants the backend `gps/#` and other accounts their own `gps/<username>/#` subtree. Match usernames to device IDs.

Set `MQTT_BIND_ADDRESS` and `HTTP_BIND_ADDRESS` to the desired interface, restrict access with your network/firewall, then recreate services with `docker compose up -d --force-recreate`. The supplied SIM800/Android MQTT implementations use plain TCP; HTTPS terminates HTTP only. Use a private network/tunnel or implement compatible MQTT TLS before exposing credential-bearing MQTT traffic to untrusted networks.

Android supports local build-time broker credentials for this source release. Credentials embedded in an APK are recoverable; use restricted per-device accounts. This ACL separates devices but cannot distinguish the same device account's app commands from device-originated messages. See the documented authorization limits.

## Database and backup provenance

On an empty database the backend's `init_db()` creates tables from current SQLAlchemy models. The old backup SQL file is preserved in `archive/legacy-init.sql`; it is not mounted because it lacks fields the current code uses. `create_all` is not a migration runner: existing installations need explicit migrations and backup/restore planning.

The public source uses the backup's on-disk backend. It includes four newer files than the running image at backup time; those older files are preserved in `archive/server-runtime-backend/`. This repository includes source, not real user accounts, GPS history or broker persistence.

Run one Uvicorn worker: `_pending_auth`, `_pending_verify` and some geofence/sync state are in process memory. Restarting loses pending OTP operations. Redis is not a substitute until code actually moves state into it.

## Firmware delivery

The ESP32 expects `/api/firmware/latest` and Range-capable firmware download URLs. Neither an OTA distribution route nor uploaded firmware files are present in the supplied backend. Supply a separate controlled firmware distribution service; do not assume this Compose stack completes OTA support.

## Development without Docker

Create a Python 3.12 virtual environment, install `backend/requirements.txt`, and provide `DATABASE_URL`, `API_KEY`, `JWT_SECRET`, MQTT and optional Redis environment values. Run `uvicorn main:app --host 127.0.0.1 --port 8000` from `backend/`. A reachable PostgreSQL/MQTT setup is needed for live application startup. Import-only checks can inspect OpenAPI without starting the lifespan.

## Operations

Use `docker compose stop` to stop services while preserving their named volumes. Back up PostgreSQL and MQTT state separately from source. `.env` and broker password files are intentionally ignored. The public stack replaces the backup's machine-specific RelayPro proxy, external Docker network, certificates and unused Node-RED deployment with standalone GPS development configuration.
