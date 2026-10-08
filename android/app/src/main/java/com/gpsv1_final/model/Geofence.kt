package com.gpsv1_final.model

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "geofence")
data class Geofence(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val car_id: String,
    val server_id: String = "",           // ID from server (for sync)
    val name: String,
    val lat: Double,
    val lng: Double,
    val radius: Float,
    val is_active: Int = 1,               // 1 = enabled, 0 = disabled
    val alert_on_exit: Int = 1,           // 1 = true, 0 = false
    val alert_on_enter: Int = 1,
    val exit_method: String = "call",     // "call" | "sms"
    val enter_method: String = "sms",     // "call" | "sms"
    val speed_limit: Int = 0              // 0 = no limit
)