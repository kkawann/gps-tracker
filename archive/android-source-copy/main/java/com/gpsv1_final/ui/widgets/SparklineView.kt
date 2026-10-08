package com.gpsv1_final.ui.widgets

import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Shader
import android.util.AttributeSet
import android.view.View
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

    private val linePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
        color = 0xFF22D3EE.toInt()
        strokeWidth = 5f
    }
    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }

    private val path = Path()
    private val fillPath = Path()

    fun setCapacity(n: Int) {
        capacity = n
        while (data.size > capacity) data.removeFirst()
    }

    fun addSample(kmh: Float) {
        data.addLast(kmh)
        while (data.size > capacity) data.removeFirst()
        max = max(max, kmh)
        if (max < 30f) max = 30f
        invalidate()
    }

    fun reset() {
        data.clear()
        max = 60f
        invalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        linePaint.strokeWidth = (h * 0.06f).coerceAtLeast(3f)
    }

    override fun onDraw(canvas: Canvas) {
        if (data.isEmpty()) return
        val w = width.toFloat()
        val h = height.toFloat()
        val n = data.size

        path.reset()
        fillPath.reset()

        var lastX = 0f
        var lastY = h
        var firstX = 0f

        var i = 0
        for (v in data) {
            val x = if (n == 1) w else w * i / (n - 1).toFloat()
            val y = h - (v / max) * h * 0.9f - h * 0.05f
            if (i == 0) {
                path.moveTo(x, y)
                fillPath.moveTo(x, h)
                firstX = x
            } else {
                path.lineTo(x, y)
            }
            lastX = x
            lastY = y
            i++
        }

        // Fill under line
        fillPath.lineTo(lastX, lastY)
        fillPath.lineTo(lastX, h)
        fillPath.lineTo(firstX, h)
        fillPath.close()

        fillPaint.shader = LinearGradient(
            0f, 0f, 0f, h,
            intArrayOf(0x5922D3EE, 0x0A22D3EE),
            floatArrayOf(0f, 1f),
            Shader.TileMode.CLAMP
        )
        canvas.drawPath(fillPath, fillPaint)
        canvas.drawPath(path, linePaint)
    }
}
