package com.gpsv1_final.ui.settings

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.HttpURLConnection
import java.net.URL

/**
 * SettingsViewModel — تنظیمات دستگاه هماهنگ با server.js
 *
 * API endpoints:
 *   GET  /api/device/:carId/config    → دریافت تنظیمات
 *   PUT  /api/device/:carId/config    → ذخیره تنظیمات
 *   GET  /api/auth/check              → بررسی session
 *   POST /api/auth/logout             → خروج
 */
class SettingsViewModel(application: Application) : AndroidViewModel(application) {

    private val prefs = application.getSharedPreferences("gps_prefs", Context.MODE_PRIVATE)

    data class DeviceConfig(
        val customName: String,
        val ownerPhone: String,
        val authorizedPhones: List<String>,
        val speedLimit: Int,
        val isOwner: Boolean,
        val maxPhones: Int
    )

    private val _config = MutableLiveData<DeviceConfig?>()
    val config: LiveData<DeviceConfig?> = _config

    private val _loading = MutableLiveData(false)
    val loading: LiveData<Boolean> = _loading

    private val _toastMessage = MutableLiveData<String>()
    val toastMessage: LiveData<String> = _toastMessage

    private val _logoutEvent = MutableLiveData<Unit>()
    val logoutEvent: LiveData<Unit> = _logoutEvent

    // Server URL
    private val _serverUrl = MutableLiveData(prefs.getString("server_url", com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL) ?: com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL)
    val serverUrl: LiveData<String> = _serverUrl

    // MQTT Broker
    private val _brokerUrl = MutableLiveData(prefs.getString("broker", com.gpsv1_final.BuildConfig.DEFAULT_MQTT_BROKER) ?: com.gpsv1_final.BuildConfig.DEFAULT_MQTT_BROKER)
    val brokerUrl: LiveData<String> = _brokerUrl

    init {
        loadConfig()
    }

    // ─── Load device config ────────────────────────────────
    fun loadConfig() {
        val deviceId = prefs.getString("device_uid", null) ?: return
        val jwt = prefs.getString("jwt_token", null) ?: return

        _loading.value = true
        viewModelScope.launch {
            try {
                val result = withContext(Dispatchers.IO) {
                    apiGet("/api/devices/$deviceId/config", jwt)
                }
                _config.postValue(
                    DeviceConfig(
                        customName = result.optString("customName", "نامشخص"),
                        ownerPhone = result.optString("ownerPhone", ""),
                        authorizedPhones = run {
                            val arr = result.optJSONArray("authorizedPhones")
                            val list = mutableListOf<String>()
                            if (arr != null) {
                                for (i in 0 until arr.length()) list.add(arr.getString(i))
                            }
                            list
                        },
                        speedLimit = result.optInt("speedLimit", 0),
                        isOwner = result.optBoolean("isOwner", false),
                        maxPhones = result.optInt("maxPhones", 10)
                    )
                )
                // ⚠️ هماهنگی با kill-flow داشبورد: شماره مالک باید بعد از لود
                // config در prefs باشه تا publishAuthRequest خودِ دستگاه بتونه
                // ازش استفاده کنه
                val ownerPhone = result.optString("ownerPhone", "")
                if (ownerPhone.isNotBlank()) {
                    prefs.edit().putString("owner_phone", ownerPhone).apply()
                    // alertPhone (گیرنده هشدار جیوفنس روی ESP32) همگون با شماره مالک
                    prefs.edit().putString("alert_phone", ownerPhone).apply()
                }
                val speedLimit = result.optInt("speedLimit", 0)
                if (speedLimit > 0) {
                    prefs.edit().putInt("global_speed_limit", speedLimit).apply()
                }
            } catch (e: Exception) {
                _toastMessage.postValue("خطا در دریافت تنظیمات: ${e.message}")
            }
            _loading.postValue(false)
        }
    }

    // ─── Save custom name ──────────────────────────────────
    fun saveCustomName(name: String) {
        val deviceId = prefs.getString("device_uid", null) ?: return
        val jwt = prefs.getString("jwt_token", null) ?: return

        viewModelScope.launch {
            try {
                val body = JSONObject().apply { put("customName", name) }
                val result = withContext(Dispatchers.IO) {
                    apiPut("/api/devices/$deviceId/config", jwt, body)
                }
                if (result.optBoolean("success", false)) {
                    _toastMessage.postValue("نام ذخیره شد")
                    prefs.edit().putString("car_name", name).apply()
                    loadConfig()
                } else {
                    _toastMessage.postValue(result.optString("error", "خطا"))
                }
            } catch (e: Exception) {
                _toastMessage.postValue("خطا: ${e.message}")
            }
        }
    }

