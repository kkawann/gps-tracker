package com.gpsv1_final.ui.history

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
import java.net.HttpURLConnection
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Locale
import java.util.TimeZone
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.pow
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * HistoryViewModel — تاریخچه سفر با فیلتر اسپایک و نمایش ناحیه‌ای
 */
class HistoryViewModel(application: Application) : AndroidViewModel(application) {

    private val prefs = application.getSharedPreferences("gps_prefs", Context.MODE_PRIVATE)

    data class HistoryPoint(
        val lat: Double, val lng: Double, val speed: Float,
        val timestamp: Long, val satellites: Int = 0, val accuracy: Float = 0f
    )

    data class DayStats(
        val distance: String, val avgSpeed: String, val maxSpeed: Int,
        val movingMin: Int, val stoppedMin: Int, val pointCount: Int,
        val peakSpeedTime: String = ""
    )

    // ناحیه ضعیف سیگنال (وقتی نقاط در یک شعاع کوچک پراکنده‌ان)
    data class WeakArea(
        val centerLat: Double, val centerLng: Double,
        val radiusMeters: Double, val pointCount: Int,
        val startTime: Long, val endTime: Long
    )

    private val _points = MutableLiveData<List<HistoryPoint>>(emptyList())
    val points: LiveData<List<HistoryPoint>> = _points

    private val _weakAreas = MutableLiveData<List<WeakArea>>(emptyList())
    val weakAreas: LiveData<List<WeakArea>> = _weakAreas

    private val _stats = MutableLiveData<DayStats?>()
    val stats: LiveData<DayStats?> = _stats

    private val _loading = MutableLiveData(false)
    val loading: LiveData<Boolean> = _loading

    private val _error = MutableLiveData<String?>()
    val error: LiveData<String?> = _error

    // ─── Time Range Filter State ───────────────────────────
    private var allFilteredPoints: List<HistoryPoint> = emptyList()
    private var timeStartHour: Int = 0
    private var timeStartMinute: Int = 0
    private var timeEndHour: Int = 23
    private var timeEndMinute: Int = 59
    private var isTimeFilterActive: Boolean = false

