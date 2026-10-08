package com.gpsv1_final.data

import androidx.lifecycle.LiveData
import androidx.room.*
import com.gpsv1_final.model.Geofence

@Dao
interface GeofenceDao {

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insert(geofence: Geofence): Long

    @Delete
    suspend fun delete(geofence: Geofence)

    @Query("SELECT * FROM geofence WHERE car_id = :carId")
    fun getAll(carId: String): LiveData<List<Geofence>>

    @Query("UPDATE geofence SET is_active = :active WHERE id = :id")
    suspend fun setActive(id: Long, active: Int)
}
