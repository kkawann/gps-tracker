# Source inventory

Active firmware, app and backend source inventory. Names are extracted from declarations; see component guides for runtime relationships.

## firmware/src

### `firmware/src/handlers/RelayHandler.cpp`

Declarations: `begin`, `turnOn`, `turnOff`, `toggle`, `handleCommand`, `saveState`, `loadState`.

### `firmware/src/handlers/RelayHandler.h`

Declarations: `RelayHandler`, `begin`, `turnOn`, `turnOff`, `toggle`, `handleCommand`, `getState`, `saveState`, `loadState`.

### `firmware/src/main.cpp`

Declarations: `updateConnectionStatusLed`, `updateSystemAlerts`, `onMqttMessage`, `publishSafe`, `publishGPS`, `publishHeartbeat`, `distanceMeters`, `handleGeofenceConfig`, `checkLocalGeofences`, `startCallAlert`, `updateCallAlertStateMachine`, `startAuthRequest`, `updateAuthStateMachine`, `handleAuthVerify`, `resetAuthState`, `updateSyncStateMachine`, `handleRelayCommand`, `isValidRelayAction`, `isAuthSessionValid`, `handleGeofenceAlert`, `startAlertBlink`, `updateAlertBlink`, `initWatchdog`, `initSim800WithRetry`, `connectGprsWithRetry`, `manageConnections`, `fatalRebootSequence`, `printBanner`, `printSystemStatus`, `setup`, `loop`.

### `firmware/src/managers/GPSManager.cpp`

Declarations: `begin`, `haversineMeters`, `passesQualityFilter`, `passesJumpFilter`, `applyEMA`, `loop`, `hasSignal`, `isValid`, `getUnixTimestamp`, `printStatus`.

### `firmware/src/managers/GPSManager.h`

Declarations: `GPSManager`, `passesJumpFilter`, `passesQualityFilter`, `applyEMA`, `begin`, `loop`, `hasSignal`, `isValid`, `getUnixTimestamp`, `printStatus`.

### `firmware/src/managers/LocationBuffer.cpp`

Declarations: `begin`, `allocSeq`, `addPoint`, `flushOldestToFlash`, `countFlashRecords`, `hasBacklog`, `pendingCount`, `oldestSeq`, `newestSeq`, `getNextBatch`, `purgeRamUpTo`, `compactFlashUpTo`, `confirmUpTo`.

### `firmware/src/managers/LocationBuffer.h`

Declarations: `LocationBuffer`, `allocSeq`, `flushOldestToFlash`, `countFlashRecords`, `compactFlashUpTo`, `purgeRamUpTo`, `begin`, `addPoint`, `hasBacklog`, `pendingCount`, `oldestSeq`, `newestSeq`, `getNextBatch`, `confirmUpTo`.

### `firmware/src/managers/MQTTManager.cpp`

Declarations: `begin`, `mqttCallback`, `loop`, `connect`, `isConnected`, `publish`, `subscribe`, `setMessageCallback`, `printStatus`.

### `firmware/src/managers/MQTTManager.h`

Declarations: `MQTTManager`, `begin`, `loop`, `connect`, `isConnected`, `publish`, `subscribe`, `setMessageCallback`, `printStatus`.

### `firmware/src/managers/OTAManager.cpp`

Declarations: `begin`, `loadMetadata`, `saveMetadata`, `saveOffset`, `clearMetadata`, `checkPendingOta`, `handleOtaCommand`, `checkForUpdate`, `loop`, `enterState`, `checkPrerequisites`, `sha256Init`, `sha256Update`, `sha256Final`, `doPrepare`, `computeSegmentBounds`, `advanceSegment`, `reportSegmentProgress`, `maybeSendProgressSms`, `buildUrl`, `sendHttpGet`, `parseHttpStatusLine`, `parseHttpHeaders`, `readHttpBodyChunk`, `doConnectHttp`, `doDisconnectHttp`, `doDownload`, `sha256ToHex`, `doVerify`, `doComplete`, `ensureMqttConnected`, `ensureMqttDisconnected`, `publishStatus`, `isInProgress`, `getProgress`, `abort`, `failOta`, `feedWatchdog`.

