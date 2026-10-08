package com.gpsv1_final.data

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase
import com.gpsv1_final.model.Geofence
import com.gpsv1_final.model.GpsPoint
import com.gpsv1_final.model.Trip

@Database(entities = [GpsPoint::class, Trip::class, Geofence::class], version = 1, exportSchema = false)
abstract class AppDatabase : RoomDatabase() {
    abstract fun gpsPointDao(): GpsPointDao
    abstract fun tripDao(): TripDao
    abstract fun geofenceDao(): GeofenceDao

    companion object {
        @Volatile
        private var INSTANCE: AppDatabase? = null

        fun getInstance(context: Context): AppDatabase {
            return INSTANCE ?: synchronized(this) {
                Room.databaseBuilder(context, AppDatabase::class.java, "gps.db")
                    .fallbackToDestructiveMigration()
                    .build().also { INSTANCE = it }
            }
        }
    }
}
