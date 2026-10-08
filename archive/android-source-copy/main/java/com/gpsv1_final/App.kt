package com.gpsv1_final

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.os.Build
import com.gpsv1_final.data.AppDatabase
import com.gpsv1_final.mqtt.MqttManager

class App : Application() {

    val database: AppDatabase by lazy { AppDatabase.getInstance(this) }
    var mqttManager: MqttManager? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "GPS Tracker",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "GPS tracking service"
            }
            val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            nm.createNotificationChannel(channel)
        }
    }

    companion object {
        const val CHANNEL_ID = "gps_service"
    }
}
