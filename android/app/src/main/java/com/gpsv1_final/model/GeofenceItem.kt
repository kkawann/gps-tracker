package com.gpsv1_final.model

/** Shared geofence item for Room ↔ ViewModel ↔ MQTT */
data class GeofenceItem(
    val id: Long = 0,           // local Room ID
    val serverId: String = "",  // server ID
    val name: String = "",
    val centerLat: Double = 0.0,
    val centerLng: Double = 0.0,
    val radius: Float = 0f,
    val enabled: Boolean = true,
    val alertOnExit: Boolean = false,
    val alertOnEnter: Boolean = false,
    val exitMethod: String = "call",
    val enterMethod: String = "sms",
    val speedLimit: Int = 0
)