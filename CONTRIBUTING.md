# Contributing

Use the component guides to reproduce the firmware build, Android build or server setup. Describe the affected component, device ID/topic contract, expected behavior and observed behavior in an issue or pull request. Include redacted logs and the relevant firmware/app/server versions.

Keep credentials, phone numbers, location history, signing keys and production configuration outside Git. Use the provided example files for local settings. Preserve the meaning of existing MQTT fields or document a compatibility change in `docs/MQTT_PROTOCOL.md`.

For firmware changes, compile the ESP32-C3 environment and distinguish compiler checks from hardware tests. For Android changes, assemble a debug APK and describe the emulator/device used. For server changes, import the app, run relevant logic tests, and exercise changed API/MQTT behavior against an isolated database. Existing Unity firmware tests only check basic arithmetic and are not evidence of hardware correctness.

Use meaningful tests for new logic or fixes. Include the verification performed and any untested assumptions. Contributions to project-owned code are under the repository's MIT license; preserve third-party notices.
