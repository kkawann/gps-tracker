package com.gpsv1_final.data

import androidx.lifecycle.LiveData
import androidx.room.*
import com.gpsv1_final.model.GpsPoint

@Dao
interface GpsPointDao {

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insert(point: GpsPoint)

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertAll(points: List<GpsPoint>)

    @Query("SELECT * FROM gps_point ORDER BY timestamp DESC LIMIT 1")
    fun getLatest(): LiveData<GpsPoint?>

    @Query("SELECT * FROM gps_point WHERE car_id = :carId AND timestamp BETWEEN :start AND :end ORDER BY timestamp ASC")
    suspend fun getBetween(carId: String, start: Long, end: Long): List<GpsPoint>

    @Query("SELECT * FROM gps_point WHERE car_id = :carId ORDER BY timestamp DESC LIMIT 500")
    suspend fun getRecent(carId: String): List<GpsPoint>

    @Query("DELETE FROM gps_point WHERE car_id = :carId")
    suspend fun deleteAll(carId: String)
}