### `firmware/src/managers/OTAManager.h`

Declarations: `OTAManager`, `begin`, `loop`, `handleOtaCommand`, `checkForUpdate`, `isInProgress`, `getProgress`, `abort`, `checkPendingOta`, `loadMetadata`, `saveMetadata`, `saveOffset`, `clearMetadata`, `enterState`, `doPrepare`, `doConnectHttp`, `doDownload`, `doDisconnectHttp`, `doVerify`, `doComplete`, `computeSegmentBounds`, `advanceSegment`, `reportSegmentProgress`, `maybeSendProgressSms`, `sendHttpGet`, `parseHttpStatusLine`, `parseHttpHeaders`, `readHttpBodyChunk`, `buildUrl`, `publishStatus`, `ensureMqttConnected`, `ensureMqttDisconnected`, `checkPrerequisites`, `failOta`, `feedWatchdog`, `sha256Init`, `sha256Update`, `sha256Final`.

### `firmware/src/managers/SIM800Manager.cpp`

Declarations: `initCallback`, `gprsCallback`, `begin`, `loop`, `sendRawAT`, `isReady`, `isGprsConnected`, `getSignalQuality`, `connectGPRS`, `containsNonAscii`, `sendSMS`, `utf8ToUcs2Hex`, `waitForChar`, `sendUnicodeSMS`, `isSmsBusy`, `isModemResponding`, `isSimCardPresent`, `verifyGprsAlive`, `softReinit`, `hardReset`, `printStatus`.

### `firmware/src/managers/SIM800Manager.h`

Declarations: `SIM800Manager`, `begin`, `loop`, `sendRawAT`, `isReady`, `isGprsConnected`, `getSignalQuality`, `connectGPRS`, `sendSMS`, `sendUnicodeSMS`, `isSmsBusy`, `isModemResponding`, `isSimCardPresent`, `verifyGprsAlive`, `softReinit`, `hardReset`, `printStatus`, `pauseProcessing`, `containsNonAscii`, `utf8ToUcs2Hex`, `waitForChar`.

## firmware/lib

### `firmware/lib/SIM800_Arduino/SIM800_Arduino.cpp`

Declarations: `SIM800_Init`, `SIM800_Process`, `SIM800_CommandReady`, `SIM800_SetSmsCallback`, `SIM800_SetCallCallback`, `SIM800_SetInitCallback`, `SIM800_IsReady`, `SIM800_IsNetworkRegistered`, `SIM800_GetSignalStrength`, `SIM800_ForceReinit`, `SIM800_GetQueueSize`, `SIM800_GprsIsConnected`, `SIM800_SetGprsCallback`, `SIM800_GetTime`, `SIM800_GetDate`, `SIM800_TcpIsConnected`, `SIM800_SetTcpConnectCallback`, `SIM800_SetTcpDataCallback`, `SIM800_SetHttpCallback`.

### `firmware/lib/SIM800_Arduino/SIM800_Arduino.h`

Declarations: `SIM800_Init`, `SIM800_Process`, `SIM800_CommandReady`, `SIM800_IsReady`, `SIM800_IsNetworkRegistered`, `SIM800_GetSignalStrength`, `SIM800_ForceReinit`, `SIM800_GetQueueSize`, `SIM800_SetSmsCallback`, `SIM800_SetCallCallback`, `SIM800_SetInitCallback`, `SIM800_GetTime`, `SIM800_GetDate`, `SIM800_GprsIsConnected`, `SIM800_SetGprsCallback`, `SIM800_TcpIsConnected`, `SIM800_SetTcpConnectCallback`, `SIM800_SetTcpDataCallback`, `SIM800_SetHttpCallback`.

### `firmware/lib/SIM800Client/SIM800Client.cpp`

Declarations: `tcpConnectCallback`, `tcpDataCallback`, `getIpStackState`, `recoverDeadPdpContext`, `attemptCipStart`, `waitForSmsIdle`, `write`, `connect`, `available`, `read`, `peek`, `flush`, `stop`, `connected`, `setTimeout`, `loop`.

