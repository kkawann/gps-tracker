package com.gpsv1_final.ui.geofence

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.map
import androidx.lifecycle.viewModelScope
import com.gpsv1_final.App
import com.gpsv1_final.model.Geofence
import com.gpsv1_final.model.GeofenceItem
import com.gpsv1_final.mqtt.MqttManager
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
 * GeofenceViewModel — مدیریت جیوفنس: Room (local cache) + REST (server) + MQTT (push to ESP32)
 *
 * ⚠️ هماهنگ با سرور (cloud_server routes_gps.py):
 *   GET    /api/gps/geofences?device_uid={uid}   → {"ok":true,"geofences":[...]}
 *   POST   /api/gps/geofences                    → GeofenceCreate (latitude/longitude/radius_meters)
 *   DELETE /api/gps/geofences/{gf_id}
 *   POST   /api/gps/geofences/push-all/{uid}     → push همه به ESP32
 *   (سرور PUT ندارد — ویرایش = create با id جدید نیست؛ فیلد DELETE→CREATE.
 *    تا زمانی که PUT به سرور اضافه شود، ویرایش محلی + push مستقیم MQTT است.)
 */
class GeofenceViewModel(application: Application) : AndroidViewModel(application) {

    private val prefs = application.getSharedPreferences("gps_prefs", Context.MODE_PRIVATE)
    private val dao = (application as App).database.geofenceDao()
    private val app = application as App

    // LiveData from Room (source of truth for UI)
    val geofences: LiveData<List<GeofenceItem>> = dao.getAll(getCarId()).map { entities ->
        entities.map { e ->
            GeofenceItem(
                id = e.id,
                serverId = e.server_id,
                name = e.name,
                centerLat = e.lat,
                centerLng = e.lng,
                radius = e.radius,
                enabled = e.is_active == 1,
                alertOnExit = e.alert_on_exit == 1,
                alertOnEnter = e.alert_on_enter == 1,
                exitMethod = e.exit_method,
                enterMethod = e.enter_method,
                speedLimit = e.speed_limit
            )
        }
    }

    private val _loading = MutableLiveData(false)
    val loading: LiveData<Boolean> = _loading

    private val _toastMessage = MutableLiveData<String>()
    val toastMessage: LiveData<String> = _toastMessage

    private fun getCarId(): String = prefs.getString("device_uid", "") ?: ""

    private fun getMqtt(): MqttManager? = app.mqttManager

    // ─── Initial load: sync from server, then observe Room ──────────────────
    fun loadGeofences() {
        val sessionToken = prefs.getString("jwt_token", null) ?: return
        val deviceUid = getCarId()
        if (deviceUid.isBlank()) return

        _loading.value = true
        viewModelScope.launch {
            try {
                val result = withContext(Dispatchers.IO) {
                    apiGet("/api/gps/geofences?device_uid=$deviceUid", sessionToken)
                }
                val arr = result.optJSONArray("geofences")
                val entities = mutableListOf<Geofence>()
                if (arr != null) {
                    for (i in 0 until arr.length()) {
                        val g = arr.getJSONObject(i)
                        entities.add(
                            Geofence(
                                id = 0, // auto-generate
                                car_id = deviceUid,
                                server_id = g.getString("id"),
                                name = g.getString("name"),
                                lat = g.getDouble("latitude"),
                                lng = g.getDouble("longitude"),
                                radius = g.getDouble("radius_meters").toFloat(),
                                is_active = if (g.optBoolean("is_active", true)) 1 else 0,
                                alert_on_exit = if (g.optBoolean("alert_on_exit", false)) 1 else 0,
                                alert_on_enter = if (g.optBoolean("alert_on_enter", false)) 1 else 0,
                                exit_method = g.optString("exit_method", "call"),
                                enter_method = g.optString("enter_method", "sms"),
                                speed_limit = g.optInt("speed_limit", 0)
                            )
                        )
                    }
                }
                withContext(Dispatchers.IO) {
                    // ⚠️ فیکس - ادغام به‌جای جایگزینی: حصارهای محلی بدون server_id
                    // (که هنوز روی سرور sync نشده‌اند / create شکست خورده) نباید با
                    // لود مجدد پاک شوند — قبلاً deleteAllForCar همه را می‌کشت و
                    // حصار بعد از خروج/ورود به فرگمنت ناپدید می‌شد.
                    val localNames = entities.map { it.name }.toSet()
                    val locals = dao.getAllBlocking(deviceUid)
                    val orphans = locals.filter { it.server_id.isBlank() && it.name !in localNames }
                    dao.deleteAllForCar(deviceUid)
                    dao.insertAll(entities)
                    dao.insertAll(orphans)
                }
                _toastMessage.postValue("حصارها بروز شدند (${entities.size})")
            } catch (e: Exception) {
                _toastMessage.postValue("خطا: ${e.message}")
            }
            _loading.postValue(false)
        }
    }

