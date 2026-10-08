package com.gpsv1_final.ui.widgets

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.View
import android.view.animation.AccelerateDecelerateInterpolator
import androidx.core.content.ContextCompat

/**
 * GlowDot — نقطه وضعیت با تنفس (breathing) نرم.
 * برای MQTT / Engine / GPS status.
 */
class GlowDot @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private var color = 0xFFFF4D67
    private var breathing = false

    private val corePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val haloPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }

    private var pulse = 0f  // 0..1 breathing phase
    private var animator: ValueAnimator? = null

    fun setColorRes(resId: Int) {
        color = ContextCompat.getColor(context, resId)
    }

    fun setGlowing(glow: Boolean) {
        if (glow == breathing) return
        breathing = glow
        if (glow) {
            animator = ValueAnimator.ofFloat(0f, 1f).apply {
                duration = 1200
                repeatCount = ValueAnimator.INFINITE
                repeatMode = ValueAnimator.REVERSE
                interpolator = AccelerateDecelerateInterpolator()
                addUpdateListener {
                    pulse = it.animatedValue as Float
                    invalidate()
                }
                start()
            }
        } else {
            animator?.cancel()
            pulse = 0f
            invalidate()
        }
    }

    override fun onDraw(canvas: Canvas) {
        val cx = width / 2f
        val cy = height / 2f
        val r = minOf(width, height) / 2f * 0.42f

        // Halo (breathes)
        haloPaint.color = color
        haloPaint.alpha = ((60 + pulse * 90).toInt())
        canvas.drawCircle(cx, cy, r * (1.5f + pulse * 0.5f), haloPaint)

        // Core
        corePaint.color = color
        corePaint.alpha = 255
        canvas.drawCircle(cx, cy, r, corePaint)
    }

    override fun onDetachedFromWindow() {
        animator?.cancel()
        super.onDetachedFromWindow()
    }
}
