package com.gpsv1_final.ui.widgets

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.os.Handler
import android.os.Looper
import android.util.AttributeSet
import android.view.View
import kotlin.math.min

/**
 * PulseRadar — حلقه‌های پالس رادار؛ روی نقشه در موقعیت خودرو.
 * سه حلقه با فاصله زمانی از هم منتشر می‌شوند و محو می‌شوند.
 */
class PulseRadar @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
    }
    private val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
    }
    private val dotPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
    }
    private val glowDotPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
    }

    private val handler = Handler(Looper.getMainLooper())
    private var phase = 0f            // 0..3 (سه حلقه با فاز 1 واحدی)
    private var running = false

    private val tick = object : Runnable {
        override fun run() {
            phase += 0.025f
            if (phase >= 3f) phase -= 3f
            invalidate()
            handler.postDelayed(this, 33)
        }
    }

    fun start() {
        if (running) return
        running = true
        handler.post(tick)
    }

    fun stop() {
        running = false
        handler.removeCallbacks(tick)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val cx = width / 2f
        val cy = height / 2f
        val maxR = min(width, height) / 2f

        // 3 expanding rings with glow
        for (i in 0 until 3) {
            val p = ((phase + i) % 3f) / 3f      // 0..1
            val r = maxR * p
            val alpha = ((1f - p) * 130).toInt().coerceIn(0, 255)
            val glowAlpha = ((1f - p) * 40).toInt().coerceIn(0, 255)

            // Glow ring (wider, softer)
            glowPaint.color = Color.argb(glowAlpha, 0x61, 0xD7, 0xC4)
            glowPaint.strokeWidth = (8f * (1f - p) + 1f)
            canvas.drawCircle(cx, cy, r, glowPaint)

            // Main ring
            paint.color = Color.argb(alpha, 0x61, 0xD7, 0xC4)
            paint.strokeWidth = (2.5f * (1f - p) + 0.5f)
            canvas.drawCircle(cx, cy, r, paint)
        }

        // Core dot with glow
        glowDotPaint.color = Color.argb(30, 0x61, 0xD7, 0xC4)
        canvas.drawCircle(cx, cy, maxR * 0.22f, glowDotPaint)

        dotPaint.color = Color.argb(255, 0x61, 0xD7, 0xC4)
        canvas.drawCircle(cx, cy, maxR * 0.08f, dotPaint)
        dotPaint.color = Color.argb(60, 0x61, 0xD7, 0xC4)
        canvas.drawCircle(cx, cy, maxR * 0.16f, dotPaint)
    }

    override fun onDetachedFromWindow() {
        stop()
        super.onDetachedFromWindow()
    }
}
