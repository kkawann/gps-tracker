package com.gpsv1_final.ui

import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import androidx.navigation.fragment.NavHostFragment
import androidx.navigation.ui.setupWithNavController
import com.gpsv1_final.R
import com.google.android.material.bottomnavigation.BottomNavigationView

class MainActivity : AppCompatActivity() {

    private lateinit var bottomNav: BottomNavigationView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        bottomNav = findViewById(R.id.bottom_nav)

        val navHostFragment = supportFragmentManager
            .findFragmentById(R.id.nav_host_fragment) as NavHostFragment
        val navController = navHostFragment.navController

        bottomNav.setupWithNavController(navController)

        navController.addOnDestinationChangedListener { _, destination, _ ->
            when (destination.id) {
                R.id.authFragment -> hideNavAnimated()
                else -> {
                    showNavAnimated()
                    animatePageChange()
                }
            }
        }
    }

    private fun animatePageChange() {
        val content = findViewById<View>(R.id.nav_host_fragment) ?: return
        content.animate().cancel()
        content.alpha = 0.92f
        content.translationY = 8f * resources.displayMetrics.density
        content.animate()
            .alpha(1f)
            .translationY(0f)
            .setDuration(240)
            .setInterpolator(android.view.animation.DecelerateInterpolator(1.5f))
            .start()
    }

    /** ناوبری پایین: slide-down هنگام ورود، slide-up هنگام داشبورد */
    private fun hideNavAnimated() {
        if (bottomNav.visibility == View.VISIBLE) {
            bottomNav.animate()
                .translationY(bottomNav.height.toFloat())
                .alpha(0f)
                .setDuration(240)
                .withEndAction { bottomNav.visibility = View.GONE }
                .start()
        }
    }

    private fun showNavAnimated() {
        if (bottomNav.visibility != View.VISIBLE) {
            bottomNav.visibility = View.VISIBLE
            bottomNav.translationY = bottomNav.height.toFloat()
            bottomNav.alpha = 0f
            bottomNav.animate()
                .translationY(0f).alpha(1f)
                .setDuration(280)
                .setInterpolator(android.view.animation.DecelerateInterpolator(1.4f))
                .start()
        }
    }
}
