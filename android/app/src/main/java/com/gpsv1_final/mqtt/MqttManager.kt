package com.gpsv1_final.mqtt

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.hivemq.client.mqtt.mqtt3.Mqtt3Client
import com.hivemq.client.mqtt.datatypes.MqttQos
import com.hivemq.client.mqtt.lifecycle.MqttClientConnectedListener
import com.hivemq.client.mqtt.lifecycle.MqttClientDisconnectedListener
import com.hivemq.client.mqtt.mqtt3.Mqtt3AsyncClient
import com.hivemq.client.mqtt.mqtt3.message.connect.connack.Mqtt3ConnAck
import com.hivemq.client.mqtt.mqtt3.message.publish.Mqtt3Publish
import com.hivemq.client.mqtt.mqtt3.message.subscribe.suback.Mqtt3SubAck
import com.gpsv1_final.model.GeofenceItem
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/**
 * MQTT Manager — HiveMQ client (async, better than Paho)
 */
class MqttManager(private val context: Context) {

    private var client: Mqtt3AsyncClient? = null
    private var brokerUrl: String = ""
    private var carId: String = ""
    private val mainHandler = Handler(Looper.getMainLooper())

    // جلوگیری از کرش ناوبری دوبل: callback فقط یکبار تحویل می‌شود
    private var resultDelivered = false
    private var onResultCallback: ((Boolean) -> Unit)? = null

    // ─── Callbacks ──────
    private val _onLocationUpdateCallbacks = mutableListOf<(lat: Double, lng: Double, speed: Float, satellites: Int,
                           engine: Boolean, timestamp: Long, signalBars: Int) -> Unit>()
    var onSmsStatus: ((status: String) -> Unit)? = null
    var onAuthResult: ((approved: Boolean) -> Unit)? = null
    var onSyncHello: ((oldestSeq: Long, newestSeq: Long, count: Int) -> Unit)? = null
    var onBacklogPoints: ((points: List<BacklogPoint>) -> Unit)? = null
    private val _onGeofenceAlertCallbacks = mutableListOf<(action: String, geofenceName: String, timestamp: Long) -> Unit>()
    private val _onConnectionChangedCallbacks = mutableListOf<(connected: Boolean) -> Unit>()
    var onRelayStatus: ((status: String, action: String, engine: Boolean) -> Unit)? = null

    // ─── Listener methods ──────
    fun addOnLocationUpdateListener(listener: (lat: Double, lng: Double, speed: Float, satellites: Int,
                                                engine: Boolean, timestamp: Long, signalBars: Int) -> Unit) {
        _onLocationUpdateCallbacks.add(listener)
    }

    fun addOnConnectionChangedListener(listener: (connected: Boolean) -> Unit) {
        _onConnectionChangedCallbacks.add(listener)
    }

    fun addOnGeofenceAlertListener(listener: (action: String, geofenceName: String, timestamp: Long) -> Unit) {
        _onGeofenceAlertCallbacks.add(listener)
    }

    // ─── Legacy setters ──────
    var onLocationUpdate: ((lat: Double, lng: Double, speed: Float, satellites: Int,
                           engine: Boolean, timestamp: Long, signalBars: Int) -> Unit)? = null
        set(value) {
            val old = field
            field = value
            if (old != null) _onLocationUpdateCallbacks.remove(old)
            value?.let { _onLocationUpdateCallbacks.add(it) }
        }
    var onConnectionChanged: ((connected: Boolean) -> Unit)? = null
        set(value) {
            val old = field
            field = value
            if (old != null) _onConnectionChangedCallbacks.remove(old)
            value?.let { _onConnectionChangedCallbacks.add(it) }
        }
    var onGeofenceAlert: ((action: String, geofenceName: String, timestamp: Long) -> Unit)? = null
        set(value) {
            val old = field
            field = value
            if (old != null) _onGeofenceAlertCallbacks.remove(old)
            value?.let { _onGeofenceAlertCallbacks.add(it) }
        }

    data class BacklogPoint(
        val lat: Double, val lng: Double, val speed: Float,
        val ts: Long, val seq: Long
    )

