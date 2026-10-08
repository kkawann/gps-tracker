package com.gpsv1_final.ui.geofence

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.HttpURLConnection
import java.net.URL

/**
 * GeofenceViewModel — مدیریت جیوفنس هماهنگ با Node.js server.js
 *
 * API endpoints (server.js):
 *   GET    /api/geofence/:carId              → لیست حصارها
 *   POST   /api/geofence/:carId              → ایجاد حصار
 *   PUT    /api/geofence/:carId/:id          → ویرایش حصار
 *   DELETE /api/geofence/:carId/:id          → حذف حصار
 *
 * Field names match server.js:
 *   center: { lat, lng }, radius, name,
 *   alertOnExit, alertOnEnter, exitMethod, enterMethod, speedLimit
 */
class GeofenceViewModel(application: Application) : AndroidViewModel(application) {

    private val prefs = application.getSharedPreferences("gps_prefs", Context.MODE_PRIVATE)

    data class GeofenceItem(
        val id: String,
        val name: String,
        val centerLat: Double,
        val centerLng: Double,
        val radius: Double,
        val enabled: Boolean,
        val alertOnExit: Boolean,
        val alertOnEnter: Boolean,
        val exitMethod: String,
        val enterMethod: String,
        val speedLimit: Int
    )

    private val _geofences = MutableLiveData<List<GeofenceItem>>(emptyList())
    val geofences: LiveData<List<GeofenceItem>> = _geofences

    private val _loading = MutableLiveData(false)
    val loading: LiveData<Boolean> = _loading

    private val _toastMessage = MutableLiveData<String>()
    val toastMessage: LiveData<String> = _toastMessage

    // ─── Load geofences ────────────────────────────────────
    fun loadGeofences() {
        val sessionToken = prefs.getString("jwt_token", null) ?: return
        val deviceUid = prefs.getString("device_uid", null) ?: return

        _loading.value = true
        viewModelScope.launch {
            try {
                val arr = withContext(Dispatchers.IO) {
                    apiGet("/api/geofence/$deviceUid", sessionToken)
                }
                val list = mutableListOf<GeofenceItem>()
                for (i in 0 until arr.length()) {
                    val g = arr.getJSONObject(i)
                    val center = g.getJSONObject("center")
                    list.add(
                        GeofenceItem(
                            id = g.getString("id"),
                            name = g.getString("name"),
                            centerLat = center.getDouble("lat"),
                            centerLng = center.getDouble("lng"),
                            radius = g.getDouble("radius"),
                            enabled = g.optBoolean("enabled", true),
                            alertOnExit = g.optBoolean("alertOnExit", false),
                            alertOnEnter = g.optBoolean("alertOnEnter", false),
                            exitMethod = g.optString("exitMethod", "call"),
                            enterMethod = g.optString("enterMethod", "sms"),
                            speedLimit = g.optInt("speedLimit", 0)
                        )
                    )
                }
                _geofences.postValue(list)
            } catch (e: Exception) {
                _toastMessage.postValue("خطا: ${e.message}")
            }
            _loading.postValue(false)
        }
    }

    // ─── Create geofence ───────────────────────────────────
    fun createGeofence(
        name: String, lat: Double, lng: Double, radius: Int,
        alertOnExit: Boolean, alertOnEnter: Boolean,
        exitMethod: String, enterMethod: String, speedLimit: Int
    ) {
        val sessionToken = prefs.getString("jwt_token", null) ?: return
        val deviceUid = prefs.getString("device_uid", null) ?: return

        viewModelScope.launch {
            try {
                val body = JSONObject().apply {
                    put("name", name)
                    put("center", JSONObject().apply {
                        put("lat", lat)
                        put("lng", lng)
                    })
                    put("radius", radius)
                    put("alertOnExit", alertOnExit)
                    put("alertOnEnter", alertOnEnter)
                    put("exitMethod", exitMethod)
                    put("enterMethod", enterMethod)
                    put("speedLimit", speedLimit)
                }
                val result = withContext(Dispatchers.IO) {
                    apiPost("/api/geofence/$deviceUid", sessionToken, body)
                }
                if (result.optBoolean("success", false)) {
                    _toastMessage.postValue("حصار ایجاد شد")
                    loadGeofences()
                } else {
                    _toastMessage.postValue(result.optString("error", "خطا"))
                }
            } catch (e: Exception) {
                _toastMessage.postValue("خطا: ${e.message}")
            }
        }
    }

