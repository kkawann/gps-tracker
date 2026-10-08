package com.gpsv1_final.model

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "geofence")
data class Geofence(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val car_id: String,
    val name: String,
    val lat: Double,
    val lng: Double,
    val radius: Float,
    val is_active: Int = 1
)