    // ─── Connect ──────
    fun connect(broker: String, car: String, username: String = "", password: String = "", onResult: (Boolean) -> Unit) {
        if (client?.state?.isConnected == true) {
            Log.d(TAG, "Already connected, skipping")
            onResult(true)
            return
        }

        brokerUrl = broker
        carId = car
        onResultCallback = onResult

        val clientId = "mobile_${car}_${UUID.randomUUID().toString().take(8)}"
        val address = broker.removePrefix("tcp://")
        val host = address.substringBefore(":")
        val port = address.substringAfter(":", "1883").toIntOrNull() ?: 1883

        val builder = Mqtt3Client.builder()
            .identifier(clientId)
            .serverHost(host)
            .serverPort(port)
            // ⚠️ غیرفعال کردن auto-reconnect خود HiveMQ — GpsForegroundService خودش manage میکنه
            .automaticReconnect()
                .initialDelay(5, java.util.concurrent.TimeUnit.SECONDS)
                .maxDelay(30, java.util.concurrent.TimeUnit.SECONDS)
                .applyAutomaticReconnect()
            .addConnectedListener(MqttClientConnectedListener {
                Log.d(TAG, "Connected to $broker")
                mainHandler.post {
                    _onConnectionChangedCallbacks.forEach { it(true) }
                }
                subscribeToTopics()
                deliverResult(true)
            })
            .addDisconnectedListener(MqttClientDisconnectedListener {
                Log.w(TAG, "Disconnected")
                mainHandler.post {
                    _onConnectionChangedCallbacks.forEach { it(false) }
                }
            })

        if (username.isNotEmpty()) {
            builder.simpleAuth()
                .username(username)
                .password(password.toByteArray())
                .applySimpleAuth()
        }

        // 🔒 جلوگیری از connection leak: کلاینت قبلی (اگه هست) بسته بشه
        try { client?.disconnect() } catch (_: Exception) {}

        client = builder.buildAsync()

        // onResult فقط یکبار تحویل داده بشه — auto-reconnect های بعدی دوباره صدا نزنن
        resultDelivered = false

        try {
            val connectFuture = client!!.connectWith()
                .keepAlive(30)
                .send()

            // اگه connect fail شد
            connectFuture.whenComplete { _, throwable ->
                if (throwable != null) {
                    Log.e(TAG, "Connect failed: ${throwable.message}")
                    mainHandler.post { deliverResult(false) }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Connect exception: ${e.message}")
            deliverResult(false)
        }
    }

    /** نتیجه‌ی connect فقط یکبار به callback داده می‌شه (نه در auto-reconnect های بعدی) */
    private fun deliverResult(success: Boolean) {
        if (resultDelivered) return
        resultDelivered = true
        onResultCallback?.invoke(success)
    }

    // ─── Subscribe ──────
    private fun subscribeToTopics() {
        if (carId.isEmpty()) {
            Log.e(TAG, "⚠️ carId is EMPTY!")
            return
        }
        val topics = listOf(
            "gps/$carId/location",
            "gps/$carId/auth/code",
            "gps/$carId/auth/result",
            "gps/$carId/sync/hello",
            "gps/$carId/location/backlog",
            "gps/$carId/geofence/alert",
            "gps/$carId/relay/status"
        )
        Log.d(TAG, "📝 Subscribing to topics: $topics")
        try {
            topics.forEach { topic ->
                client!!.subscribeWith()
                    .topicFilter(topic)
                    .qos(MqttQos.AT_LEAST_ONCE)
                    .callback { publish ->
                        val t = publish.topic.toString()
                        val p = String(publish.payloadAsBytes)
                        Log.d(TAG, "📨 RAW msg: topic=$t, size=${p.length}B")
                        handleMessage(t, p)
                    }
                    .send()
                    .whenComplete { _, throwable ->
                        if (throwable != null) {
                            Log.e(TAG, "❌ Subscribe FAIL: $topic — ${throwable.message}")
                        }
                    }
            }
            Log.d(TAG, "Subscribed to ${topics.size} topics for car: $carId")
        } catch (e: Exception) {
            Log.e(TAG, "Subscribe exception: ${e.message}")
        }
    }

    // ─── Publish methods ──────
    fun publishAuthRequest(phone: String) {
        val payload = JSONObject().apply { put("phone", phone) }
        publish("gps/$carId/auth/req", payload)
        Log.d(TAG, "📤 auth/req → $carId for $phone")
    }

    fun publishAuthVerify(phone: String, code: String) {
        val payload = JSONObject().apply {
            put("phone", phone)
            put("code", code)
        }
        publish("gps/$carId/auth/verify", payload)
        Log.d(TAG, "📤 auth/verify → $carId")
    }

    fun publishRelayCommand(action: String) {
        val payload = JSONObject().apply {
            put("action", action)
            put("timestamp", System.currentTimeMillis())
        }
        publish("gps/$carId/relay/command", payload)
        Log.d(TAG, "📤 relay/$action → $carId")
    }

    fun publishSyncAck(lastKnownSeq: Long) {
        val payload = JSONObject().apply { put("lastKnownSeq", lastKnownSeq) }
        publish("gps/$carId/sync/ack", payload)
        Log.d(TAG, "📤 sync/ack → $carId (lastSeq:$lastKnownSeq)")
    }

    /** Publish full geofence config to ESP32 (topic: gps/{carId}/geofence/config) */
    fun publishGeofenceConfig(
        alertPhone: String,
        globalSpeedLimit: Int,
        geofences: List<GeofenceItem>
    ) {
        val payload = JSONObject().apply {
            put("alertPhone", alertPhone)
            put("globalSpeedLimit", globalSpeedLimit)
            val arr = JSONArray()
            geofences.forEach { gf ->
                arr.put(
                    JSONObject().apply {
                        put("id", gf.id)
                        put("lat", gf.centerLat)
                        put("lng", gf.centerLng)
                        put("radius", gf.radius)
                        put("alertOnExit", gf.alertOnExit)
                        put("alertOnEnter", gf.alertOnEnter)
                        put("exitMethod", gf.exitMethod)
                        put("enterMethod", gf.enterMethod)
                        put("speedLimit", gf.speedLimit)
                        put("enabled", gf.enabled)
                    }
                )
            }
            put("geofences", arr)
        }
        publish("gps/$carId/geofence/config", payload)
        Log.d(TAG, "📤 geofence/config → $carId (${geofences.size} fences)")
    }

    fun publishRaw(topic: String, payload: String) {
        try {
            client!!.publishWith()
                .topic(topic)
                .payload(payload.toByteArray())
                .qos(MqttQos.AT_LEAST_ONCE)
                .send()
            Log.d(TAG, "Published to $topic")
        } catch (e: Exception) {
            Log.e(TAG, "Publish failed: ${e.message}")
        }
    }

    private fun publish(topic: String, payload: JSONObject) {
        try {
            client!!.publishWith()
                .topic(topic)
                .payload(payload.toString().toByteArray())
                .qos(MqttQos.AT_LEAST_ONCE)
                .send()
        } catch (e: Exception) {
            Log.e(TAG, "Publish failed: ${e.message}")
        }
    }

    // ─── Handle Incoming Messages ──────
    private fun handleMessage(topic: String, rawPayload: String) {
        Log.d(TAG, "📨 handleMessage: topic=$topic")
        try {
            val json = JSONObject(rawPayload)

            when {
                topic.endsWith("/location") -> {
                    Log.d(TAG, "📍 Location: $rawPayload")
                    val lat = json.getDouble("lat")
                    val lng = json.getDouble("lon")
                    val speed = json.optDouble("speed", 0.0).toFloat()
                    val satellites = json.optInt("satellites", 0)
                    // timestamp ممکنه به ثانیه یا میلی‌ثانیه باشه → همیشه به میلی‌ثانیه تبدیل کن
                    val rawTs = json.optDouble("timestamp", System.currentTimeMillis() / 1000.0)
                    val timestamp = if (rawTs < 1_000_000_000_000.0) (rawTs * 1000).toLong() else rawTs.toLong()
                    val signalBars = getSignalBars(satellites)
                    // ⚠️ engine فقط از relay/status میاد — در payload location ESP32 وجود نداره
                    val engine = json.optBoolean("engine", false)

                    mainHandler.post {
                        _onLocationUpdateCallbacks.forEach { callback ->
                            try {
                                callback(lat, lng, speed, satellites, engine, timestamp, signalBars)
                            } catch (e: Exception) {
                                Log.e(TAG, "Error in callback: ${e.message}")
                            }
                        }
                    }
                }

                topic.endsWith("/auth/code") -> {
                    val status = json.optString("sms_status", "unknown")
                    mainHandler.post { onSmsStatus?.invoke(status) }
                    Log.d(TAG, "📨 SMS status: $status")
                }

                topic.endsWith("/auth/result") -> {
                    val approved = json.optString("status") == "approved"
                    mainHandler.post { onAuthResult?.invoke(approved) }
                    Log.d(TAG, "🔐 auth/result: approved=$approved")
                }

                topic.endsWith("/sync/hello") -> {
                    val oldestSeq = json.optLong("oldestSeq", 0)
                    val newestSeq = json.optLong("newestSeq", 0)
                    val count = json.optInt("count", 0)
                    mainHandler.post { onSyncHello?.invoke(oldestSeq, newestSeq, count) }
                    Log.d(TAG, "🔄 sync/hello: oldest=$oldestSeq newest=$newestSeq count=$count")
                }

                topic.endsWith("/location/backlog") -> {
                    val pointsArr = json.optJSONArray("points") ?: return
                    val points = mutableListOf<BacklogPoint>()
                    for (i in 0 until pointsArr.length()) {
                        val p = pointsArr.getJSONObject(i)
                        val lat = p.optDouble("lat", 0.0)
                        val lng = p.optDouble("lon", p.optDouble("lng", 0.0))
                        if (lat == 0.0 && lng == 0.0) continue
                        points.add(
                            BacklogPoint(
                                lat = lat,
                                lng = lng,
                                speed = p.optDouble("speed", 0.0).toFloat(),
                                ts = p.optLong("ts", 0),
                                seq = p.optLong("seq", 0)
                            )
                        )
                    }
                    mainHandler.post { onBacklogPoints?.invoke(points) }
                    Log.d(TAG, "📦 backlog: ${points.size} points")
                }

                topic.endsWith("/geofence/alert") -> {
                    val action = json.optString("action", "unknown")
                    val gfObj = json.optJSONObject("geofence")
                    val gfName = gfObj?.optString("name", "نامشخص") ?: "نامشخص"
                    val ts = json.optLong("timestamp", System.currentTimeMillis())
                    mainHandler.post {
                        _onGeofenceAlertCallbacks.forEach { it(action, gfName, ts) }
                    }
                    Log.d(TAG, "🚨 geofence alert: $action — $gfName")
                }

                topic.endsWith("/relay/status") -> {
                    val status = json.optString("status", "unknown")
                    val action = json.optString("action", "unknown")
                    val engine = json.optBoolean("engine", false)
                    mainHandler.post { onRelayStatus?.invoke(status, action, engine) }
                    Log.d(TAG, "🔌 relay/status: $status, action=$action, engine=$engine")
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Parse error on $topic: ${e.message}")
        }
    }

    // ─── Signal Bars ──────
    private fun getSignalBars(satellites: Int): Int {
        return when {
            satellites <= 0 -> 0
            satellites < 4 -> 1
            satellites < 6 -> 2
            satellites < 9 -> 3
            else -> 4
        }
    }

    // ─── Disconnect ──────
    fun disconnect() {
        try {
            client?.disconnect()
        } catch (_: Exception) {}
    }

    fun isConnected(): Boolean = client?.state?.isConnected == true

    fun getCarId(): String = carId

    companion object {
        private const val TAG = "MqttManager"
    }
}
