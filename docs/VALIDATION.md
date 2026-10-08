# Publication validation

Validated on 2026-10-09 before the initial public source push. These checks use the sanitized/public source tree, with example development endpoints and no production credentials.

| Check | Result | Evidence / limits |
| --- | --- | --- |
| ESP32-C3 firmware build | **Passed** | `pio run -e nologo_esp32c3_super_mini` with Espressif32 6.10.0 / Arduino 2.0.17 |
| Firmware memory size | **Passed** | 24,604 bytes static RAM; 447,682 bytes program flash out of a 1,703,936-byte application slot |
| Android debug build | **Passed** | `gradlew.bat :app:assembleDebug --offline --no-daemon` using JDK 17 and the installed Android SDK |
| Python backend dependencies | **Passed** | Installed the supplied requirements in a separate Python 3.12 virtual environment |
| Backend import / ORM mappings | **Passed** | App and all registered routes imported; SQLAlchemy mappers configured |
| OpenAPI generation | **Passed** | 31 paths / 39 method declarations, with schemas generated successfully |
| Password/JWT round trips | **Passed** | Correct password accepted, incorrect password rejected; signed JWT decoded with the generated validation secret |
| Offline HTTP smoke checks | **Passed** | `/health` and `/openapi.json` returned 200; unauthenticated `/api/devices/` returned 403 through the existing HTTPBearer dependency |
| Compose YAML/static bindings | **Passed** | YAML parsed; database/Redis have no host port mappings and development HTTP/MQTT defaults bind to loopback |
| Private config initialization | **Passed** | Generates private random settings; a second invocation refuses to overwrite them |
| Android/archived XML syntax | **Passed** | 171 tracked XML files parsed |
| Dashboard JavaScript syntax | **Passed** | Inline script passed `node --check` |
| Python syntax | **Passed** | Active and archived Python sources parsed |
| Publication secret/file scan | **Passed** | Tracked source checked against original password/secret/host values, old seeded hashes and private-key/token patterns; private config, raw ZIP, logs, keys and runtime state are absent |
| Git whitespace / documentation links | **Passed** | Source diff checked; local documentation links resolve. Upstream license text preserves its original formatting. |
| Local live Docker startup | **Not run** | Docker CLI was not available on the publication computer; the workflow includes a live Compose/Nginx health check |
| Hardware, GSM, SMS, geofence and OTA end-to-end tests | **Not run** | No board/modem/phone/production network was operated for this publication |

The public repository includes [Build and validate](../.github/workflows/build.yml) for Linux firmware/Android builds, backend import checks and a live Docker Compose API health check. Its current outcome is shown on the repository's Actions page; local results above should not be read as a claim that a future CI run or hardware test passed.

The first local firmware attempt used a machine-specific unpinned PlatformIO installation and failed while installing packages on the system drive. The final passing build used the official pinned platform and a separate tool/cache location. The first Android attempt used an invalid machine-specific Java path; the final passing build used an existing JDK 17 and cached Gradle dependencies. No compiler code repairs were required beyond the documented publication/configuration changes.

Android compilation reported existing unused-parameter/expression warnings, and the SDK tools reported a metadata-version warning. Neither prevented the final debug build. These are not a runtime behavior assessment.

Read [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) for concrete behavior that these checks do not certify.
