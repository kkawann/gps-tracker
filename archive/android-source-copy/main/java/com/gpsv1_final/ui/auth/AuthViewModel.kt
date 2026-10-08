package com.gpsv1_final.ui.auth

import android.app.Application
import android.content.Context
import android.util.Log
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
 * AuthViewModel — لاگین هماهنگ با FastAPI backend
 *
 * Flow:
 *   1) login(username, password) → POST /api/auth/login → JWT token
 *   2) requestOtp(deviceUid, phone) → POST /api/auth/request-otp → SMS ارسال
 *   3) verifyOtp(deviceUid, code) → POST /api/auth/verify-otp → JWT token
 *   4) registerDevice(deviceUid) → POST /api/devices/ → device info
 */
class AuthViewModel(application: Application) : AndroidViewModel(application) {

    private val prefs = application.getSharedPreferences("gps_prefs", Context.MODE_PRIVATE)

    private val _uiState = MutableLiveData<UiState>(UiState.LoginEntry)
    val uiState: LiveData<UiState> = _uiState

    private val _toastMessage = MutableLiveData<String>()
    val toastMessage: LiveData<String> = _toastMessage

    sealed class UiState {
        object LoginEntry : UiState()
        object OtpSent : UiState()
        object DeviceEntry : UiState()
        object Loading : UiState()
        data class Success(val deviceUid: String, val deviceName: String) : UiState()
    }

    fun getSavedSession(): Pair<String, String>? {
        val deviceUid = prefs.getString("device_uid", null)
        val jwt = prefs.getString("jwt_token", null)
        if (!deviceUid.isNullOrBlank() && !jwt.isNullOrBlank()) {
            return Pair(deviceUid, jwt)
        }
        return null
    }

    // ─── Step 1a: Login with username/password ────────────
    fun login(username: String, password: String) {
        if (username.isBlank() || password.isBlank()) {
            _toastMessage.value = "نام کاربری و رمز عبور را وارد کنید"
            return
        }

        _uiState.value = UiState.Loading
        viewModelScope.launch {
            try {
                val result = withContext(Dispatchers.IO) {
                    apiCall("/api/auth/login", JSONObject().apply {
                        put("username", username)
                        put("password", password)
                    })
                }

                if (result.has("access_token")) {
                    val token = result.getString("access_token")
                    val userId = result.optString("user_id", "")
                    prefs.edit()
                        .putString("jwt_token", token)
                        .putString("user_id", userId)
                        .putString("server_url", getBaseUrl())
                        .apply()
                    Log.d(TAG, "Login OK — username:$username")
                    _uiState.value = UiState.DeviceEntry
                } else {
                    _toastMessage.value = result.optString("detail", "خطا در ورود")
                    _uiState.value = UiState.LoginEntry
                }
            } catch (e: Exception) {
                _toastMessage.value = "خطا در اتصال: ${e.message}"
                _uiState.value = UiState.LoginEntry
            }
        }
    }

    // ─── Step 1b: Request OTP via SMS ─────────────────────
    fun requestOtp(deviceUid: String, phone: String) {
        if (deviceUid.isBlank() || phone.isBlank()) {
            _toastMessage.value = "شناسه دستگاه و شماره تلفن را وارد کنید"
            return
        }

        _uiState.value = UiState.Loading
        viewModelScope.launch {
            try {
                val result = withContext(Dispatchers.IO) {
                    apiCall("/api/auth/request-otp", JSONObject().apply {
                        put("device_uid", deviceUid)
                        put("phone", phone)
                    })
                }

                if (result.optBoolean("success", false)) {
                    prefs.edit().putString("device_uid", deviceUid).apply()
                    _toastMessage.value = "کد تایید ارسال شد"
                    _uiState.value = UiState.OtpSent
                } else {
                    _toastMessage.value = result.optString("detail", "خطا در ارسال کد")
                    _uiState.value = UiState.LoginEntry
                }
            } catch (e: Exception) {
                _toastMessage.value = "خطا: ${e.message}"
                _uiState.value = UiState.LoginEntry
            }
        }
    }