    // ─── Save speed limit ──────────────────────────────────
    fun saveSpeedLimit(limit: Int) {
        val deviceId = prefs.getString("device_uid", null) ?: return
        val jwt = prefs.getString("jwt_token", null) ?: return

        viewModelScope.launch {
            try {
                val body = JSONObject().apply { put("speedLimit", limit) }
                val result = withContext(Dispatchers.IO) {
                    apiPut("/api/devices/$deviceId/config", jwt, body)
                }
                if (result.optBoolean("success", false)) {
                    _toastMessage.postValue("محدودیت سرعت ذخیره شد")
                    // ⚠️ هماهنگ با فریمور: globalSpeedLimit مستقیم به ESP32 پوش بشه
                    // (فریمور اون رو از geofence/config می‌خونه)
                    prefs.edit().putInt("global_speed_limit", limit).apply()
                    publishGlobalSpeedToEsp32(limit)
                    loadConfig()
                } else {
                    _toastMessage.postValue(result.optString("error", "خطا"))
                }
            } catch (e: Exception) {
                _toastMessage.postValue("خطا: ${e.message}")
            }
        }
    }

    /**
     * ⚠️ هماهنگ با فریمور — سرعت سراسری از طریق تاپیک geofence/config به ESP32
     * پوش می‌شه. لیست حصارها از Room خونده می‌شه (کانفیگ فعلی دست‌نخورده می‌مونه).
     */
    private fun publishGlobalSpeedToEsp32(speedLimit: Int) {
        val mqtt = (getApplication() as? com.gpsv1_final.App)?.mqttManager ?: return
        if (!mqtt.isConnected()) return

        val deviceId = prefs.getString("device_uid", "") ?: return
        val alertPhone = prefs.getString("alert_phone", "") ?: ""
        val dao = (getApplication() as com.gpsv1_final.App).database.geofenceDao()

        viewModelScope.launch {
            val entities = withContext(Dispatchers.IO) { dao.getAllBlocking(deviceId) }
            val items = entities.map { e ->
                com.gpsv1_final.model.GeofenceItem(
                    id = e.id, serverId = e.server_id, name = e.name,
                    centerLat = e.lat, centerLng = e.lng, radius = e.radius,
                    enabled = e.is_active == 1,
                    alertOnExit = e.alert_on_exit == 1,
                    alertOnEnter = e.alert_on_enter == 1,
                    exitMethod = e.exit_method, enterMethod = e.enter_method,
                    speedLimit = e.speed_limit
                )
            }
            mqtt.publishGeofenceConfig(alertPhone, speedLimit, items)
        }
    }

    // ─── Save authorized phones ────────────────────────────
    fun saveAuthorizedPhones(phones: List<String>) {
        val deviceId = prefs.getString("device_uid", null) ?: return
        val jwt = prefs.getString("jwt_token", null) ?: return

        viewModelScope.launch {
            try {
                val arr = org.json.JSONArray(phones)
                val body = JSONObject().apply { put("authorizedPhones", arr) }
                val result = withContext(Dispatchers.IO) {
                    apiPut("/api/devices/$deviceId/config", jwt, body)
                }
                if (result.optBoolean("success", false)) {
                    _toastMessage.postValue("شماره‌ها ذخیره شدند")
                    loadConfig()
                } else {
                    _toastMessage.postValue(result.optString("error", "خطا"))
                }
            } catch (e: Exception) {
                _toastMessage.postValue("خطا: ${e.message}")
            }
        }
    }

    // ─── Save server/broker URLs ───────────────────────────
    fun saveServerUrl(url: String) {
        prefs.edit().putString("server_url", url).apply()
        _serverUrl.value = url
        _toastMessage.value = "آدرس سرور ذخیره شد"
    }

    fun saveBrokerUrl(url: String) {
        prefs.edit().putString("broker", url).apply()
        _brokerUrl.value = url
        _toastMessage.value = "آدرس broker ذخیره شد"
    }

    // ─── Logout ────────────────────────────────────────────
    fun logout() {
        val jwt = prefs.getString("jwt_token", null)
        if (jwt != null) {
            viewModelScope.launch {
                try {
                    withContext(Dispatchers.IO) {
                        apiPost("/api/auth/logout", jwt, JSONObject())
                    }
                } catch (_: Exception) {}
            }
        }
        prefs.edit().clear().apply()
        _logoutEvent.postValue(Unit)
    }

    // ─── HTTP Helpers ──────────────────────────────────────

    private fun apiGet(path: String, jwt: String): JSONObject {
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

    private fun apiPut(path: String, jwt: String, body: JSONObject): JSONObject {
        val baseUrl = prefs.getString("server_url", com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL) ?: com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL
        val url = URL("$baseUrl$path")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = "PUT"
            setRequestProperty("Content-Type", "application/json")
            setRequestProperty("Authorization", "Bearer $jwt")
            connectTimeout = 15000
            readTimeout = 30000
        }
        OutputStreamWriter(conn.outputStream).use { it.write(body.toString()) }
        val code = conn.responseCode
        val stream = if (code in 200..299) conn.inputStream else conn.errorStream
        val response = BufferedReader(InputStreamReader(stream)).use { it.readText() }
        conn.disconnect()
        return JSONObject(response)
    }

    private fun apiPost(path: String, jwt: String, body: JSONObject): JSONObject {
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
}
