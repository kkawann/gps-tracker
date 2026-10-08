# Source provenance and publication changes

Source assembly date: 2026-10-09. Server backup date in the supplied artifact: 2026-10-08.

| Input | Public location | Selection |
| --- | --- | --- |
| `gps_v1.0.0_FIXED/` | `firmware/` | Full source, custom libraries, board definition, partitions and existing basic test |
| `gpsv1_final/android/` | `android/` | Active Android Gradle project and `app/src/main` |
| Nested `android/android/` | `archive/android-project-copy/` | Earlier source/assets, with temporary files and keys omitted |
| Nested `app/src/src/` | `archive/android-source-copy/` | Earlier source/resources |
| ZIP `gps_stage/gps-server/backend/` | `server/backend/` | Newer on-disk backend |
| ZIP `gps_stage/gps-server/dashboard/` | `server/dashboard/` | Static dashboard source |
| ZIP `running-code/backend/` | `archive/server-runtime-backend/` | Only four files differing from the disk version |
| ZIP SQL script | `archive/legacy-init.sql` | Historical schema without default administrator seed |

The four differing runtime files are `models.py`, `mqtt_handler.py`, `routes_geofence_app.py` and `routes_gps_geofences.py`. The on-disk versions include geofence ownership filtering, enter/exit event publishing and deletion handling for alert foreign keys. The original input artifacts remain untouched outside this repository.

## Omitted material

- Raw server backup ZIP, container metadata/environment dumps and production diff exports.
- TLS certificates/private keys, broker persistence, logs, local SDK paths and keystores.
- Build output, IDE/runtime caches and local agent folders.
- Production credential values and deployment addresses.
- ACME challenge/probe files and machine-specific RelayPro reverse-proxy configuration.

No user accounts or real GPS history are included as seed data. Useful code from the ZIP is present as source files rather than an opaque archive.

## Publication changes

1. Added MIT project license, font notices, contribution guide, bilingual overview and detailed component/protocol guides.
2. Created a standalone, localhost-bound Compose development stack. Removed the backup's external RelayPro Docker network, private certificate bindings and unused Node-RED deployment dependency.
3. Replaced hardcoded backend secrets with required environment values. Added non-overwriting random-config generation and interactive administrator bootstrap.
4. Added firmware private overrides and example configuration. MQTT topic strings derive from `DEVICE_ID`. Removed the developer's fixed upload port.
5. Added private Android endpoint/broker properties and BuildConfig defaults, removed machine-specific Java paths and published debug signing material, and left release signing unsigned.
6. Wired existing Android MQTT credential inputs into initial login connection. Corrected port parsing after removing `tcp://` so configured non-default ports work.
7. Kept earlier source versions in an explicit reference archive and the current ORM schema as fresh-install authority.
8. Pinned the firmware platform to official Espressif32 6.10.0 and library versions used for the publication build, avoiding machine-dependent platform selection.

Detailed verification and any subsequent build repairs appear in `VALIDATION.md`.
