package com.gpsv1_final.ui.widgets

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import android.view.animation.DecelerateInterpolator
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

/**
 * SpeedGauge — گیج سرعت نئونی:
 * - قوس گرادیانی که با سرعت پر می‌شود (cyan → green → amber → red)
 * - هاله گلو لیزری دور قوس
 * - تیک‌های ظریف + تیک آستانه قرمز در ۸۰٪
 * - عدد سرعت با انیمیشن count-up هموار
 */
class SpeedGauge @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private var progress = 0f          // 0..1
    private var displaySpeed = 0f
    private var maxSpeed = 180f

    private val trackPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
    }
    private val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
    }
    private val arcPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
    }
    private val tickPaint = Paint(Paint.ANTI_ALIAS_FLAG)

    private val arcRect = RectF()

    private var colorLow = 0xFF22D3EE.toInt()      // cyan
    private var colorMid = 0xFF34D399.toInt()      // green
    private var colorHigh = 0xFFFBBF24.toInt()     // amber
    private var colorCritical = 0xFFFF4D67.toInt() // red

    private var progressAnimator: ValueAnimator? = null

    fun setSpeed(kmh: Float, animate: Boolean = true) {
        val clamped = kmh.coerceIn(0f, maxSpeed)
        if (animate) {
            animateTo(clamped)
        } else {
            progress = clamped / maxSpeed
            displaySpeed = clamped
            invalidate()
        }
    }

    fun setMaxSpeed(max: Float) {
        maxSpeed = max.coerceAtLeast(20f)
        invalidate()
    }

    private fun animateTo(target: Float) {
        progressAnimator?.cancel()
        val startProgress = progress
        val endProgress = target / maxSpeed
        val startNum = displaySpeed

        progressAnimator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 500
            interpolator = DecelerateInterpolator(1.5f)
            addUpdateListener { anim ->
                val t = anim.animatedFraction
                progress = startProgress + (endProgress - startProgress) * t
                displaySpeed = startNum + (target - startNum) * t
                invalidate()
            }
            start()
        }
    }

    private fun currentColor(p: Float): Int = when {
        p < 0.5f -> blend(colorLow, colorMid, p / 0.5f)
        p < 0.8f -> blend(colorMid, colorHigh, (p - 0.5f) / 0.3f)
        else -> blend(colorHigh, colorCritical, ((p - 0.8f) / 0.2f).coerceIn(0f, 1f))
    }

    private fun blend(c1: Int, c2: Int, t: Float): Int {
        val r = (Color.red(c1) + (Color.red(c2) - Color.red(c1)) * t).toInt()
        val g = (Color.green(c1) + (Color.green(c2) - Color.green(c1)) * t).toInt()
        val b = (Color.blue(c1) + (Color.blue(c2) - Color.blue(c1)) * t).toInt()
        return Color.rgb(r, g, b)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val side = min(width, height).toFloat()
        val stroke = side * 0.085f
        val half = stroke / 2f + side * 0.04f
        arcRect.set(half, half, width - half, height - half)

        val startAngle = 135f
        val sweep = 270f
        val current = currentColor(progress)

        trackPaint.strokeWidth = stroke
        trackPaint.color = 0x22FFFFFF
        canvas.drawArc(arcRect, startAngle, sweep, false, trackPaint)

        if (progress > 0.005f) {
            // 3-layer GPU-friendly glow: wide faint → medium → core
            glowPaint.strokeWidth = stroke * 2.4f
            glowPaint.color = current
            glowPaint.alpha = 28
            canvas.drawArc(arcRect, startAngle, sweep * progress, false, glowPaint)

            glowPaint.strokeWidth = stroke * 1.6f
            glowPaint.alpha = 60
            canvas.drawArc(arcRect, startAngle, sweep * progress, false, glowPaint)

            // Main arc
            arcPaint.strokeWidth = stroke
            arcPaint.color = current
            canvas.drawArc(arcRect, startAngle, sweep * progress, false, arcPaint)
        }

        // Ticks
        val cx = width / 2f
        val cy = height / 2f
        val r = (side - stroke) / 2f - side * 0.02f
        val tickCount = 13
        for (i in 0 until tickCount) {
            val angle = Math.toRadians(135.0 + 270.0 * i / (tickCount - 1))
            val isMajor = i % 3 == 0
            val inner = r - side * (if (isMajor) 0.055f else 0.035f)
            val outer = r - side * 0.008f
            val active = i.toFloat() / (tickCount - 1) <= progress
            tickPaint.color = if (active)
                (currentColor(progress) and 0x00FFFFFF) or (0xB3 shl 24)
            else 0x33FFFFFF
            canvas.drawLine(
                cx + cos(angle).toFloat() * inner, cy + sin(angle).toFloat() * inner,
                cx + cos(angle).toFloat() * outer, cy + sin(angle).toFloat() * outer,
                tickPaint
            )
        }

        // Threshold tick (red) at 80%
        val thrAngle = Math.toRadians(135.0 + 270.0 * 0.8)
        tickPaint.color = colorCritical
        canvas.drawLine(
            cx + cos(thrAngle).toFloat() * (r - side * 0.07f),
            cy + sin(thrAngle).toFloat() * (r - side * 0.07f),
            cx + cos(thrAngle).toFloat() * r,
            cy + sin(thrAngle).toFloat() * r,
            tickPaint
        )
    }

    override fun onDetachedFromWindow() {
        progressAnimator?.cancel()
        super.onDetachedFromWindow()
    }
}
