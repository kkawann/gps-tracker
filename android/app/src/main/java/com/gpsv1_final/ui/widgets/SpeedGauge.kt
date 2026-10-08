package com.gpsv1_final.ui.widgets

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.util.AttributeSet
import android.view.View
import android.view.animation.DecelerateInterpolator
import android.view.animation.OvershootInterpolator
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

/**
 * SpeedGauge v3 — گیج سرعت مینیمال و مدرن:
 * - قوس تمیز با گرادیان رنگی (cyan → green → amber → red)
 * - هاله نرم زیر قوس (فقط ۱ لایه)
 * - تیک‌های مینیمال (بدون تیک آستانه قرمز)
 * - انیمیشن نرم‌تر با spring-like overshoot
 * - حذف stroke اضافی — فقط قوس اصلی + glow
 */
class SpeedGauge @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private var progress = 0f          // 0..1
    private var displaySpeed = 0f
    private var maxSpeed = 180f

    // Track (background arc)
    private val trackPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
    }

    // Glow (soft halo under arc)
    private val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
    }

    // Main arc with gradient
    private val arcPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
    }

    // Ticks
    private val tickPaint = Paint(Paint.ANTI_ALIAS_FLAG)

    // Endpoint dot
    private val dotPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
    }

    private val arcRect = RectF()

    // Colors
    private var colorLow = 0xFF61D7C4.toInt()
    private var colorMid = 0xFF62C9A5.toInt()
    private var colorHigh = 0xFFE8B66D.toInt()
    private var colorCritical = 0xFFEB7884.toInt()

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
            duration = 600
            interpolator = DecelerateInterpolator(2f)
            addUpdateListener { anim ->
                val t = anim.animatedFraction
                // Smooth ease-out curve
                val eased = 1f - (1f - t) * (1f - t)
                progress = startProgress + (endProgress - startProgress) * eased
                displaySpeed = startNum + (target - startNum) * eased
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
        val stroke = (side * 0.075f).coerceAtLeast(5f * resources.displayMetrics.density)
        val half = stroke / 2f + side * 0.075f
        arcRect.set(half, half, width - half, height - half)

        val startAngle = 135f
        val sweep = 270f
        val current = currentColor(progress)

        // 1. Track (background)
        trackPaint.strokeWidth = stroke
        trackPaint.color = 0x15FFFFFF
        canvas.drawArc(arcRect, startAngle, sweep, false, trackPaint)

        // Layered halo makes the live value read clearly against a dark map.
        if (progress > 0.005f) {
            glowPaint.strokeWidth = stroke * 3.2f
            glowPaint.color = current
            glowPaint.alpha = 13
            canvas.drawArc(arcRect, startAngle, sweep * progress, false, glowPaint)
            glowPaint.strokeWidth = stroke * 1.8f
            glowPaint.alpha = 30
            canvas.drawArc(arcRect, startAngle, sweep * progress, false, glowPaint)
        }

        // 3. Main arc with gradient shader
        if (progress > 0.005f) {
            arcPaint.strokeWidth = stroke

            // Create gradient along the arc
            val cx = width / 2f
            val cy = height / 2f
            val r = (side - stroke) / 2f - side * 0.06f

            // Get start and end points of the progress arc
            val startRad = Math.toRadians(startAngle.toDouble())
            val endRad = Math.toRadians((startAngle + sweep * progress).toDouble())

            val x1 = cx + r * cos(startRad).toFloat()
            val y1 = cy + r * sin(startRad).toFloat()
            val x2 = cx + r * cos(endRad).toFloat()
            val y2 = cy + r * sin(endRad).toFloat()

            arcPaint.shader = LinearGradient(
                x1, y1, x2, y2,
                intArrayOf(
                    colorLow,
                    current
                ),
                floatArrayOf(0f, 1f),
                Shader.TileMode.CLAMP
            )
            canvas.drawArc(arcRect, startAngle, sweep * progress, false, arcPaint)
            arcPaint.shader = null
        }

        // 4. Minimal ticks (fewer, subtler)
        val cx = width / 2f
        val cy = height / 2f
        val r = (side - stroke) / 2f - side * 0.02f
        val tickCount = 25
        val density = resources.displayMetrics.density
        for (i in 0 until tickCount) {
            val angle = Math.toRadians(135.0 + 270.0 * i / (tickCount - 1))
            val isMajor = i % 6 == 0
            val inner = r - side * (if (isMajor) 0.043f else 0.022f)
            val outer = r - side * 0.008f
            val active = i.toFloat() / (tickCount - 1) <= progress
            tickPaint.color = if (active)
                (currentColor(progress) and 0x00FFFFFF) or (0x80 shl 24)
            else 0x20FFFFFF
            tickPaint.strokeWidth = if (isMajor) 2f * density else 1f * density
            canvas.drawLine(
                cx + cos(angle).toFloat() * inner, cy + sin(angle).toFloat() * inner,
                cx + cos(angle).toFloat() * outer, cy + sin(angle).toFloat() * outer,
                tickPaint
            )
        }

        // 5. Endpoint dot (glowing dot at the end of progress)
        if (progress > 0.01f) {
            val endAngle = Math.toRadians((startAngle + sweep * progress).toDouble())
            val dotX = cx + r * cos(endAngle).toFloat()
            val dotY = cy + r * sin(endAngle).toFloat()
            val dotR = stroke * 0.4f

            // Outer glow
            dotPaint.color = current
            dotPaint.alpha = 50
            canvas.drawCircle(dotX, dotY, dotR * 2.5f, dotPaint)

            // Crisp highlight at the moving endpoint.
            dotPaint.alpha = 255
            canvas.drawCircle(dotX, dotY, dotR * 1.5f, dotPaint)
            dotPaint.color = Color.WHITE
            canvas.drawCircle(dotX, dotY, dotR * 0.62f, dotPaint)
        }
    }

    override fun onDetachedFromWindow() {
        progressAnimator?.cancel()
        super.onDetachedFromWindow()
    }
}
