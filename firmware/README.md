# Firmware — ESP32-C3 GPS/GSM tracker

The firmware targets the custom `nologo_esp32c3_super_mini` board definition, Arduino framework and 4 MB flash. It uses a GPS receiver on UART, a SIM800 modem for GPRS/SMS/calls, one relay output and an ignition input. The source version is `1.0.0` in `include/config.h`.

## Build and configure

The public build pins PlatformIO Espressif32 6.10.0 (Arduino 2.0.17), ArduinoJson 6.21.6, TinyGPSPlus 1.1.0 and PubSubClient 2.8.0. [Upstream platform release](https://github.com/platformio/platform-espressif32/releases/tag/v6.10.0).

Install PlatformIO, copy `include/local_config.example.h` to `include/local_config.h`, and set your own APN, broker, device ID and optional OTA host. `config.h` includes private overrides first and supplies public defaults when an override is absent. MQTT topic constants derive from `DEVICE_ID`.

```sh
pio run -e nologo_esp32c3_super_mini
pio run -e nologo_esp32c3_super_mini -t upload
pio device monitor -b 115200
```

The upload port is detected by PlatformIO. Use `--upload-port YOUR_PORT` if detection selects the wrong board. Firmware output is `.pio/build/nologo_esp32c3_super_mini/firmware.bin`; generated output is excluded from Git.

## GPIO values in the code

| Setting | GPIO | Role |
| --- | --- | --- |
| `SIM800_RX` | 2 | ESP UART receive, connect to modem TX |
| `SIM800_TX` | 3 | ESP UART transmit, connect to modem RX |
| `SIM800_RST` | 4 | Modem reset control |
| `GPS_RX` | 5 | ESP UART receive, connect to GPS TX |
| `GPS_TX` | 6 | ESP UART transmit, connect to GPS RX |
| `RELAY_PIN` | 1 | Relay output; logical on drives HIGH |
| `LED_STATUS` | 10 | Connection/alert LED output |
| `IGNITION_PIN` | 0 | Ignition status input |
| `VOLTAGE_PIN` | 7 | Declared ADC input; live battery reporting is still a placeholder |

These are the actual integer definitions, not physical header pin numbers. Several old comments refer to different GPIOs; verify against your board and wiring. This source release contains no circuit diagram or PCB design. Match signal voltage levels, use an appropriate relay driver and isolation, and supply SIM800 from a suitable regulated supply with a common signal ground. The ESP GPIO cannot drive a relay coil or power a GSM module directly.

Relay state is restored from NVS at boot; it is not forcibly reset off every time. Validate output polarity and reboot behavior on a bench load before using the output in a vehicle. No speed interlock for relay shutdown is implemented in the command handler.

## Source map

| File/module | Responsibility |
| --- | --- |
| `src/main.cpp` | Setup, manager loops, watchdog, connection recovery, OTP/call/geofence/sync state machines and message dispatch |
| `include/config.h` | Public defaults, GPIOs, intervals, topic names, GPS filter and OTA settings |
| `src/managers/GPSManager.*` | TinyGPSPlus parsing, fix quality checks, implausible jump rejection, EMA smoothing and UTC timestamp extraction |
| `src/managers/SIM800Manager.*` | Modem startup, SIM/network checks, GPRS reconnect, SMS/UCS2 conversion and modem recovery |
| `lib/SIM800_Arduino/` | AT command queue, line parser, unsolicited notifications, modem/GPRS/TCP/HTTP state machines |
| `lib/SIM800Client/` | Arduino Client adapter for raw modem TCP, receive buffering, connection retry and PDP recovery |
| `src/managers/MQTTManager.*` | PubSubClient setup, subscriptions, JSON serialization and reconnect backoff |
| `src/managers/LocationBuffer.*` | RAM ring, LittleFS backlog, NVS sequence allocation and confirmation-based removal |
| `src/managers/OTAManager.*` | HTTP Range downloads, retries, segments, progress, stored update metadata, SHA-256 and restart |
| `src/handlers/RelayHandler.*` | Start/kill/toggle and NVS relay-state persistence |
| `boards/nologo_esp32c3_super_mini.json` | Custom MCU, USB, flash and upload settings |
| `partitions_ota.csv` | NVS, OTA metadata, PHY, LittleFS and two application slots |
| `test/basic/test_basic.cpp` | Existing Unity arithmetic smoke test, not a hardware test |

## Runtime order and timing

Setup starts the watchdog, initializes relay state, initializes the modem/GPRS/MQTT/GPS managers, starts backlog storage and checks pending OTA metadata. A missing SIM permits offline operation in one failure path; other repeated startup failures can restart the device.

The main loop feeds the watchdog and processes modem, MQTT, GPS and OTA work. During an active update, it skips much of the normal telemetry/auth/geofence work. Outside OTA it updates the authentication, call, geofence, sync and connection state machines.

| Default | Value |
| --- | --- |
| Live GPS update | 3 seconds |
| Heartbeat | 30 seconds |
| Backlog point append | 15 seconds |
| RAM points | 40 |
| RAM-to-flash chunk | 15 points |
| Backlog batch | 10 points |
| Sync retry / ACK timeout | 5 / 8 seconds |
| OTP wait / authorized relay session | 120 / 120 seconds |
| Wrong OTP attempts | 5 |
| Local geofence count | Up to 10 |
| Local geofence check | 5 seconds |
| Global speed-alert cooldown | 5 minutes |
| Automatic firmware check | 6 hours |
| GPS UART / debug serial | 9600 / 115200 baud |

Some operations still use blocking waits/delays, including relay LED effects and modem/network operations. The system is organized around state machines but is not completely non-blocking.

## Flash layout

The LittleFS partition is 256 KiB. Each OTA application slot is `0x1A0000` bytes (1,703,936 bytes). The custom board's declared maximum flash size alone is not the usable OTA application size; ensure `firmware.bin` fits an individual slot. The partition type for LittleFS is represented as `spiffs` in the CSV, while PlatformIO selects the `littlefs` filesystem implementation.

## Protocol and diagnostics

Use the [MQTT protocol guide](../docs/MQTT_PROTOCOL.md) when integrating clients. Configure a restricted broker account before remote operation. LED patterns are fast blink for GPRS unavailable, slow blink for GPS unavailable, solid for MQTT unavailable with GPRS/GPS present, and off when those checks pass. Alert blinking temporarily overrides connection indication.

GPS data includes a placeholder battery percentage and an HDOP value in `accuracy`; HDOP is not a distance in meters. Valid timestamps should come from GPS UTC. The fallback uptime value is not a Unix timestamp. These details matter for server history and charts.

See [OTA_ARCHITECTURE.md](docs/OTA_ARCHITECTURE.md) for update behavior and [known limitations](../docs/KNOWN_LIMITATIONS.md) for authorization and persistence limits.