    // ─── Create geofence (server + local + ESP32) ──────────
    fun createGeofence(
        name: String, lat: Double, lng: Double, radius: Int,
        alertOnExit: Boolean, alertOnEnter: Boolean,
        exitMethod: String, enterMethod: String, speedLimit: Int
    ) {
        val sessionToken = prefs.getString("jwt_token", null) ?: return
        val deviceUid = getCarId()
        if (deviceUid.isBlank()) return

        viewModelScope.launch {
            try {
                val body = JSONObject().apply {
                    put("name", name)
                    put("latitude", lat)
                    put("longitude", lng)
                    put("radius_meters", radius)
                    put("device_uid", deviceUid)
                    put("alertOnExit", alertOnExit)
                    put("alertOnEnter", alertOnEnter)
                    put("exit_method", exitMethod)
                    put("enter_method", enterMethod)
                    put("speed_limit", speedLimit)
                    put("is_active", true)
                }
                val gf = withContext(Dispatchers.IO) {
                    apiPostJson("/api/gps/geofences", sessionToken, body)
                }
                val newServerId = gf.optString("id", "")
                withContext(Dispatchers.IO) {
                    val newLocalId = dao.insert(
                        Geofence(
                            id = 0,
                            car_id = deviceUid,
                            server_id = newServerId,
                            name = name,
                            lat = lat,
                            lng = lng,
                            radius = radius.toFloat(),
                            is_active = 1,
                            alert_on_exit = if (alertOnExit) 1 else 0,
                            alert_on_enter = if (alertOnEnter) 1 else 0,
                            exit_method = exitMethod,
                            enter_method = enterMethod,
                            speed_limit = speedLimit
                        )
                    )
                    if (newServerId.isNotBlank()) dao.setServerId(newLocalId, newServerId)
                }
                _toastMessage.postValue("حصار ایجاد شد")
                publishToMqtt()
            } catch (e: Exception) {
                _toastMessage.postValue("خطا: ${e.message}")
            }
        }
    }

    // ─── Toggle enable/disable (محلی + ESP32؛ سرور PUT ندارد) ──
    fun toggleGeofence(item: GeofenceItem) {
        val deviceUid = getCarId()
        if (deviceUid.isBlank()) return

        val newEnabled = !item.enabled
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                dao.setActive(item.id, if (newEnabled) 1 else 0)
            }
            _toastMessage.postValue(if (newEnabled) "حصار فعال شد" else "حصار غیرفعال شد")
            publishToMqtt()
        }
    }

    // ─── Update geofence (full) — محلی + ESP32 ─────────────
    // ⚠️ سرور endpoint PUT ندارد — وقتی اضافه شد، syncToServer(this) اینجا کافی است
    fun updateGeofence(
        item: GeofenceItem,
        name: String, lat: Double, lng: Double, radius: Int,
        alertOnExit: Boolean, alertOnEnter: Boolean,
        exitMethod: String, enterMethod: String, speedLimit: Int
    ) {
        val deviceUid = getCarId()
        if (deviceUid.isBlank()) return

        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                dao.updateFull(
                    id = item.id,
                    name = name,
                    lat = lat,
                    lng = lng,
                    radius = radius.toFloat(),
                    alertOnExit = if (alertOnExit) 1 else 0,
                    alertOnEnter = if (alertOnEnter) 1 else 0,
                    exitMethod = exitMethod,
                    enterMethod = enterMethod,
                    speedLimit = speedLimit,
                    enabled = if (item.enabled) 1 else 0
                )
            }
            _toastMessage.postValue("حصار به‌روزرسانی شد")
            publishToMqtt()
        }
    }

    // ─── Delete geofence ───────────────────────────────────
    fun deleteGeofence(item: GeofenceItem) {
        val sessionToken = prefs.getString("jwt_token", null) ?: return
        val deviceUid = getCarId()
        if (deviceUid.isBlank()) return

        viewModelScope.launch {
            // حذف محلی اول (UI فوراً پاک شود)، بعد سرور
            withContext(Dispatchers.IO) {
                dao.delete(Geofence(id = item.id, car_id = deviceUid, name = "", lat = 0.0, lng = 0.0, radius = 0f))
            }
            _toastMessage.postValue("حصار حذف شد")
            publishToMqtt()

            if (item.serverId.isNotBlank()) {
                try {
                    withContext(Dispatchers.IO) {
                        apiDelete("/api/gps/geofences/${item.serverId}", sessionToken)
                    }
                } catch (e: Exception) {
                    _toastMessage.postValue("حذف از سرور ناموفق: ${e.message}")
                }
            }
        }
    }

    // ─── Publish current geofences to ESP32 via MQTT ─────────
    private fun publishToMqtt() {
        val mqtt = getMqtt() ?: return
        if (!mqtt.isConnected()) return

        val deviceUid = getCarId()
        if (deviceUid.isBlank()) return

        val alertPhone = prefs.getString("alert_phone", "") ?: ""
        val globalSpeedLimit = prefs.getInt("global_speed_limit", 0)

        // خواندن مستقیم از Room — LiveData.value موقع صدا زدن null است
        viewModelScope.launch {
            val entities = withContext(Dispatchers.IO) { dao.getAllBlocking(deviceUid) }
            val items = entities.map { e ->
                GeofenceItem(
                    id = e.id,
                    serverId = e.server_id,
                    name = e.name,
                    centerLat = e.lat,
                    centerLng = e.lng,
                    radius = e.radius,
                    enabled = e.is_active == 1,
                    alertOnExit = e.alert_on_exit == 1,
                    alertOnEnter = e.alert_on_enter == 1,
                    exitMethod = e.exit_method,
                    enterMethod = e.enter_method,
                    speedLimit = e.speed_limit
                )
            }
            mqtt.publishGeofenceConfig(alertPhone, globalSpeedLimit, items)
        }
    }

    // ─── HTTP Helpers ──────────────────────────────────────

    private fun apiGet(path: String, sessionToken: String): JSONObject {
        val baseUrl = prefs.getString("server_url", com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL) ?: com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL
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
        return JSONObject(response)
    }

    private fun apiPostJson(path: String, sessionToken: String, body: JSONObject): JSONObject {
        val baseUrl = prefs.getString("server_url", com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL) ?: com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL
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

    private fun apiDelete(path: String, sessionToken: String) {
        val baseUrl = prefs.getString("server_url", com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL) ?: com.gpsv1_final.BuildConfig.DEFAULT_SERVER_URL
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
