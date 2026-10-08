package com.gpsv1_final.ui.dashboard

import android.app.Application
import android.content.Context
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import com.gpsv1_final.mqtt.MqttManager
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.HttpURLConnection
import java.net.URL
import java.text.SimpleDateFormat
import java.util.*
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.pow
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * DashboardViewModel — مدیریت داده زنده GPS و کنترل رله
 *
 * FastAPI endpoints:
 *   GET  /api/devices/:id/location/latest  → داده زنده GPS
 *   POST /api/devices/:id/relay/start      → روشن کردن موتور
 *   POST /api/devices/:id/relay/kill/request-otp  → درخواست OTP
 *   POST /api/devices/:id/relay/kill/verify       → تایید OTP
 */
class DashboardViewModel(application: Application) : AndroidViewModel(application) {

    private val prefs = application.getSharedPreferences("gps_prefs", Context.MODE_PRIVATE)

    // ─── Live Data ──────────────────────────────────────────
    private val _rawSpeed = MutableLiveData(0f)
    private val _speed = MutableLiveData(0f)
    val speed: LiveData<Float> = _speed

    private val _satellites = MutableLiveData(0)
    val satellites: LiveData<Int> = _satellites

    private val _signalBars = MutableLiveData(0)
    val signalBars: LiveData<Int> = _signalBars

    private val _engineOn = MutableLiveData(false)
    val engineOn: LiveData<Boolean> = _engineOn

    private val _lat = MutableLiveData(0.0)
    val lat: LiveData<Double> = _lat

    private val _lng = MutableLiveData(0.0)
    val lng: LiveData<Double> = _lng

    private val _lastUpdate = MutableLiveData("--:--")
    val lastUpdate: LiveData<String> = _lastUpdate

    private val _mqttConnected = MutableLiveData(false)
    val mqttConnected: LiveData<Boolean> = _mqttConnected

    // ─── Spike Filtering State ─────────────────────────────
    private var lastValidLat: Double = 0.0
    private var lastValidLng: Double = 0.0
    private var lastLocationTime: Long = 0L

    // ─── Speed Smoothing (Moving Average) ──────────────────
    private val speedBuffer = mutableListOf<Float>()
    private var speedBufferIndex = 0
    companion object {
        private const val MAX_LIVE_SPEED_KMH = 200.0
        private const val SPEED_SMOOTH_WINDOW = 5
    }

    // ─── Relay State ────────────────────────────────────────
    private val _relayBusy = MutableLiveData(false)
    val relayBusy: LiveData<Boolean> = _relayBusy

    private val _relayMessage = MutableLiveData<String>()
    val relayMessage: LiveData<String> = _relayMessage

    private val _needsOtpForKill = MutableLiveData(false)
    val needsOtpForKill: LiveData<Boolean> = _needsOtpForKill

    private val _toastMessage = MutableLiveData<String>()
    val toastMessage: LiveData<String> = _toastMessage

    // ─── MQTT Callbacks ────────────────────────────────────
    fun setupMqttCallbacks(mqtt: MqttManager?) {
        mqtt ?: return

        // بارگذاری آخرین لوکیشن ذخیره شده
        loadLastLocation()

        mqtt.onLocationUpdate = { lat, lng, speed, sat, engine, timestamp, signalBars ->
            // فیلتر اسپایک زنده
            var isSpike = false
            if (lastValidLat != 0.0 && lastValidLng != 0.0 && lastLocationTime > 0) {
                val dist = haversine(lastValidLat, lastValidLng, lat, lng)
                val timeDiff = (timestamp - lastLocationTime) / 1000.0
                if (timeDiff > 0) {
                    val impliedSpeed = (dist / timeDiff) * 3.6
                    if (impliedSpeed > MAX_LIVE_SPEED_KMH) {
                        isSpike = true // اسپایک - نادیده بگیر
                    }
                }
            }

            if (!isSpike) {
                lastValidLat = lat
                lastValidLng = lng
                lastLocationTime = timestamp

                // هموارسازی سرعت با میانگین متحرک
                _rawSpeed.postValue(speed)
                val smoothedSpeed = smoothSpeed(speed)

                _lat.postValue(lat)
                _lng.postValue(lng)
                _speed.postValue(smoothedSpeed)
                _satellites.postValue(sat)
                _signalBars.postValue(signalBars)
                // ⚠️ engine فقط از relay/status آپدیت شه — ESP32 توی location ارسال نمیکنه
                // _engineOn.postValue(engine) ❌ حذف شد — باعث overwrite false میشد

                // ذخیره آخرین لوکیشن
                saveLastLocation(lat, lng, timestamp)

                // آخرین آپدیت فقط وقتی داده قبول شد آپدیت شه (نه وقتی فیلتر شد)
                val sdf = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault())
                _lastUpdate.postValue(sdf.format(java.util.Date(timestamp)))
            }
        }

