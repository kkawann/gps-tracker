# GPS Tracker

این پروژه یک ردیاب GPS متن‌باز است که از سه بخش تشکیل شده: فریمور ESP32-C3 برای ارتباط با GPS و ماژول SIM800، اپ اندروید برای نمایش موقعیت و مدیریت دستگاه، و سرور FastAPI برای دریافت و ذخیرهٔ اطلاعات. امکانات آن شامل موقعیت‌یابی زنده، تاریخچهٔ مسیر، کنترل رله، محدودهٔ جغرافیایی و به‌روزرسانی فریمور است. توضیحات نصب و عملکرد هر بخش در [راهنمای کامل فارسی](README.fa.md) آمده است.

An open-source GPS tracking system with ESP32-C3 firmware, an Android application, and a self-hosted FastAPI server. GPS telemetry, SMS verification, relay control, circular geofences, offline location recovery, and firmware updates share a device-specific MQTT protocol.

**[راهنمای کامل فارسی](README.fa.md)** · [Architecture](docs/ARCHITECTURE.md) · [MQTT protocol](docs/MQTT_PROTOCOL.md) · [API routes](docs/API_ROUTES.md) · [Source inventory](docs/SOURCE_INVENTORY.md) · [Known limitations](docs/KNOWN_LIMITATIONS.md)

This is a source release of one existing project, assembled from its firmware, Android project, and server backup dated 2026-10-08. It includes the newer server files present on disk at backup time. See [source provenance](docs/SOURCE_PROVENANCE.md) for preserved earlier source variants and publication changes.

## Components

| Directory | Responsibility | Start here |
| --- | --- | --- |
| [`firmware/`](firmware/README.md) | ESP32-C3, GPS UART, SIM800 GPRS/SMS, relay, geofences, offline backlog, OTA | PlatformIO build and wiring |
| [`android/`](android/README.md) | Kotlin app, OpenStreetMap dashboard, history, geofences, authentication, foreground tracking | Android Studio or Gradle |
| [`server/`](server/README.md) | FastAPI, PostgreSQL, MQTT ingestion, JWT, device management, dashboard | Docker Compose |
| [`docs/`](docs/) | Architecture, protocol, compatibility, source inventory and limitations | Full system explanation |
| [`archive/`](archive/README.md) | Earlier Android source copies, four older runtime server files, historical SQL schema | Reference only |
| [`LICENSES/`](LICENSES/) | Redistribution notices for bundled fonts | Third-party licenses |

## Features in the source

- Live location and connection/engine status over MQTT.
- GPS quality filtering, jump rejection, and smoothing.
- Location buffering in RAM and LittleFS, with sequence-based recovery after reconnect.
- SMS OTP login and protected relay shutdown flows.
- Circular geofences with entry, exit, speed, SMS, and call behavior in firmware.
- Android map, speed gauge, route history, local Room database, and Aurora UI widgets.
- HTTP Range firmware downloading through SIM800, dual OTA partitions, progress reporting and SHA-256 checking.
- FastAPI endpoints for authentication, devices, trips, geofences, and administration.

## Quick start

### 1. Server on a development computer

```sh
cd server
python scripts/init_config.py
docker compose up -d --build
docker compose exec backend python create_admin.py
```

Open the dashboard at `http://localhost:8081`, API documentation at `http://localhost:8081/docs`, and health status at `http://localhost:8081/health`. Configuration generation creates private random application/database secrets. The development MQTT listeners are anonymous and **bound to localhost**. Read the server guide before making the broker reachable from hardware or a phone.

### 2. Firmware

```sh
cd firmware
# Copy include/local_config.example.h to include/local_config.h and edit it.
pio run -e nologo_esp32c3_super_mini
pio run -e nologo_esp32c3_super_mini -t upload
pio device monitor -b 115200
```

Configure the SIM APN, broker address, device ID and wiring for your own setup. An actual ESP32-C3 board is required for upload and hardware verification.

### 3. Android

Open `android/` in Android Studio with JDK 17 and Android SDK 34. Copy `gps.properties.example` to `gps.properties` and set your API/broker addresses before the first login. The defaults target the Android emulator's host at `10.0.2.2`. On Windows:

```powershell
cd android
.\gradlew.bat :app:assembleDebug
```

Debug APKs use Android's locally generated debug key. Release builds are unsigned; supply your own signing configuration for distribution.

## Scope and verification

Build/import checks and their results are recorded in [VALIDATION.md](docs/VALIDATION.md). Compilation does not establish working GSM coverage, correct vehicle wiring, end-to-end SMS, or safe relay behavior. The project retains existing protocol and authorization limitations, documented with concrete code references.

The backup's backend does not contain the firmware distribution API expected by automatic OTA checks. The download service must be supplied separately. Redis is provisioned by the server stack, but this backend currently keeps pending OTP and verification state in process memory.

## Contributing and license

See [CONTRIBUTING.md](CONTRIBUTING.md). Project code and documentation are released under [MIT](LICENSE); bundled fonts and external dependencies retain their own licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