### `firmware/lib/SIM800Client/SIM800Client.h`

Declarations: `SIM800Client`, `attemptCipStart`, `getIpStackState`, `recoverDeadPdpContext`, `waitForSmsIdle`, `needsHardReset`, `clearNeedsHardReset`, `setTimeout`, `loop`.

## android/app/src/main/java

### `android/app/src/main/java/com/gpsv1_final/App.kt`

Declarations: `App`, `onCreate`, `createNotificationChannel`.

### `android/app/src/main/java/com/gpsv1_final/data/AppDatabase.kt`

Declarations: `AppDatabase`, `gpsPointDao`, `tripDao`, `geofenceDao`, `getInstance`.

### `android/app/src/main/java/com/gpsv1_final/data/GeofenceDao.kt`

Declarations: `insert`, `insertAll`, `delete`, `deleteAllForCar`, `getAll`, `getAllBlocking`, `getById`, `getByServerId`, `setActive`, `updateFull`, `setServerId`.

### `android/app/src/main/java/com/gpsv1_final/data/GpsPointDao.kt`

Declarations: `insert`, `insertAll`, `getLatest`, `getBetween`, `getRecent`, `deleteAll`.

### `android/app/src/main/java/com/gpsv1_final/data/TripDao.kt`

Declarations: `insert`, `getDays`, `getByDay`, `deleteAll`.

### `android/app/src/main/java/com/gpsv1_final/model/Geofence.kt`

Declarations: `Geofence`.

### `android/app/src/main/java/com/gpsv1_final/model/GeofenceItem.kt`

Declarations: `GeofenceItem`.

### `android/app/src/main/java/com/gpsv1_final/model/GpsPoint.kt`

Declarations: `GpsPoint`.

### `android/app/src/main/java/com/gpsv1_final/model/Trip.kt`

Declarations: `Trip`.

### `android/app/src/main/java/com/gpsv1_final/mqtt/MqttManager.kt`

Declarations: `MqttManager`, `addOnLocationUpdateListener`, `addOnConnectionChangedListener`, `addOnGeofenceAlertListener`, `BacklogPoint`, `connect`, `deliverResult`, `subscribeToTopics`, `publishAuthRequest`, `publishAuthVerify`, `publishRelayCommand`, `publishSyncAck`, `publishGeofenceConfig`, `publishRaw`, `publish`, `handleMessage`, `getSignalBars`, `disconnect`, `isConnected`, `getCarId`.

### `android/app/src/main/java/com/gpsv1_final/service/GpsForegroundService.kt`

Declarations: `GpsForegroundService`, `onBind`, `onCreate`, `hasLocationPermission`, `connectMqtt`, `startReconnectLoop`, `run`, `stopReconnectLoop`, `startLocationUpdates`, `onLocationResult`, `publishLocation`, `getBatteryLevel`, `buildNotification`, `updateNotification`, `onStartCommand`, `onDestroy`, `start`, `stop`.

### `android/app/src/main/java/com/gpsv1_final/ui/auth/AuthFragment.kt`

Declarations: `AuthFragment`, `onCreateView`, `onViewCreated`, `bindViews`, `playEntrance`, `setupListeners`, `observeViewModel`, `switchStep`, `showStepRequestOtp`, `showStepVerifyOtp`, `showStepRegister`, `updateStepIndicator`, `showLoading`, `connectMqttAndNavigate`, `navigateSafely`, `requestLocationPermissionAndStartService`, `onDestroyView`.

### `android/app/src/main/java/com/gpsv1_final/ui/auth/AuthViewModel.kt`

Declarations: `AuthViewModel`, `UiState`, `Success`, `getSavedSession`, `login`, `requestOtp`, `verifyOtp`, `registerDevice`, `logout`, `getBaseUrl`, `apiCall`, `apiCallAuth`.

### `android/app/src/main/java/com/gpsv1_final/ui/dashboard/DashboardFragment.kt`