        mqtt.onConnectionChanged = { connected ->
            _mqttConnected.postValue(connected)
        }

        mqtt.onGeofenceAlert = { action, geofenceName, _ ->
            val msg = if (action == "exit") "خارج از $geofenceName" else "ورود به $geofenceName"
            _toastMessage.postValue("🚨 $msg")
        }

        mqtt.onRelayStatus = { status, action, engine ->
            _engineOn.postValue(engine)
            _relayMessage.postValue("وضعیت رله: $status ($action)")
            _relayBusy.postValue(false)
            Log.d("Dashboard", "🔌 Relay status received: engine=$engine")
        }

        // ⚠️ هماهنگ با فریمور: جواب verify OTP خود دستگاه (SMS-based) —
        // وقتی approved شد و کاربر در فاز kill بود، دستور kill برو
        mqtt.onAuthResult = { approved ->
            if (approved) {
                onAuthApproved(mqtt)
                _needsOtpForKill.postValue(false)
            } else {
                _toastMessage.postValue("کد تایید رد شد")
            }
        }

        // وضعیت فعلی اتصال رو sync کن (وقتی فرگمنت از paused برمیگرده)
        _mqttConnected.postValue(mqtt.isConnected())
    }

    // ─── Relay: Start Engine (بدون OTP) ────────────────────
    // ⚠️ هماهنگ با فریمور: دستور رله مستقیم روی MQTT می‌رود؛ فریمور جلسه‌ی
    // auth (OTP از خود دستگاه) را چک می‌کند. مسیر REST سرور مسیر موازی
    // ناهماهنگ بود — حذف شد.
    fun startEngine(mqtt: MqttManager?) {
        _relayBusy.value = true
        if (mqtt?.isConnected() == true) {
            mqtt.publishRelayCommand("start")
            _relayMessage.postValue("دستور روشن شدن ارسال شد")
            _relayBusy.value = false
        } else {
            _relayBusy.value = false
            _toastMessage.value = "MQTT متصل نیست"
        }
    }

    // ─── Relay: Kill (OTP از خود دستگاه — auth/req → SMS → auth/verify) ──
    fun requestKillOtp(mqtt: MqttManager?) {
        val ownerPhone = prefs.getString("owner_phone", "") ?: ""
        _relayBusy.value = true
        if (mqtt?.isConnected() != true) {
            _relayBusy.value = false
            _toastMessage.value = "MQTT متصل نیست"
            return
        }
        if (ownerPhone.isBlank()) {
            _relayBusy.value = false
            _toastMessage.value = "ابتدا شماره مالک را در تنظیمات ذخیره کنید"
            return
        }
        pendingKill = true
        mqtt.publishAuthRequest(ownerPhone)
        _needsOtpForKill.postValue(true)
        _relayMessage.postValue("کد تایید به شماره مالک پیامک شد")
        _relayBusy.value = false
    }

    fun verifyKillOtp(code: String, mqtt: MqttManager?) {
        val ownerPhone = prefs.getString("owner_phone", "") ?: ""
        if (mqtt?.isConnected() != true) {
            _toastMessage.value = "MQTT متصل نیست"
            return
        }
        pendingKill = false
        mqtt.publishAuthVerify(ownerPhone, code)
        _relayMessage.postValue("در حال بررسی کد...")
    }

    // ⚠️ اگر جلسه‌ی auth تایید شد و کاربر سکش AJAX (کد) را برای kill وارد کرده بود،
    // بعد از verify، دستور kill را بفرست
    private var pendingKill = false

    fun onAuthApproved(mqtt: MqttManager?) {
        if (pendingKill) {
            pendingKill = false
            if (mqtt?.isConnected() == true) {
                mqtt.publishRelayCommand("kill")
                _relayMessage.postValue("دستور خاموش شدن ارسال شد")
            }
        }
    }

    // ─── Cancel Kill OTP ────────────────────────────────────
    fun cancelKillOtp() {
        _needsOtpForKill.value = false
        pendingKill = false
    }

    // ─── Last Location Memory ──────────────────────────────
    // هموارسازی سرعت: میانگین متحرک از آخرین N نمونه
    private fun smoothSpeed(rawSpeed: Float): Float {
        if (speedBuffer.size < SPEED_SMOOTH_WINDOW) {
            speedBuffer.add(rawSpeed)
        } else {
            speedBuffer[speedBufferIndex % SPEED_SMOOTH_WINDOW] = rawSpeed
        }
        speedBufferIndex++
        return if (speedBuffer.isNotEmpty()) {
            speedBuffer.average().toFloat()
        } else {
            rawSpeed
        }
    }
    private fun saveLastLocation(lat: Double, lng: Double, timestamp: Long) {
        prefs.edit()
            .putFloat("last_lat", lat.toFloat())
            .putFloat("last_lng", lng.toFloat())
            .putLong("last_timestamp", timestamp)
            .apply()
    }

    fun loadLastLocation() {
        val lat = prefs.getFloat("last_lat", 0f).toDouble()
        val lng = prefs.getFloat("last_lng", 0f).toDouble()
        val timestamp = prefs.getLong("last_timestamp", 0)
        if (lat != 0.0 && lng != 0.0) {
            lastValidLat = lat
            lastValidLng = lng
            lastLocationTime = timestamp
            _lat.postValue(lat)
            _lng.postValue(lng)
        }
    }

    private fun haversine(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
        val r = 6371000.0
        val dLat = Math.toRadians(lat2 - lat1)
        val dLon = Math.toRadians(lon2 - lon1)
        val a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
                Math.cos(Math.toRadians(lat1)) * Math.cos(Math.toRadians(lat2)) *
                Math.sin(dLon / 2) * Math.sin(dLon / 2)
        val c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a))
        return r * c
    }

    // ─── HTTP Helper ────────────────────────────────────────
    private fun apiCallAuth(path: String, jwt: String, body: JSONObject): JSONObject {
        val baseUrl = prefs.getString("server_url", com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL) ?: com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL
        val url = URL("$baseUrl$path")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            setRequestProperty("Content-Type", "application/json")
            setRequestProperty("Authorization", "Bearer $jwt")
            connectTimeout = 15000
            readTimeout = 30000
        }

        if (body.length() > 0) {
            OutputStreamWriter(conn.outputStream).use { it.write(body.toString()) }
        }

        val code = conn.responseCode
        val stream = if (code in 200..299) conn.inputStream else conn.errorStream
        val response = BufferedReader(InputStreamReader(stream)).use { it.readText() }
        conn.disconnect()

        return JSONObject(response)
    }

    private fun apiGetObject(path: String, jwt: String): JSONObject {
        val baseUrl = prefs.getString("server_url", com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL) ?: com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL
        val url = URL("$baseUrl$path")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = "GET"
            setRequestProperty("Authorization", "Bearer $jwt")
            connectTimeout = 15000
            readTimeout = 30000
        }
        val code = conn.responseCode
        val stream = if (code in 200..299) conn.inputStream else conn.errorStream
        val response = BufferedReader(InputStreamReader(stream)).use { it.readText() }
        conn.disconnect()
        return JSONObject(response)
    }
}
