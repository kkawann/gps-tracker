package com.gpsv1_final.service

import android.app.Notification
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import com.gpsv1_final.App
import com.gpsv1_final.mqtt.MqttManager
import com.gpsv1_final.ui.MainActivity

/**
 * GpsForegroundService — نگه‌داشتن اتصال MQTT در پس‌زمینه
 * و نمایش موقعیت در notification
 */
class GpsForegroundService : Service() {

    private var mqttManager: MqttManager? = null
    private var reconnectHandler: android.os.Handler? = null
    private var reconnectRunnable: Runnable? = null
    private var isReconnecting = false
    private var listenersAdded = false
    private val RECONNECT_DELAY_MS = 5000L // 5 seconds

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()

        // Android 14 (API 34) requires location permissions before startForeground with type location
        if (!hasLocationPermission()) {
            Log.e(TAG, "Location permission not granted — stopping service")
            stopSelf()
            return
        }

        startForeground(NOTIFICATION_ID, buildNotification("در حال اتصال..."))
        connectMqtt()
    }

    private fun hasLocationPermission(): Boolean {
        return ContextCompat.checkSelfPermission(this, android.Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED ||
                ContextCompat.checkSelfPermission(this, android.Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED
    }

    private fun connectMqtt() {
        val prefs = getSharedPreferences("gps_prefs", Context.MODE_PRIVATE)
        val broker = prefs.getString("broker", "tcp://gps.example.com:1883") ?: return
        val deviceUid = prefs.getString("device_uid", "") ?: return
        val mqttUser = prefs.getString("mqtt_user", "") ?: ""
        val mqttPass = prefs.getString("mqtt_pass", "") ?: ""
        if (deviceUid.isBlank()) return

        val app = application as com.gpsv1_final.App

        // اگه MqttManager قبلاً ساخته شده و وصله، دوباره نساز
        if (app.mqttManager?.isConnected() == true) {
            mqttManager = app.mqttManager
            updateNotification("متصل - در حال ارسال GPS")
            startLocationUpdates()
            return
        }

        // اگه MqttManager قبلاً ساخته شده ولی قطعه، فقط reconnect کن
        if (mqttManager == null) {
            mqttManager = app.mqttManager ?: MqttManager(applicationContext)
            app.mqttManager = mqttManager
        }

        // listener ها فقط یکبار اضافه بشن (نه هر بار reconnect)
        if (!listenersAdded) {
            mqttManager!!.addOnLocationUpdateListener { lat, lng, speed, satellites, engine, timestamp, signalBars ->
                updateNotification("سرعت: ${speed.toInt()} km/h | موقعیت: ${String.format("%.4f", lat)}, ${String.format("%.4f", lng)}")
            }

            mqttManager!!.addOnConnectionChangedListener { connected ->
                updateNotification(
                    if (connected) "متصل به MQTT" else "قطع از MQTT"
                )
                if (!connected && !isReconnecting) {
                    startReconnectLoop()
                }
            }
            listenersAdded = true
        }

        mqttManager!!.connect(broker, deviceUid, mqttUser, mqttPass) { success ->
            if (success) {
                updateNotification("متصل - در حال ارسال GPS")
                startLocationUpdates()
                stopReconnectLoop()
            } else {
                updateNotification("خطا در اتصال MQTT - تلاش مجدد...")
                startReconnectLoop()
            }
        }
    }

    private fun startReconnectLoop() {
        if (isReconnecting) return
        isReconnecting = true

        reconnectHandler = android.os.Handler(android.os.Looper.getMainLooper())
        reconnectRunnable = object : Runnable {
            override fun run() {
                if (mqttManager?.isConnected() != true) {
                    Log.d(TAG, "Attempting MQTT reconnection...")
                    updateNotification("در حال تلاش برای اتصال مجدد...")
                    connectMqtt()
                    reconnectHandler?.postDelayed(this, RECONNECT_DELAY_MS)
                } else {
                    isReconnecting = false
                }
            }
        }
        reconnectHandler?.postDelayed(reconnectRunnable!!, RECONNECT_DELAY_MS)
    }

    private fun stopReconnectLoop() {
        isReconnecting = false
        reconnectRunnable?.let { reconnectHandler?.removeCallbacks(it) }
        reconnectHandler = null
        reconnectRunnable = null
    }

    // ─── Location Updates ──────────────────────────────────
    private var locationCallback: com.google.android.gms.location.LocationCallback? = null

    private fun startLocationUpdates() {
        val fusedClient = com.google.android.gms.location.LocationServices.getFusedLocationProviderClient(this)

        // حذف listener قبلی اگه وجود داره (جلوگیری از listener تکراری)
        locationCallback?.let { fusedClient.removeLocationUpdates(it) }

        val locationRequest = com.google.android.gms.location.LocationRequest.Builder(
            com.google.android.gms.location.Priority.PRIORITY_HIGH_ACCURACY, 5000L
        ).apply {
            setMinUpdateDistanceMeters(0f)           // ⚠ هر بار آپدیت بیاد ارسال شه (حتی اگه ساکنیم)
            setMinUpdateIntervalMillis(3000L)
            setGranularity(com.google.android.gms.location.Granularity.GRANULARITY_PERMISSION_LEVEL)
            setWaitForAccurateLocation(false)        // ⚠ بلافاصله آخرین موقعیت شناخته شده رو بفرست
            setMaxUpdateDelayMillis(10000L)          // حداکثر ۱۰ ثانیه تاخیر
        }.build()

        locationCallback = object : com.google.android.gms.location.LocationCallback() {
            override fun onLocationResult(result: com.google.android.gms.location.LocationResult) {
                val location = result.lastLocation ?: return
                publishLocation(location)
            }
        }

        try {
            fusedClient.requestLocationUpdates(locationRequest, locationCallback!!, android.os.Looper.getMainLooper())
            Log.d(TAG, "📍 Location updates started")
        } catch (e: SecurityException) {
            Log.e(TAG, "Location permission not granted: ${e.message}")
        }
    }

    private fun publishLocation(location: android.location.Location) {
        val prefs = getSharedPreferences("gps_prefs", Context.MODE_PRIVATE)
        val sessionId = prefs.getString("session_id", null) ?: run {
            val newId = java.util.UUID.randomUUID().toString()
            prefs.edit().putString("session_id", newId).apply()
            newId
        }

        val payload = org.json.JSONObject().apply {
            put("lat", location.latitude)
            put("lon", location.longitude)
            put("alt", location.altitude)
            put("speed", (location.speed * 3.6).toFloat()) // m/s → km/h
            put("bearing", location.bearing)
            put("accuracy", location.accuracy)
            put("battery", getBatteryLevel())
            put("network_type", "gps")
            put("timestamp", System.currentTimeMillis() / 1000.0)
            put("session_id", sessionId)
        }

        val topic = "gps/${prefs.getString("device_uid", "")}/location"
        mqttManager?.publishRaw(topic, payload.toString())
    }

    private fun getBatteryLevel(): Int {
        val batteryManager = getSystemService(Context.BATTERY_SERVICE) as android.os.BatteryManager
        return batteryManager.getIntProperty(android.os.BatteryManager.BATTERY_PROPERTY_CAPACITY)
    }

    private fun buildNotification(text: String): Notification {
        val intent = Intent(this, MainActivity::class.java)
        val pendingIntent = PendingIntent.getActivity(
            this, 0, intent, PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, App.CHANNEL_ID)
            .setContentTitle("GPS Tracker")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_menu_mylocation)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(text: String) {
        val notification = buildNotification(text)
        NotificationManagerCompat.from(this).notify(NOTIFICATION_ID, notification)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY
    }

    override fun onDestroy() {
        super.onDestroy()
        stopReconnectLoop()
        locationCallback?.let {
            try {
                com.google.android.gms.location.LocationServices
                    .getFusedLocationProviderClient(this)
                    .removeLocationUpdates(it)
            } catch (_: Exception) {}
        }
        mqttManager?.disconnect()
    }

    companion object {
        private const val TAG = "GpsForegroundService"
        private const val NOTIFICATION_ID = 1001
        private var isRunning = false

        fun start(context: Context) {
            if (isRunning) return
            isRunning = true
            val intent = Intent(context, GpsForegroundService::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun stop(context: Context) {
            isRunning = false
            context.stopService(Intent(context, GpsForegroundService::class.java))
        }
    }
}