Declarations: `DashboardFragment`, `onCreateView`, `onViewCreated`, `initOsmDroid`, `bindViews`, `setupMap`, `onScroll`, `onZoom`, `dropInMarker`, `glideMarkerTo`, `setupMqtt`, `setupRelay`, `observeViewModel`, `updateRelayToggleVisual`, `updateConnectionUI`, `updateMapPosition`, `placePulseRadar`, `repositionPulseRadar`, `animateKillOtp`, `onResume`, `onDestroyView`, `onPause`, `refreshDashboard`.

### `android/app/src/main/java/com/gpsv1_final/ui/dashboard/DashboardViewModel.kt`

Declarations: `DashboardViewModel`, `setupMqttCallbacks`, `startEngine`, `requestKillOtp`, `verifyKillOtp`, `onAuthApproved`, `cancelKillOtp`, `smoothSpeed`, `saveLastLocation`, `loadLastLocation`, `haversine`, `apiCallAuth`, `apiGetObject`.

### `android/app/src/main/java/com/gpsv1_final/ui/geofence/GeofenceFragment.kt`

Declarations: `GeofenceFragment`, `onCreateView`, `onViewCreated`, `singleTapConfirmedHelper`, `longPressHelper`, `observeViewModel`, `drawGeofencesOnMap`, `buildGeofenceChips`, `createCirclePolygon`, `saveGeofence`, `showGeofenceDialog`, `showActionDialog`, `showEditDialog`, `resetDialog`, `showDeleteDialog`, `onResume`, `onPause`.

### `android/app/src/main/java/com/gpsv1_final/ui/geofence/GeofenceViewModel.kt`

Declarations: `GeofenceViewModel`, `getCarId`, `getMqtt`, `loadGeofences`, `createGeofence`, `toggleGeofence`, `updateGeofence`, `deleteGeofence`, `publishToMqtt`, `apiGet`, `apiPostJson`, `apiDelete`.

### `android/app/src/main/java/com/gpsv1_final/ui/history/HistoryFragment.kt`

Declarations: `HistoryFragment`, `onCreateView`, `onViewCreated`, `buildDaySelector`, `getDayNameFa`, `styleSelected`, `styleUnselected`, `highlightButton`, `setupTimePickers`, `observeViewModel`, `drawSpeedColoredRoute`, `getSpeedColor`, `formatStopDuration`, `formatTimeAgo`, `animateStatsIn`, `animateStat`, `onResume`, `onPause`.

### `android/app/src/main/java/com/gpsv1_final/ui/history/HistoryViewModel.kt`

Declarations: `HistoryViewModel`, `HistoryPoint`, `DayStats`, `StopEvent`, `WeakArea`, `parseTimestamp`, `loadDayHistory`, `setTimeFilter`, `applyTimeRangeFilter`, `computeStats`, `formatDuration`, `detectStops`, `filterSpikes`, `detectWeakAreas`, `haversine`, `apiGetObject`, `refreshToken`.

### `android/app/src/main/java/com/gpsv1_final/ui/MainActivity.kt`

Declarations: `MainActivity`, `onCreate`, `animatePageChange`, `hideNavAnimated`, `showNavAnimated`.

### `android/app/src/main/java/com/gpsv1_final/ui/settings/SettingsFragment.kt`

Declarations: `SettingsFragment`, `onCreateView`, `onViewCreated`, `bindViews`, `setupListeners`, `observeViewModel`.

### `android/app/src/main/java/com/gpsv1_final/ui/settings/SettingsViewModel.kt`

Declarations: `SettingsViewModel`, `DeviceConfig`, `loadConfig`, `saveCustomName`, `saveSpeedLimit`, `publishGlobalSpeedToEsp32`, `saveAuthorizedPhones`, `saveServerUrl`, `saveBrokerUrl`, `logout`, `apiGet`, `apiPut`, `apiPost`.

### `android/app/src/main/java/com/gpsv1_final/ui/widgets/AnimatedCarMarker.kt`

