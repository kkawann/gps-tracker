package com.gpsv1_final.model

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "trip")
data class Trip(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val car_id: String,
    val day: String,
    val distance_km: Float = 0f,
    val duration_min: Int = 0,
    val start_time: Long = 0,
    val end_time: Long = 0
)
