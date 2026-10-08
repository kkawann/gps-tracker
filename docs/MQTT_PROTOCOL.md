# MQTT protocol

The namespace is `gps/<device_id>/...`. The firmware defaults to `device_1`; configure `DEVICE_ID` and use the same UID in Android/server registration. Protocol messages are JSON. GPIO/relay direction is configured separately from MQTT action names.

## Topics

| Suffix | Typical publisher → subscriber | Payload / behavior |
| --- | --- | --- |
| `location` | ESP32 or phone → server/app | Position, timestamp and optional telemetry |
| `heartbeat` | ESP32 → server/app | `device`, `uptime`, `gsm_signal`, `gps_status`, `mqtt_status`, `ip` |
| `status` | Compatible client → server/app | Optional online/battery/device status |
| `relay/command` | App/server → ESP32 | `action`: `start`, `kill` or `toggle` |
| `relay/status` | ESP32 → app/server | Relay/engine state or command error |
| `auth/req` | Server/app → ESP32 | `phone` for an SMS OTP request |
| `auth/code` | ESP32 → server/app | `phone`, `sms_status`; the code itself is sent by SMS |
| `auth/verify` | Server/app → ESP32 | `phone`, `code` entered by the user |
| `auth/result` | ESP32 → server/app | `status`: approved/rejected/error/timeout, plus context |
| `geofence/config` | Server/app → ESP32 | `geofences`, `alertPhone`, `globalSpeedLimit` |
| `geofence/alert` | Server → ESP32/app | `action`: enter/exit and nested `geofence` context |
| `sync/hello` | ESP32 → server/app | `oldestSeq`, `newestSeq`, `count` |
| `sync/ack` | Server/app → ESP32 | `lastKnownSeq` |
| `location/backlog` | ESP32 → server/app | `points` array containing sequence-stamped historical fixes |
| `ota/cmd` | Configured update controller → ESP32 | `cmd`: `update`, nonempty `version` and `url`, positive `size`, 64-character `sha256` |
| `ota/status` | ESP32 → monitoring client | Update status/progress/error fields |

Backend ingestion does not implement firmware distribution or OTA orchestration. Some subscriptions are compatibility hooks rather than proof that all producers emit that topic.

## Position example

These coordinates and values are illustrative, not exported tracking data.

```json
{
  "lat": 35.0,
  "lon": 51.0,
  "alt": 1000.0,
  "speed": 40.0,
  "bearing": 90.0,
  "accuracy": 1.2,
  "battery": 100,
  "network_type": "gsm",
  "satellites": 8,
  "timestamp": 1791504000,
  "session_id": "device_1"
}
```

The firmware uses `lon`; backlog/server compatibility paths also recognize `lng`. Speed is km/h and bearing is degrees. Firmware `accuracy` currently contains HDOP, while the server model describes meter accuracy. `battery` is currently fixed to 100 in firmware. When GPS time is unavailable, firmware sends uptime seconds; that value must not be treated as a valid Unix epoch by an integrator.

The backend trip/session field uses UUIDs. A firmware string UID such as `device_1` is not a valid trip UUID. Phone-origin telemetry may contain a generated UUID session and a real battery percentage.

## Backlog example

```json
{"oldestSeq": 101, "newestSeq": 110, "count": 10}
```

The server replies:

```json
{"lastKnownSeq": 100}
```

The device then publishes:

```json
{"points": [{"seq": 101, "lat": 35.0, "lon": 51.0, "speed": 40.0, "ts": 1791504000}]}
```

Confirmation is obtained through subsequent hello/ACK rounds. A successful MQTT publish call is not itself a database commit acknowledgement. Read the known persistence limits before relying on backlog recovery.

## Geofence configuration

```json
{
  "alertPhone": "",
  "globalSpeedLimit": 100,
  "geofences": [{
    "id": "example-fence",
    "lat": 35.0,
    "lng": 51.0,
    "radius": 200,
    "alertOnExit": true,
    "alertOnEnter": true,
    "exitMethod": "call",
    "enterMethod": "sms",
    "speedLimit": 60
  }]
}
```

Radius is meters, limits are km/h. Firmware truncates to its maximum local fence count. Inspect the Android/backend compatibility adapters when mapping stored entity IDs and booleans to this payload.

## Authorization, quality of service and retention

The provided restricted ACL separates device namespaces; it does not authenticate message origin within one shared device account. Do not allow arbitrary clients to publish `auth/result`, `relay/command`, `sync/ack` or `ota/cmd`. Device-generated messages have no cryptographic signature.

The firmware PubSubClient publisher and incoming subscriptions do not establish an exactly-once delivery guarantee. Backend publications use QoS in selected paths, but end-to-end durability still depends on the device publish behavior and database commit. Avoid retaining command/OTP/OTA payloads: reconnect should not replay an old actuator command or authentication operation. The project uses transient control messages rather than a documented retained-command contract.