Declarations: `updatePosition`, `dropInAnimation`, `smoothMoveTo`, `startGlow`, `stopGlow`.

### `android/app/src/main/java/com/gpsv1_final/ui/widgets/GlowDot.kt`

Declarations: `setColorRes`, `setGlowing`, `onDraw`, `onDetachedFromWindow`.

### `android/app/src/main/java/com/gpsv1_final/ui/widgets/PulseRadar.kt`

Declarations: `run`, `start`, `stop`, `onDraw`, `onDetachedFromWindow`.

### `android/app/src/main/java/com/gpsv1_final/ui/widgets/SparklineView.kt`

Declarations: `setCapacity`, `addSample`, `reset`, `onSizeChanged`, `onDraw`, `onDetachedFromWindow`.

### `android/app/src/main/java/com/gpsv1_final/ui/widgets/SpeedGauge.kt`

Declarations: `setSpeed`, `setMaxSpeed`, `animateTo`, `currentColor`, `blend`, `onDraw`, `onDetachedFromWindow`.

## server/backend

### `server/backend/auth.py`

Declarations: `hash_password`, `verify_password`, `create_access_token`, `get_current_user`, `require_admin`.

### `server/backend/config.py`

Declarations: `Settings`, `get_settings`.

### `server/backend/create_admin.py`

Declarations: `create`.

### `server/backend/database.py`

Declarations: `Base`, `init_db`, `get_db`.

### `server/backend/gps_filter.py`

Declarations: `haversine_distance`, `get_signal_bars`, `is_weak_signal_point`, `is_implausible_jump`, `ParkingDetector`, `filter_gps_outliers`, `simplify_history_points`.

### `server/backend/main.py`

Declarations: `lifespan`, `root`, `health`.

### `server/backend/models.py`

Declarations: `User`, `Device`, `GpsLocation`, `Trip`, `Geofence`, `Alert`.

### `server/backend/mqtt_handler.py`

Declarations: `MQTTHandler`.

### `server/backend/routes_admin.py`

Declarations: `admin_stats`, `list_users`, `create_user`, `toggle_user`, `delete_user`, `list_all_devices`, `delete_device`.

### `server/backend/routes_auth.py`

Declarations: `login`, `request_otp`, `verify_otp`, `logout`, `refresh_token`, `auth_check`.

### `server/backend/routes_devices.py`

Declarations: `ConfigUpdate`, `get_device_config`, `update_device_config`, `_get_device_or_404`, `list_devices`, `register_device`, `get_latest_location`, `get_location_history`, `relay_start`, `relay_kill_request_otp`, `relay_kill_verify`.

### `server/backend/routes_geofence_app.py`

Declarations: `MobileGeofenceCreate`, `MobileGeofenceUpdate`, `_gf_to_mobile`, `_find_user_device`, `_find_user_geofence`, `_push_config`, `list_geofences_mobile`, `create_geofence_mobile`, `update_geofence_mobile`, `delete_geofence_mobile`.

### `server/backend/routes_geofences.py`

Declarations: `_push_geofence_config_to_device`, `_get_device_uid`, `list_geofences`, `create_geofence`, `update_geofence`, `delete_geofence`.

### `server/backend/routes_gps_geofences.py`

Declarations: `GpsGeofenceCreate`, `_gf_to_gps`, `_get_device`, `_push_config`, `list_geofences_gps`, `create_geofence_gps`, `delete_geofence_gps`.

### `server/backend/routes_trips.py`

Declarations: `list_trips`, `get_trip`, `get_trip_route`, `end_trip`.

### `server/backend/schemas.py`

Declarations: `UserCreate`, `UserLogin`, `TokenResponse`, `OtpRequest`, `OtpVerify`, `CodeOnly`, `DeviceCreate`, `DeviceResponse`, `DeviceListResponse`, `LocationSubmit`, `LocationResponse`, `LocationHistory`, `TripResponse`, `TripListResponse`, `GeofenceCreate`, `GeofenceResponse`, `AlertResponse`, `AlertListResponse`, `DeviceStats`, `DailyStats`.
