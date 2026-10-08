package com.gpsv1_final.ui.widgets

import android.animation.AnimatorSet
import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.content.Context
import android.util.AttributeSet
import android.view.LayoutInflater
import android.view.View
import android.view.animation.OvershootInterpolator
import android.view.animation.AccelerateDecelerateInterpolator
import android.widget.FrameLayout
import android.widget.ImageView
import com.gpsv1_final.R

/**
 * A Google Maps–style animated car marker.
 *
 * Features:
 *  - Drop-in bounce animation on first appearance
 *  - Smooth position interpolation when GPS updates
 *  - Subtle breathing glow shadow underneath
 *  - Looks exactly like the classic Google Maps red pin
 */
class AnimatedCarMarker @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : FrameLayout(context, attrs, defStyleAttr) {

    private val pinImage: ImageView
    private val glowShadow: ImageView
    private var hasAppeared = false
    private var prevScreenX = -1f
    private var prevScreenY = -1f

    // Position interpolation
    private var animX: Float = 0f
    private var animY: Float = 0f
    private var posAnimator: AnimatorSet? = null

    init {
        LayoutInflater.from(context).inflate(R.layout.widget_animated_car_marker, this, true)
        pinImage = findViewById(R.id.markerPin)
        glowShadow = findViewById(R.id.markerGlow)

        // Start invisible for drop-in
        alpha = 0f
        translationY = -120f
        scaleX = 0.3f
        scaleY = 0.3f
    }

    /**
     * Position the marker on screen (called from DashboardFragment).
     * Handles both first appearance (drop-in) and subsequent moves (smooth slide).
     */
    fun updatePosition(screenX: Float, screenY: Float) {
        if (!hasAppeared) {
            // First time — drop-in animation
            prevScreenX = screenX
            prevScreenY = screenY
            translationX = screenX - width / 2f
            translationY = screenY - height + 10f  // tip of pin points to coordinate
            dropInAnimation()
            hasAppeared = true
        } else {
            // Subsequent — smooth slide
            smoothMoveTo(screenX, screenY)
        }
    }

    /**
     * Drop-in bounce animation (Google Maps style)
     */
    private fun dropInAnimation() {
        // Phase 1: Drop from above + scale up + fade in
        val dropAnim = AnimatorSet().apply {
            playTogether(
                ObjectAnimator.ofFloat(this@AnimatedCarMarker, View.TRANSLATION_Y, -180f, animY.coerceAtLeast(translationY)),
                ObjectAnimator.ofFloat(this@AnimatedCarMarker, View.ALPHA, 0f, 1f),
                ObjectAnimator.ofFloat(this@AnimatedCarMarker, View.SCALE_X, 0.3f, 1f),
                ObjectAnimator.ofFloat(this@AnimatedCarMarker, View.SCALE_Y, 0.3f, 1f)
            )
            duration = 500
            interpolator = OvershootInterpolator(1.4f)
        }

        // Phase 2: Settle bounce
        val settleAnim = ObjectAnimator.ofFloat(this@AnimatedCarMarker, View.TRANSLATION_Y,
            this.translationY, this.translationY - 8f, this.translationY).apply {
            duration = 300
            startDelay = 450
            interpolator = AccelerateDecelerateInterpolator()
        }

        AnimatorSet().apply {
            playTogether(dropAnim, settleAnim)
            start()
        }
    }

    /**
     * Smooth position interpolation (no jarring jumps)
     */
    private fun smoothMoveTo(targetX: Float, targetY: Float) {
        val targetTranslationX = targetX - width / 2f
        val targetTranslationY = targetY - height + 10f

        posAnimator?.cancel()

        val animTx = ObjectAnimator.ofFloat(this, View.TRANSLATION_X, translationX, targetTranslationX)
        val animTy = ObjectAnimator.ofFloat(this, View.TRANSLATION_Y, translationY, targetTranslationY)

        posAnimator = AnimatorSet().apply {
            playTogether(animTx, animTy)
            duration = 400
            interpolator = AccelerateDecelerateInterpolator()
            start()
        }

        prevScreenX = targetX
        prevScreenY = targetY
    }

    /**
     * Start the breathing glow animation
     */
    fun startGlow() {
        glowShadow.animate()
            .scaleX(1.2f)
            .scaleY(1.2f)
            .alpha(0.3f)
            .setDuration(1200)
            .setInterpolator(AccelerateDecelerateInterpolator())
            .withEndAction {
                glowShadow.animate()
                    .scaleX(1f)
                    .scaleY(1f)
                    .alpha(0.6f)
                    .setDuration(1200)
                    .setInterpolator(AccelerateDecelerateInterpolator())
                    .withEndAction { startGlow() }
                    .start()
            }
            .start()
    }

    fun stopGlow() {
        glowShadow.animate().cancel()
    }
}
