# Known implementation limits

These findings describe the existing source and publication-specific scope. The source release is not a certification of a production deployment or vehicle installation.

| Area | Concrete behavior and consequence | Relevant source |
| --- | --- | --- |
| MQTT trust | Clients sharing a device account can publish device control and result topics. HTTP JWT ownership checks do not protect direct MQTT traffic. Anonymous development listeners are localhost-only. | Firmware message dispatcher, server MQTT handler, broker ACL |
| First-time ownership | The OTP API can create an unowned placeholder device and asks the physical device to SMS a supplied phone. This is not an independent proof of physical ownership. | `server/backend/routes_auth.py`, `routes_devices.py` |
| OTP lifetime | Firmware OTP wait and relay session are 120 seconds. Server `OTP_EXPIRE_MS` is 300 seconds, but pending request/result coordination is stored in memory and device verification is authoritative. | `firmware/src/main.cpp`, server settings/MQTT handler |
| OTP logging | Existing firmware debug prints include entered OTP/phone data. Do not publish runtime logs without redaction. | `firmware/src/main.cpp` |
| Relay safety | State is restored from NVS, on is HIGH, and no speed shutdown interlock is implemented. Some relay LED effects use blocking delays. | `RelayHandler.cpp`, `handleRelayCommand()` |
| Relay session mismatch | Firmware requires a valid 120-second auth session for every relay command. The server/app start flow can send start without obtaining a fresh session, so commands can be silently rejected after expiry. | Firmware command handler, server relay start endpoint, Android dashboard |
| Backlog commit order | The server advances `_last_seq` before `session.commit()`. A failed commit can leave its in-memory watermark ahead of persisted points. | `server/backend/mqtt_handler.py::_process_backlog` |
| Backlog coordinate tests | Truthy `lat`/`lng` checks can reject valid zero coordinates; a watermark can still advance. | Same handler |
| Timestamp fallback | Firmware uses uptime when GPS UTC is unavailable. Server conversion can interpret it as a Unix epoch. | `publishGPS()`, server location/backlog handlers |
| Trip sessions | Firmware uses a device UID string as `session_id`, while server trip sessions expect UUIDs. | `publishGPS()`, server models/MQTT handler |
| Telemetry units | Firmware battery is 100 and `accuracy` contains HDOP, not meters. | `publishGPS()` |
| Two location sources | Android foreground service and hardware can publish locations under the same device UID. | `GpsForegroundService.kt`, firmware publisher |
| OTA distribution | The backup lacks `/api/firmware/latest` and firmware upload/download distribution routes. | Server route inventory, OTAManager |
| OTA trust | Plain HTTP plus supplied SHA-256 does not provide an independent signed-image authenticity chain. Interruption recovery needs hardware testing. | OTAManager |
| Server scaling | Pending auth/futures, geofence and sync state are in process memory; Redis is configured but does not back this state. Use one backend worker. | MQTTHandler |
| Logout semantics | API logout tells the client to remove its JWT; it does not revoke issued tokens server-side. | `routes_auth.py::logout` |
| Existing databases | `create_all()` creates missing tables but is not a schema migration engine. Historical SQL differs from current models. | `database.py`, `archive/legacy-init.sql` |
| Network transport | Android permits cleartext HTTP and MQTT uses plain TCP. Firmware SIM800 transport also uses raw TCP. | Android manifest/MQTT manager, SIM800Client |
| APK settings | BuildConfig credentials can be recovered from an APK. Use restricted device accounts; no secure enrollment service is provided. | Android Gradle configuration |
| Broker parsing | Active Android parser now handles `tcp://host:port` correctly, but not IPv6 literals or TLS schemes. | `MqttManager.kt` |
| GPS wiring | Old comments disagree with some defined GPIO values. No hardware schematic/PCB artifact was supplied. | `firmware/include/config.h` |
| Geofence capabilities | Firmware supports ten local circles. Polygon database fields do not imply implemented polygon behavior throughout the system. | Main firmware/geofence models |
| Health reporting | `/health` reports backend process health without testing every dependent service. | `server/backend/main.py` |
| Dependency reproducibility | Some original dependencies use version ranges. Validation records resolved versions; no dependency-upgrade/security audit is claimed. | PlatformIO/Python requirements |

Changes in this release address publication and reproducibility: private settings, standalone development deployment, bootstrap utilities, portable Android endpoints/signing, and one broker-port parsing fix. Broader changes to authentication, routing, OTA trust and persistence should be reviewed and tested as separate functional work.
