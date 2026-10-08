package com.gpsv1_final.data

import androidx.lifecycle.LiveData
import androidx.room.*
import com.gpsv1_final.model.Geofence

@Dao
interface GeofenceDao {

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insert(geofence: Geofence): Long

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertAll(geofences: List<Geofence>)

    @Delete
    suspend fun delete(geofence: Geofence)

    @Query("DELETE FROM geofence WHERE car_id = :carId")
    suspend fun deleteAllForCar(carId: String)

    @Query("SELECT * FROM geofence WHERE car_id = :carId")
    fun getAll(carId: String): LiveData<List<Geofence>>

    @Query("SELECT * FROM geofence WHERE car_id = :carId")
    suspend fun getAllBlocking(carId: String): List<Geofence>

    @Query("SELECT * FROM geofence WHERE id = :id")
    suspend fun getById(id: Long): Geofence?

    @Query("SELECT * FROM geofence WHERE server_id = :serverId AND car_id = :carId")
    suspend fun getByServerId(serverId: String, carId: String): Geofence?

    @Query("UPDATE geofence SET is_active = :active WHERE id = :id")
    suspend fun setActive(id: Long, active: Int)

    @Query("UPDATE geofence SET name = :name, lat = :lat, lng = :lng, radius = :radius, " +
           "alert_on_exit = :alertOnExit, alert_on_enter = :alertOnEnter, " +
           "exit_method = :exitMethod, enter_method = :enterMethod, speed_limit = :speedLimit, " +
           "is_active = :enabled WHERE id = :id")
    suspend fun updateFull(
        id: Long,
        name: String,
        lat: Double,
        lng: Double,
        radius: Float,
        alertOnExit: Int,
        alertOnEnter: Int,
        exitMethod: String,
        enterMethod: String,
        speedLimit: Int,
        enabled: Int
    )

    @Query("UPDATE geofence SET server_id = :serverId WHERE id = :id")
    suspend fun setServerId(id: Long, serverId: String)
}