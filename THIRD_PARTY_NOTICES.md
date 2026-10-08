# Third-party notices

The root MIT license applies to project-owned code and documentation. It does not replace dependency licenses or asset licenses.

## Bundled assets

| Asset | Upstream | License text included |
| --- | --- | --- |
| Vazirmatn font files | https://github.com/rastikerdar/vazirmatn | [SIL OFL 1.1](LICENSES/Vazirmatn-OFL.txt) |
| Inter font files | https://github.com/rsms/inter | [SIL OFL 1.1](LICENSES/Inter-OFL.txt) |
| Gradle wrapper JAR/scripts | https://github.com/gradle/gradle | [Apache-2.0](LICENSES/Gradle-Apache-2.0.txt), wrapper script headers retained |

Font files are redistributed unchanged, including their copies in the source archive. Map content comes from external OpenStreetMap providers and is not bundled in this repository. Retain map attribution in applications using those providers.

## External dependencies

Firmware dependencies are declared in `firmware/platformio.ini`: ArduinoJson, TinyGPSPlus and PubSubClient, plus the Espressif Arduino framework/toolchain. Android dependencies are declared in `android/app/build.gradle`: HiveMQ MQTT Client, OSMDroid, Room, AndroidX, Material Components and Google Play Services. Python dependencies are declared in `server/backend/requirements.txt`; container images are declared in `server/docker-compose.yml`.

Those packages are downloaded by their respective build tools and retain their upstream licenses. This repository does not vendor their complete source or claim to relicense them. The custom `SIM800_Arduino` metadata identifies its license as MIT; the local SIM800 transport sources are included as project code.
