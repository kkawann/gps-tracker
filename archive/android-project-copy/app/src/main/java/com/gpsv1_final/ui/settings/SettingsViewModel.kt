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
    private val _serverUrl = MutableLiveData(prefs.getString("server_url", "http://gps.example.com") ?: "http://gps.example.com")
    val serverUrl: LiveData<String> = _serverUrl

    // MQTT Broker
    private val _brokerUrl = MutableLiveData(prefs.getString("broker", "tcp://gps.example.com:1883") ?: "tcp://gps.example.com:1883")
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
                    loadConfig()
                } else {
                    _toastMessage.postValue(result.optString("error", "خطا"))
                }
            } catch (e: Exception) {
                _toastMessage.postValue("خطا: ${e.message}")
            }
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
        val baseUrl = prefs.getString("server_url", "http://gps.example.com") ?: "http://gps.example.com"
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
        val baseUrl = prefs.getString("server_url", "http://gps.example.com") ?: "http://gps.example.com"
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
        val baseUrl = prefs.getString("server_url", "http://gps.example.com") ?: "http://gps.example.com"
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