    // ─── Step 1c: Verify OTP ──────────────────────────────
    fun verifyOtp(code: String) {
        val deviceUid = prefs.getString("device_uid", null) ?: return
        if (code.isBlank()) {
            _toastMessage.value = "کد تایید را وارد کنید"
            return
        }

        _uiState.value = UiState.Loading
        viewModelScope.launch {
            try {
                val result = withContext(Dispatchers.IO) {
                    apiCall("/api/auth/verify-otp", JSONObject().apply {
                        put("device_uid", deviceUid)
                        put("code", code)
                    })
                }

                if (result.has("access_token")) {
                    val token = result.getString("access_token")
                    val userId = result.optString("user_id", "")
                    prefs.edit()
                        .putString("jwt_token", token)
                        .putString("user_id", userId)
                        .putString("server_url", getBaseUrl())
                        .apply()
                    Log.d(TAG, "OTP verified OK — device:$deviceUid")
                    _uiState.value = UiState.DeviceEntry
                } else {
                    _toastMessage.value = result.optString("detail", "کد اشتباه است")
                    _uiState.value = UiState.OtpSent
                }
            } catch (e: Exception) {
                _toastMessage.value = "خطا: ${e.message}"
                _uiState.value = UiState.OtpSent
            }
        }
    }

    // ─── Step 2: Register device ──────────────────────────
    fun registerDevice(deviceName: String, deviceUid: String) {
        if (deviceName.isBlank() || deviceUid.isBlank()) {
            _toastMessage.value = "نام و شناسه دستگاه را وارد کنید"
            return
        }

        val jwt = prefs.getString("jwt_token", null) ?: return

        _uiState.value = UiState.Loading
        viewModelScope.launch {
            try {
                val result = withContext(Dispatchers.IO) {
                    apiCallAuth("/api/devices/", jwt, JSONObject().apply {
                        put("device_uid", deviceUid)
                        put("name", deviceName)
                    })
                }

                val mqttTopic = result.optString("mqtt_topic", "gps/$deviceUid")
                val savedDeviceUid = result.optString("device_uid", deviceUid)
                val deviceId = result.optString("id", "")

                prefs.edit()
                    .putString("device_uid", savedDeviceUid)
                    .putString("device_name", deviceName)
                    .putString("mqtt_topic", mqttTopic)
                    .putString("device_id", deviceId)
                    .apply()

                Log.d(TAG, "Device registered: $savedDeviceUid")
                _uiState.value = UiState.Success(savedDeviceUid, deviceName)
            } catch (e: Exception) {
                if (e.message?.contains("409") == true || e.message?.contains("already") == true) {
                    prefs.edit()
                        .putString("device_uid", deviceUid)
                        .putString("device_name", deviceName)
                        .putString("mqtt_topic", "gps/$deviceUid")
                        .apply()
                    _uiState.value = UiState.Success(deviceUid, deviceName)
                } else {
                    _toastMessage.value = "خطا: ${e.message}"
                    _uiState.value = UiState.DeviceEntry
                }
            }
        }
    }

    // ─── Logout ────────────────────────────────────────────
    fun logout() {
        prefs.edit().clear().apply()
        _uiState.value = UiState.LoginEntry
    }

    // ─── HTTP Helpers ──────────────────────────────────────
    private fun getBaseUrl(): String {
        return prefs.getString("server_url", null) ?: "http://gps.example.com"
    }

    private fun apiCall(path: String, body: JSONObject): JSONObject {
        val baseUrl = getBaseUrl()
        val url = URL("$baseUrl$path")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            setRequestProperty("Content-Type", "application/json")
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

    private fun apiCallAuth(path: String, jwt: String, body: JSONObject): JSONObject {
        val baseUrl = getBaseUrl()
        val url = URL("$baseUrl$path")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
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

    companion object {
        private const val TAG = "AuthViewModel"
    }
}
