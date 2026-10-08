# OTA implementation

`src/managers/OTAManager.cpp` implements updates over the same SIM800 TCP adapter used by MQTT. It temporarily disconnects MQTT while communicating with the HTTP firmware service, downloads byte ranges in segments, feeds the watchdog during waits, publishes progress when MQTT is restored, verifies a supplied SHA-256 hash and switches the OTA application slot through the ESP32 Update API.

Configuration comes from `include/config.h`: the default service port is 3000, latest-version path is `/api/firmware/latest`, base firmware path is `/firmware`, chunk size is 2048 bytes and the automatic check interval is six hours. The service host is a public example and must be configured locally.

The check request includes `device_id` and `current_version`. The parser expects a small JSON response with a known content length, up to 511 bytes in the existing implementation. The download service must support the HTTP response/header and Range behavior that the manager parses.

Commands on `gps/<device_id>/ota/cmd` must include `cmd: "update"`, a nonempty `version` different from the currently running version, `url`, a positive byte `size` and a 64-character `sha256`. Update progress/status is published on `ota/status`. Review the parser before using arbitrary URLs: the implementation is tailored to plain HTTP and the configured service, not a general HTTPS downloader.

NVS stores update metadata and offset so `checkPendingOta()` can attempt recovery after an interrupted update. This does not prove successful power-loss recovery in every stage; interruption, downloaded-prefix verification and rollback behavior require hardware tests.

The partition CSV provides two application slots of 1,703,936 bytes each. Reject binaries larger than a slot. A SHA-256 value can check transfer integrity only when the expected value is trustworthy. The current source has no signed-image trust chain or authenticated OTA command protocol.

The included FastAPI backup has no `/api/firmware/latest` route or firmware upload/distribution implementation. A separate controlled service is required; this repository preserves the device OTA client, not an absent backend service.
