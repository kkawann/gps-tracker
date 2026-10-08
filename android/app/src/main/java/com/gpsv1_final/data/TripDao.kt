package com.gpsv1_final.data

import androidx.lifecycle.LiveData
import androidx.room.*
import com.gpsv1_final.model.Trip

@Dao
interface TripDao {

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insert(trip: Trip): Long

    @Query("SELECT * FROM trip WHERE car_id = :carId ORDER BY day DESC")
    fun getDays(carId: String): LiveData<List<Trip>>

    @Query("SELECT * FROM trip WHERE car_id = :carId AND day = :day LIMIT 1")
    suspend fun getByDay(carId: String, day: String): Trip?

    @Query("DELETE FROM trip WHERE car_id = :carId")
    suspend fun deleteAll(carId: String)
}