    // ─── Toggle enable/disable ─────────────────────────────
    fun toggleGeofence(geofence: GeofenceItem) {
        val sessionToken = prefs.getString("jwt_token", null) ?: return
        val deviceUid = prefs.getString("device_uid", null) ?: return

        viewModelScope.launch {
            try {
                val body = JSONObject().apply {
                    put("name", geofence.name)
                    put("enabled", !geofence.enabled)
                    put("alertOnExit", geofence.alertOnExit)
                    put("alertOnEnter", geofence.alertOnEnter)
                    put("exitMethod", geofence.exitMethod)
                    put("enterMethod", geofence.enterMethod)
                    put("speedLimit", geofence.speedLimit)
                }
                withContext(Dispatchers.IO) {
                    apiPut("/api/geofence/$deviceUid/${geofence.id}", sessionToken, body)
                }
                loadGeofences()
            } catch (e: Exception) {
                _toastMessage.postValue("خطا: ${e.message}")
            }
        }
    }

    // ─── Delete geofence ───────────────────────────────────
    fun deleteGeofence(id: String) {
        val sessionToken = prefs.getString("jwt_token", null) ?: return
        val deviceUid = prefs.getString("device_uid", null) ?: return

        viewModelScope.launch {
            try {
                withContext(Dispatchers.IO) {
                    apiDelete("/api/geofence/$deviceUid/$id", sessionToken)
                }
                _toastMessage.postValue("حصار حذف شد")
                loadGeofences()
            } catch (e: Exception) {
                _toastMessage.postValue("خطا: ${e.message}")
            }
        }
    }

    // ─── HTTP Helpers ──────────────────────────────────────

    private fun apiGet(path: String, sessionToken: String): JSONArray {
        val baseUrl = prefs.getString("server_url", "http://gps.example.com") ?: "http://gps.example.com"
        val url = URL("$baseUrl$path")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = "GET"
            setRequestProperty("Authorization", "Bearer $sessionToken")
            connectTimeout = 15000
            readTimeout = 30000
        }
        val code = conn.responseCode
        val stream = if (code in 200..299) conn.inputStream else conn.errorStream
        val response = BufferedReader(InputStreamReader(stream)).use { it.readText() }
        conn.disconnect()
        return JSONArray(response)
    }

    private fun apiPost(path: String, sessionToken: String, body: JSONObject): JSONObject {
        val baseUrl = prefs.getString("server_url", "http://gps.example.com") ?: "http://gps.example.com"
        val url = URL("$baseUrl$path")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            setRequestProperty("Content-Type", "application/json")
            setRequestProperty("Authorization", "Bearer $sessionToken")
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

    private fun apiPut(path: String, sessionToken: String, body: JSONObject): JSONObject {
        val baseUrl = prefs.getString("server_url", "http://gps.example.com") ?: "http://gps.example.com"
        val url = URL("$baseUrl$path")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = "PUT"
            setRequestProperty("Content-Type", "application/json")
            setRequestProperty("Authorization", "Bearer $sessionToken")
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

    private fun apiDelete(path: String, sessionToken: String) {
        val baseUrl = prefs.getString("server_url", "http://gps.example.com") ?: "http://gps.example.com"
        val url = URL("$baseUrl$path")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = "DELETE"
            setRequestProperty("Authorization", "Bearer $sessionToken")
            connectTimeout = 15000
            readTimeout = 30000
        }
        conn.responseCode
        conn.disconnect()
    }
}
