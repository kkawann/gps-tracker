# HTTP API routes

Generated from the actual route declarations in the active backend. Input/output schemas and JWT requirements are visible in `/docs`. MQTT device verification and direct MQTT control are separate from HTTP authorization.

| Method | Path | Handler |
| --- | --- | --- |
| GET | `/` | `main.py::root` |
| GET | `/health` | `main.py::health` |
| GET | `/api/admin/stats` | `routes_admin.py::admin_stats` |
| GET | `/api/admin/users` | `routes_admin.py::list_users` |
| POST | `/api/admin/users` | `routes_admin.py::create_user` |
| PUT | `/api/admin/users/{user_id}/toggle` | `routes_admin.py::toggle_user` |
| DELETE | `/api/admin/users/{user_id}` | `routes_admin.py::delete_user` |
| GET | `/api/admin/devices` | `routes_admin.py::list_all_devices` |
| DELETE | `/api/admin/devices/{device_id}` | `routes_admin.py::delete_device` |
| POST | `/api/auth/login` | `routes_auth.py::login` |
| POST | `/api/auth/request-otp` | `routes_auth.py::request_otp` |
| POST | `/api/auth/verify-otp` | `routes_auth.py::verify_otp` |
| POST | `/api/auth/logout` | `routes_auth.py::logout` |
| POST | `/api/auth/refresh` | `routes_auth.py::refresh_token` |
| GET | `/api/auth/check` | `routes_auth.py::auth_check` |
| GET | `/api/devices/{device_uid}/config` | `routes_devices.py::get_device_config` |
| PUT | `/api/devices/{device_uid}/config` | `routes_devices.py::update_device_config` |
| GET | `/api/devices/` | `routes_devices.py::list_devices` |
| POST | `/api/devices/` | `routes_devices.py::register_device` |
| GET | `/api/devices/{device_uid}/location/latest` | `routes_devices.py::get_latest_location` |
| GET | `/api/devices/{device_uid}/locations` | `routes_devices.py::get_location_history` |
| POST | `/api/devices/{device_uid}/relay/start` | `routes_devices.py::relay_start` |
| POST | `/api/devices/{device_uid}/relay/kill/request-otp` | `routes_devices.py::relay_kill_request_otp` |
| POST | `/api/devices/{device_uid}/relay/kill/verify` | `routes_devices.py::relay_kill_verify` |
| GET | `/api/geofence/{device_uid}` | `routes_geofence_app.py::list_geofences_mobile` |
| POST | `/api/geofence/{device_uid}` | `routes_geofence_app.py::create_geofence_mobile` |
| PUT | `/api/geofence/{device_uid}/{gf_id}` | `routes_geofence_app.py::update_geofence_mobile` |
| DELETE | `/api/geofence/{device_uid}/{gf_id}` | `routes_geofence_app.py::delete_geofence_mobile` |
| GET | `/api/geofences/` | `routes_geofences.py::list_geofences` |
| POST | `/api/geofences/` | `routes_geofences.py::create_geofence` |
| PUT | `/api/geofences/{geofence_id}` | `routes_geofences.py::update_geofence` |
| DELETE | `/api/geofences/{geofence_id}` | `routes_geofences.py::delete_geofence` |
| GET | `/api/gps/geofences` | `routes_gps_geofences.py::list_geofences_gps` |
| POST | `/api/gps/geofences` | `routes_gps_geofences.py::create_geofence_gps` |
| DELETE | `/api/gps/geofences/{gf_id}` | `routes_gps_geofences.py::delete_geofence_gps` |
| GET | `/api/trips/` | `routes_trips.py::list_trips` |
| GET | `/api/trips/{trip_id}` | `routes_trips.py::get_trip` |
| GET | `/api/trips/{trip_id}/route` | `routes_trips.py::get_trip_route` |
| POST | `/api/trips/{trip_id}/end` | `routes_trips.py::end_trip` |
