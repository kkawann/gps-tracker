# Android — Kotlin GPS Tracker / Aurora UI

The active project is this directory, with the app module at `app/`. Earlier nested copies are preserved under the root `archive/` directory and do not participate in this build. This is a native Kotlin app; React Native scaffolding comments in old ignore files do not describe its current build.

## Build requirements

| Setting | Value in the active project |
| --- | --- |
| Java/Kotlin JVM target | 17 |
| Android Gradle Plugin | 8.2.2 |
| Kotlin plugin | 1.9.24 |
| Gradle wrapper | See `gradle/wrapper/gradle-wrapper.properties` |
| Android SDK compile / target | 34 / 34 |
| Minimum Android SDK | 21 |
| Application ID | `com.gpsv1_final` |
| App version | `2.0-Aurora`, version code 2 |

Install the Android SDK and accept its licenses through Android Studio. Open this folder and allow Gradle sync. On Windows use `gradlew.bat :app:assembleDebug`; on Linux/macOS use `sh gradlew :app:assembleDebug`. Output is `app/build/outputs/apk/debug/app-debug.apk`.

Debug signing uses Android's locally generated debug keystore. No keystore is published. `assembleRelease` produces an unsigned release artifact; set up your own private signing for distribution.

## Configure endpoints before first login

Copy `gps.properties.example` to `gps.properties`. It configures BuildConfig defaults:

| Property | Purpose |
| --- | --- |
| `server.url` | HTTP API origin, including port if needed |
| `mqtt.broker` | `tcp://hostname:port` MQTT endpoint |
| `mqtt.username` | Optional restricted account for the device/app |
| `mqtt.password` | Password for that restricted account |

The default host `10.0.2.2` reaches the host computer from a standard Android emulator. Use an actual reachable server address on a physical phone. Server URL and broker settings already saved in SharedPreferences take precedence over new defaults; update saved settings or clear app data when changing environments.

The authentication fragment and foreground service use the same configured MQTT credential defaults. Credentials compiled into an APK are recoverable; do not embed a shared backend administrator account. Use a per-device account and matching MQTT ACL. The existing broker parser supports TCP hostnames/IPv4 and a port; it does not implement IPv6 literals or MQTT TLS.

## Code and screens

Paths below are relative to `app/src/main/java/com/gpsv1_final/`.

| Module | Responsibility |
| --- | --- |
| `App.kt` | Application-level Room database, shared MQTT manager and notification channel |
| `mqtt/MqttManager.kt` | HiveMQ MQTT connection/subscriptions, location/auth/relay/geofence message conversion and callbacks |
| `service/GpsForegroundService.kt` | Foreground notification, MQTT reconnect and Fused Location updates from the phone |
| `ui/MainActivity.kt` | Navigation host and animated bottom navigation |
| `ui/auth/` | Password/OTP login, device registration and navigation after authentication |
| `ui/dashboard/` | OSMDroid map, vehicle marker, speed/connection state and relay OTP interaction |
| `ui/history/` | Daily server history, time-range selection, route coloring, stop/weak-area calculations and statistics |
| `ui/geofence/` | Map circles, creating/editing/deleting fences, API synchronization and MQTT config publishing |
| `ui/settings/` | Device name, authorized phones, speed limit, server/broker settings and logout |
| `data/` | Room database and DAOs for GPS points, trips and geofences |
| `model/` | GPS/trip/geofence entities and mobile compatibility models |
| `ui/widgets/` | AnimatedCarMarker, GlowDot, PulseRadar, SparklineView and SpeedGauge |

XML layouts, navigation, styles and drawable resources live in `app/src/main/res/`. The manifest declares network/location/notification/foreground service permissions and uses `.ui.MainActivity`. `README_AURORA.md` describes design intent; this guide's build versions reflect the actual Gradle files.

## Data flow

1. AuthViewModel calls server auth endpoints and saves the JWT/device identity in `gps_prefs`.
2. AuthFragment constructs/reuses an MQTT manager and navigates after connection result.
3. MQTT messages update dashboard state and local storage. History and configuration screens query HTTP endpoints with a Bearer token.
4. Geofence changes are saved to the server and can also be pushed to the ESP32 over MQTT.
5. GpsForegroundService can publish the phone's position to `gps/<device_uid>/location` while maintaining a visible notification.

Because the hardware also publishes that topic, decide whether phone-origin location tracking is intended for the chosen device ID. Sharing an ID can mix telemetry sources. The app's local database is a cache; it is not the server's PostgreSQL database.

## Permissions and network behavior

Location and notification/foreground-service permissions depend on Android version. Grant them on a test device/emulator when testing the service. The manifest currently allows cleartext HTTP and the MQTT client uses plain TCP. HTTPS can be selected for the API if a trusted TLS endpoint is available; MQTT TLS requires code changes.

## UI resources

The Aurora theme uses dark backgrounds, cyan/blue/green status accents, translucent cards, animated gauges and radar effects. Vazirmatn and Inter fonts are bundled with their OFL notices at the root `LICENSES/` folder. OSMDroid supplies OpenStreetMap rendering; keep map provider attribution visible.

See [known limitations](../docs/KNOWN_LIMITATIONS.md) for trust, relay, server compatibility and history behavior that compilation alone cannot validate.
