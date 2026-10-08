package com.gpsv1_final.ui.widgets

import android.content.Context
import android.animation.ValueAnimator
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Shader
import android.util.AttributeSet
import android.view.View
import android.view.animation.DecelerateInterpolator
import kotlin.math.max

/**
 * SparklineView — نمودار زنده سرعت (خط با گرادیان fill زیرش).
 * داده‌ها را استریم می‌گیری، خودش هموار و رسم می‌کند.
 */
class SparklineView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val data = ArrayDeque<Float>()
    private var capacity = 40
    private var max = 60f
    private var displayedTail: Float? = null
    private var tailAnimator: ValueAnimator? = null

    private val linePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
        color = 0xFF61D7C4.toInt()
        strokeWidth = 5f
    }
    private val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
        color = 0xFF61D7C4.toInt()
        strokeWidth = 12f
        alpha = 40
    }
    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }

    private val path = Path()
    private val fillPath = Path()

    fun setCapacity(n: Int) {
        capacity = n
        while (data.size > capacity) data.removeFirst()
    }

    fun addSample(kmh: Float) {
        val previous = displayedTail ?: data.lastOrNull() ?: kmh
        tailAnimator?.cancel()
        data.addLast(kmh)
        while (data.size > capacity) data.removeFirst()
        displayedTail = previous
        tailAnimator = ValueAnimator.ofFloat(previous, kmh).apply {
            duration = 360
            interpolator = DecelerateInterpolator(1.6f)
            addUpdateListener {
                displayedTail = it.animatedValue as Float
                invalidate()
            }
            start()
        }
        max = max(max, kmh)
        if (max < 30f) max = 30f
        invalidate()
    }

    fun reset() {
        tailAnimator?.cancel()
        displayedTail = null
        data.clear()
        max = 60f
        invalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        linePaint.strokeWidth = (h * 0.06f).coerceAtLeast(3f)
        glowPaint.strokeWidth = linePaint.strokeWidth * 2.5f
    }

    override fun onDraw(canvas: Canvas) {
        if (data.isEmpty()) return
        val w = width.toFloat()
        val h = height.toFloat()
        val n = data.size

        path.reset()
        fillPath.reset()

        val points = data.mapIndexed { i, sample ->
            val value = if (i == n - 1) displayedTail ?: sample else sample
            val x = if (n == 1) w else w * i / (n - 1).toFloat()
            val y = h - (value / max) * h * 0.82f - h * 0.09f
            x to y
        }
        val firstX = points.first().first
        val (lastX, lastY) = points.last()
        path.moveTo(points.first().first, points.first().second)
        fillPath.moveTo(firstX, h)
        fillPath.lineTo(points.first().first, points.first().second)

        // Smooth Catmull-Rom-like cubic segments keep the live trace fluid.
        for (i in 0 until points.lastIndex) {
            val (x1, y1) = points[i]
            val (x2, y2) = points[i + 1]
            val dx = (x2 - x1) * 0.38f
            path.cubicTo(x1 + dx, y1, x2 - dx, y2, x2, y2)
            fillPath.cubicTo(x1 + dx, y1, x2 - dx, y2, x2, y2)
        }

        // Fill under line
        fillPath.lineTo(lastX, lastY)
        fillPath.lineTo(lastX, h)
        fillPath.lineTo(firstX, h)
        fillPath.close()

        fillPaint.shader = LinearGradient(
            0f, 0f, 0f, h,
            intArrayOf(0x5961D7C4.toInt(), 0x0A61D7C4),
            floatArrayOf(0f, 1f),
            Shader.TileMode.CLAMP
        )
        canvas.drawPath(fillPath, fillPaint)
        // Glow layer (wider, semi-transparent)
        canvas.drawPath(path, glowPaint)
        // Main line
        canvas.drawPath(path, linePaint)
    }

    override fun onDetachedFromWindow() {
        tailAnimator?.cancel()
        super.onDetachedFromWindow()
    }
}
