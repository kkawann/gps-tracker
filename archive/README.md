# Earlier source variants

The input included nested copies of the Android project and source tree. Their source/assets are retained here for comparison; they are excluded from the active Android build.

| Directory/file | Original location | Use |
| --- | --- | --- |
| `android-project-copy/` | `gpsv1_final/android/android/` | Earlier complete nested Android project |
| `android-source-copy/` | `gpsv1_final/android/app/src/src/` | Earlier nested app source/resources |
| `server-runtime-backend/` | Backup's `running-code/backend/` | Only the four Python files differing from the on-disk version |
| `legacy-init.sql` | Backup's `gps-server/scripts/init.sql` | Historical PostgreSQL schema, with default-user seed removed |

The active server sources come from `gps-server/backend/` in the backup. Do not replace them with these older files: the current versions include ownership checks and geofence deletion fixes.

The historical SQL schema lacks fields used by the current models, so the active Compose stack uses SQLAlchemy model metadata to initialize a fresh database. Neither method is an existing-database migration system.

Production credentials, IP addresses, private keys, logs and generated files were not carried into the archive. Android archived sources use example endpoints. They are reference artifacts, not supported build targets.