    private val isoFormat = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", Locale.US).apply {
        timeZone = TimeZone.getTimeZone("UTC")
    }
    private val dayFormat = SimpleDateFormat("yyyy-MM-dd", Locale.US)

    companion object {
        // حداکثر سرعت مجاز بین دو نقطه (km/h) - بالاتر از این مقدار = اسپایک
        private const val MAX_VALID_SPEED_KMH = 200.0
        // حداکثر دقت GPS برای نقاط معتبر (متر) - GSM معمولاً 250-500m
        private const val MAX_ACCURACY_METERS = 1000f
        // شعاع ناحیه ضعیف (متر) - نقاط در این شعاع = یک ناحیه
        private const val WEAK_AREA_RADIUS_METERS = 50.0
        // حداقل تعداد نقاط برای تشکیل ناحیه ضعیف
        private const val MIN_POINTS_FOR_WEAK_AREA = 3
        // حداکثر تفاوت زمانی برای ناحیه ضعیف (ثانیه)
        private const val MAX_WEAK_AREA_SPAN_SEC = 300L
    }

    private fun parseTimestamp(value: Any?): Long {
        return when (value) {
            is Long -> if (value < 1_000_000_0000L) value * 1000 else value
            is Number -> {
                val v = value.toLong()
                if (v < 1_000_000_0000L) v * 1000 else v
            }
            is String -> {
                try {
                    isoFormat.parse(value)?.time ?: 0L
                } catch (_: Exception) {
                    try {
                        SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", Locale.US).apply {
                            timeZone = TimeZone.getTimeZone("UTC")
                        }.parse(value)?.time ?: 0L
                    } catch (_: Exception) { 0L }
                }
            }
            else -> 0L
        }
    }

    // ─── Load history for a specific day ───────────────────
    fun loadDayHistory(date: String) {
        val deviceId = prefs.getString("device_uid", null) ?: return
        val jwt = prefs.getString("jwt_token", null) ?: return

        _loading.value = true
        viewModelScope.launch {
            try {
                val result = withContext(Dispatchers.IO) {
                    apiGetObject("/api/devices/$deviceId/locations?from_date=$date&to_date=$date&limit=50000", jwt)
                }
                val locationsArr = result.optJSONArray("locations")
                if (locationsArr == null || locationsArr.length() == 0) {
                    _error.postValue("داده‌ای یافت نشد")
                    _points.postValue(emptyList())
                    _weakAreas.postValue(emptyList())
                    _stats.postValue(null)
                    _loading.postValue(false)
                    return@launch
                }

                // تبدیل به لیست خام
                val rawPoints = mutableListOf<HistoryPoint>()
                for (i in 0 until locationsArr.length()) {
                    val p = locationsArr.getJSONObject(i)
                    val ts = parseTimestamp(p.opt("timestamp"))
                    if (ts > 0 && dayFormat.format(java.util.Date(ts)) == date) {
                        rawPoints.add(
                            HistoryPoint(
                                lat = p.getDouble("lat"),
                                lng = p.getDouble("lon"),
                                speed = p.optDouble("speed", 0.0).toFloat(),
                                timestamp = ts,
                                satellites = p.optInt("satellites", 0),
                                accuracy = p.optDouble("accuracy", 0.0).toFloat()
                            )
                        )
                    }
                }

                // مرتب‌سازی بر اساس زمان
                rawPoints.sortBy { it.timestamp }

                // فیلتر اسپایک
                val filteredPoints = filterSpikes(rawPoints)

                // ذخیره نقاط فیلتر شده برای فیلتر زمانی بعدی
                allFilteredPoints = filteredPoints

                // تشخیص نواحی ضعیف (فقط برای اطلاعات، حذف نمیشه)
                val weakAreas = detectWeakAreas(filteredPoints)

                // فقط فیلتر زمانی اعمال میشه، نقاط ناحیه ضعیف حذف نمیشن
                val timeFilteredPoints = if (isTimeFilterActive) {
                    applyTimeRangeFilter(filteredPoints)
                } else {
                    filteredPoints
                }

                _points.postValue(timeFilteredPoints)
                _weakAreas.postValue(weakAreas)

                // محاسبه آمار
                computeStats(timeFilteredPoints)

                val spikeCount = rawPoints.size - timeFilteredPoints.size
                if (spikeCount > 0) {
                    _error.postValue("$spikeCount نقطه اسپایک فیلتر شد")
                } else if (timeFilteredPoints.isEmpty()) {
                    _error.postValue("موقعیتی برای این روز ثبت نشده")
                }
            } catch (e: Exception) {
                _error.postValue("خطا در دریافت تاریخچه: ${e.message}")
            }
            _loading.postValue(false)
        }
    }

    // ─── Time Range Filter ────────────────────────────────
    fun setTimeFilter(startHour: Int, startMin: Int, endHour: Int, endMin: Int) {
        timeStartHour = startHour
        timeStartMinute = startMin
        timeEndHour = endHour
        timeEndMinute = endMin
        isTimeFilterActive = !(startHour == 0 && startMin == 0 && endHour == 23 && endMin == 59)

        // اعمال فیلتر روی نقاط موجود (بدون درخواست مجدد از سرور)
        if (allFilteredPoints.isNotEmpty()) {
            val weakAreas = detectWeakAreas(allFilteredPoints)
            val timeFiltered = if (isTimeFilterActive) {
                applyTimeRangeFilter(allFilteredPoints)
            } else {
                allFilteredPoints  // نقاط ناحیه ضعیف حذف نمیشن (consistent با loadDayHistory)
            }
            _points.postValue(timeFiltered)
            _weakAreas.postValue(weakAreas)
            computeStats(timeFiltered)
        }
    }

    // فیلتر نقاط بر اساس بازه زمانی
    private fun applyTimeRangeFilter(points: List<HistoryPoint>): List<HistoryPoint> {
        val startMs = (timeStartHour * 3600L + timeStartMinute * 60L) * 1000L
        val endMs = (timeEndHour * 3600L + timeEndMinute * 60L) * 1000L

        return points.filter { point ->
            val cal = java.util.Calendar.getInstance().apply { timeInMillis = point.timestamp }
            val pointMs = (cal.get(java.util.Calendar.HOUR_OF_DAY) * 3600L +
                    cal.get(java.util.Calendar.MINUTE) * 60L +
                    cal.get(java.util.Calendar.SECOND)) * 1000L
            pointMs in startMs..endMs
        }
    }

    // محاسبه آمار از لیست نقاط
    private fun computeStats(points: List<HistoryPoint>) {
        if (points.isNotEmpty()) {
            val speeds = points.map { it.speed }.filter { it > 0f }
            val maxSpeed = speeds.maxOrNull()?.toInt() ?: 0
            val avgSpeed = if (speeds.isNotEmpty()) speeds.average().toInt() else 0
            val durationMs = (points.last().timestamp - points.first().timestamp).coerceAtLeast(0)
            val durationMin = (durationMs / 60000).toInt()

            // پیدا کردن زمانی که حداکثر سرعت رخ داده
            val peakTime = if (speeds.isNotEmpty()) {
                val maxSpeedFloat = speeds.maxOrNull()!!
                val peakPoint = points.find { it.speed == maxSpeedFloat }
                if (peakPoint != null) {
                    SimpleDateFormat("HH:mm", Locale.US).format(java.util.Date(peakPoint.timestamp))
                } else ""
            } else ""

            var totalDistance = 0.0
            for (j in 1 until points.size) {
                totalDistance += haversine(
                    points[j - 1].lat, points[j - 1].lng,
                    points[j].lat, points[j].lng
                )
            }
            val distStr = if (totalDistance >= 1000) {
                String.format("%.1f km", totalDistance / 1000)
            } else {
                String.format("%.0f m", totalDistance)
            }

            _stats.postValue(
                DayStats(
                    distance = distStr,
                    avgSpeed = "$avgSpeed",
                    maxSpeed = maxSpeed,
                    movingMin = durationMin,
                    stoppedMin = 0,
                    pointCount = points.size,
                    peakSpeedTime = peakTime
                )
            )
        } else {
            _stats.postValue(null)
        }
    }

    // ─── فیلتر اسپایک ─────────────────────────────────────
    // فقط اسپایک‌های واقعی حذف میشن، نه داده‌های GSM با دقت پایین
    private fun filterSpikes(points: List<HistoryPoint>): List<HistoryPoint> {
        if (points.size < 2) return points

        val result = mutableListOf<HistoryPoint>()
        result.add(points[0]) // اولین نقطه همیشه معتبر

        for (i in 1 until points.size) {
            val prev = result.last()
            val curr = points[i]

            val distanceM = haversine(prev.lat, prev.lng, curr.lat, curr.lng)
            val timeDiffSec = (curr.timestamp - prev.timestamp) / 1000.0

            // فیلتر ۱: سرعت غیرممکن (>200 km/h بین دو نقطه)
            if (timeDiffSec > 0) {
                val impliedSpeedKmh = (distanceM / timeDiffSec) * 3.6
                if (impliedSpeedKmh > MAX_VALID_SPEED_KMH) {
                    continue // اسپایک واقعی - حذف
                }
            }

            // فیلتر ۲: دقت خیلی بد (>1000m فقط برای GSM)
            if (curr.accuracy > MAX_ACCURACY_METERS && curr.accuracy > 0) {
                continue
            }

            // فیلتر ۳: نقطه تکراری (خیلی نزدیک و خیلی سریع)
            if (distanceM < 10 && timeDiffSec < 15) {
                continue
            }

            result.add(curr)
        }

        // فیلتر ۴: تایید دو نقطه‌ای - فقط برای فاصله خیلی زیاد
        // نقطه‌ای که از هر دو همسایه بیش از 2500m فاصله داره اسپایکه
        if (result.size >= 3) {
            val confirmed = mutableListOf(result[0])
            for (i in 1 until result.size - 1) {
                val prev = result[i - 1]
                val curr = result[i]
                val next = result[i + 1]

                val distPrev = haversine(prev.lat, prev.lng, curr.lat, curr.lng)
                val distNext = haversine(curr.lat, curr.lng, next.lat, next.lng)

                if (distPrev > 2500 && distNext > 2500) {
                    continue // اسپایک تنها - حذف
                }
                confirmed.add(curr)
            }
            confirmed.add(result.last())
            return confirmed
        }

        return result
    }

    // ─── تشخیص نواحی ضعیف ──────────────────────────────────
    // نقاطی که در یک شعاع کوچک پراکنده‌ان و سیگنال ضعیف دارن
    private fun detectWeakAreas(points: List<HistoryPoint>): List<WeakArea> {
        if (points.size < MIN_POINTS_FOR_WEAK_AREA) return emptyList()

        val weakAreas = mutableListOf<WeakArea>()
        val used = BooleanArray(points.size)

        for (i in points.indices) {
            if (used[i]) continue

            val cluster = mutableListOf(i)
            val center = points[i]

            for (j in i + 1 until points.size) {
                if (used[j]) continue
                val dist = haversine(center.lat, center.lng, points[j].lat, points[j].lng)
                val timeSpan = (points[j].timestamp - center.timestamp) / 1000.0

                if (dist < WEAK_AREA_RADIUS_METERS && timeSpan < MAX_WEAK_AREA_SPAN_SEC) {
                    cluster.add(j)
                }
            }

            if (cluster.size >= MIN_POINTS_FOR_WEAK_AREA) {
                // محاسبه مرکز ناحیه
                val avgLat = cluster.map { points[it].lat }.average()
                val avgLng = cluster.map { points[it].lng }.average()

                // محاسبه شعاع (بزرگترین فاصله از مرکز)
                val maxRadius = cluster.maxOfOrNull {
                    haversine(avgLat, avgLng, points[it].lat, points[it].lng)
                } ?: WEAK_AREA_RADIUS_METERS

                val minTime = cluster.minOf { points[it].timestamp }
                val maxTime = cluster.maxOf { points[it].timestamp }

                weakAreas.add(
                    WeakArea(
                        centerLat = avgLat,
                        centerLng = avgLng,
                        radiusMeters = (maxRadius + 10).coerceAtMost(200.0), // +10m حاشیه
                        pointCount = cluster.size,
                        startTime = minTime,
                        endTime = maxTime
                    )
                )

                cluster.forEach { used[it] = true }
            }
        }

        return weakAreas
    }

    // حذف نقاطی که در ناحیه ضعیف قرار دارن
    private fun removePointsInWeakAreas(
        points: List<HistoryPoint>, weakAreas: List<WeakArea>
    ): List<HistoryPoint> {
        if (weakAreas.isEmpty()) return points

        return points.filter { point ->
            weakAreas.none { area ->
                haversine(area.centerLat, area.centerLng, point.lat, point.lng) < area.radiusMeters
            }
        }
    }

    // ─── Haversine ──────────────────────────────────────────
    private fun haversine(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
        val r = 6371000.0
        val dLat = Math.toRadians(lat2 - lat1)
        val dLon = Math.toRadians(lon2 - lon1)
        val a = sin(dLat / 2).pow(2) +
                cos(Math.toRadians(lat1)) * cos(Math.toRadians(lat2)) *
                sin(dLon / 2).pow(2)
        val c = 2 * atan2(sqrt(a), sqrt(1 - a))
        return r * c
    }

    // ─── HTTP GET Helper ──────────────────────────────────
    private fun apiGetObject(path: String, jwt: String): JSONObject {
        val baseUrl = prefs.getString("server_url", "http://gps.example.com") ?: "http://gps.example.com"
        val url = URL("$baseUrl$path")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = "GET"
            setRequestProperty("Authorization", "Bearer $jwt")
            connectTimeout = 15000
            readTimeout = 30000
        }
        val code = conn.responseCode

        // If 401 → try refresh token
        if (code == 401) {
            conn.disconnect()
            val refreshed = refreshToken(baseUrl, jwt)
            if (refreshed != null) {
                // Retry with new token
                val conn2 = (URL("$baseUrl$path").openConnection() as HttpURLConnection).apply {
                    requestMethod = "GET"
                    setRequestProperty("Authorization", "Bearer $refreshed")
                    connectTimeout = 15000
                    readTimeout = 30000
                }
                val code2 = conn2.responseCode
                val stream2 = if (code2 in 200..299) conn2.inputStream else conn2.errorStream
                val response2 = BufferedReader(InputStreamReader(stream2)).use { it.readText() }
                conn2.disconnect()
                return JSONObject(response2)
            }
            // Refresh failed — return error
            throw Exception("Token expired. Please login again.")
        }

        val stream = if (code in 200..299) conn.inputStream else conn.errorStream
        val response = BufferedReader(InputStreamReader(stream)).use { it.readText() }
        conn.disconnect()
        return JSONObject(response)
    }

    private fun refreshToken(baseUrl: String, oldToken: String): String? {
        return try {
            val conn = (URL("$baseUrl/api/auth/refresh").openConnection() as HttpURLConnection).apply {
                requestMethod = "POST"
                setRequestProperty("Authorization", "Bearer $oldToken")
                setRequestProperty("Content-Type", "application/json")
                connectTimeout = 10000
                readTimeout = 10000
            }
            val code = conn.responseCode
            if (code in 200..299) {
                val body = JSONObject(BufferedReader(InputStreamReader(conn.inputStream)).use { it.readText() })
                conn.disconnect()
                val newToken = body.getString("access_token")
                // Save new token
                prefs.edit().putString("jwt_token", newToken).apply()
                newToken
            } else {
                conn.disconnect()
                null
            }
        } catch (_: Exception) {
            null
        }
    }
}
