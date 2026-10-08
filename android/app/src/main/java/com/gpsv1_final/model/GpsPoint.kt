package com.gpsv1_final.model

import androidx.room.Entity
import androidx.room.Index

@Entity(
    tableName = "gps_point",
    primaryKeys = ["car_id", "timestamp"],
    indices = [Index("car_id", "timestamp")]
)
data class GpsPoint(
    val `car_id`: String,
    val lat: Double,
    val lng: Double,
    val speed: Float = 0f,
    val satellites: Int = 0,
    val engine: Int = 0,
    val signal: Int = 0,
    val timestamp: Long
)
