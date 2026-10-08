package com.gpsv1_final.ui.history

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.animation.OvershootInterpolator
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.viewModels
import com.gpsv1_final.R
import com.google.android.material.button.MaterialButton
import org.osmdroid.config.Configuration
import org.osmdroid.tileprovider.tilesource.TileSourceFactory
import org.osmdroid.util.BoundingBox
import org.osmdroid.util.GeoPoint
import org.osmdroid.views.MapView
import org.osmdroid.views.overlay.Marker
import org.osmdroid.views.overlay.Polyline
import java.text.SimpleDateFormat
import java.util.*

class HistoryFragment : Fragment() {

    private val viewModel: HistoryViewModel by viewModels()

    private lateinit var map: MapView
    private lateinit var dayContainer: LinearLayout
    private lateinit var tvDistance: TextView
    private lateinit var tvDuration: TextView
    private lateinit var tvMaxSpeed: TextView
    private lateinit var tvAvgSpeed: TextView
    private lateinit var progressBar: View
    private lateinit var btnTimeFrom: MaterialButton
    private lateinit var btnTimeTo: MaterialButton
    private lateinit var btnApplyTimeFilter: MaterialButton
    private lateinit var tvSelectedDate: TextView
    private lateinit var stopInfoCard: LinearLayout
    private lateinit var tvStopTitle: TextView
    private lateinit var tvStopDuration: TextView
    private lateinit var emptyState: View

    private var polyline: Polyline? = null
    private var selectedDate: String = ""
    private var selectedStartHour = 0
    private var selectedStartMinute = 0
    private var selectedEndHour = 23
    private var selectedEndMinute = 59

    // Speed color thresholds
    companion object {
        private const val SPEED_LOW = 20f       // آبی
        private const val SPEED_MEDIUM = 50f     // سبز
        private const val SPEED_HIGH = 80f       // نارنجی
        private const val SPEED_VERY_HIGH = 120f // قرمز
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_history, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        Configuration.getInstance().load(
            requireContext(),
            requireContext().getSharedPreferences("osmdroid", Context.MODE_PRIVATE)
        )
        Configuration.getInstance().userAgentValue = requireContext().packageName

        map = view.findViewById(R.id.mapHistory)
        dayContainer = view.findViewById(R.id.daySelectorContainer)
        tvDistance = view.findViewById(R.id.tvDistance)
        tvDuration = view.findViewById(R.id.tvDuration)
        tvMaxSpeed = view.findViewById(R.id.tvMaxSpeed)
        tvAvgSpeed = view.findViewById(R.id.tvAvgSpeed)
        progressBar = view.findViewById(R.id.progressBarHistory)
        btnTimeFrom = view.findViewById(R.id.btnTimeFrom)
        btnTimeTo = view.findViewById(R.id.btnTimeTo)
        btnApplyTimeFilter = view.findViewById(R.id.btnApplyTimeFilter)
        tvSelectedDate = view.findViewById(R.id.tvSelectedDate)
        stopInfoCard = view.findViewById(R.id.stopInfoCard)
        tvStopTitle = view.findViewById(R.id.tvStopTitle)
        tvStopDuration = view.findViewById(R.id.tvStopDuration)
        emptyState = view.findViewById(R.id.tvHistoryEmpty)

        map.setTileSource(TileSourceFactory.MAPNIK)
        map.setMultiTouchControls(true)

        buildDaySelector()
        setupTimePickers()
        observeViewModel()
        animateStatsIn()

        val today = SimpleDateFormat("yyyy-MM-dd", Locale.US).format(Date())
        viewModel.loadDayHistory(today)
    }

    private fun buildDaySelector() {
        dayContainer.removeAllViews()
        val cal = Calendar.getInstance()
        val sdf = SimpleDateFormat("yyyy-MM-dd", Locale.US)
        val dayNumSdf = SimpleDateFormat("dd", Locale.US)
        val dayNameSdf = SimpleDateFormat("EEE", Locale.US)
        val monthSdf = SimpleDateFormat("MMM", Locale.US)

        for (i in 0 until 14) {
            val date = sdf.format(cal.time)
            val dayNum = dayNumSdf.format(cal.time)
            val dayName = getDayNameFa(dayNameSdf.format(cal.time))
            val month = monthSdf.format(cal.time)

            val btn = MaterialButton(
                requireContext(), null,
                com.google.android.material.R.attr.materialButtonOutlinedStyle
            ).apply {
                text = "$dayNum\n$dayName"
                textSize = 10f
                setTextColor(ContextCompat.getColor(requireContext(), R.color.text_secondary))
                backgroundTintList = ColorStateList.valueOf(Color.TRANSPARENT)
                minimumWidth = 0
                minHeight = 0
                insetTop = 0
                insetBottom = 0
                setPadding(4, 4, 4, 4)
                layoutParams = LinearLayout.LayoutParams(
                    (60 * resources.displayMetrics.density).toInt(),
                    (58 * resources.displayMetrics.density).toInt()
                ).apply {
                    val gap = (4 * resources.displayMetrics.density).toInt()
                    marginEnd = gap
                    marginStart = gap
                }
                setStrokeColorResource(R.color.card_border)
                cornerRadius = (resources.displayMetrics.density * 12).toInt()

                setOnClickListener {
                    selectedDate = date
                    tvSelectedDate.text = "$dayName $dayNum $month"
                    tvSelectedDate.animate().cancel()
                    tvSelectedDate.alpha = 0.55f
                    tvSelectedDate.animate().alpha(1f).setDuration(220).start()
                    viewModel.loadDayHistory(date)
                    highlightButton(this)
                }
            }

            if (i == 0) {
                styleSelected(btn)
                selectedDate = date
                val todayName = getDayNameFa(dayNameSdf.format(cal.time))
                tvSelectedDate.text = "$todayName $dayNum $month"
            }

            dayContainer.addView(btn)
            cal.add(Calendar.DAY_OF_YEAR, -1)
        }
    }

    private fun getDayNameFa(day: String): String = when (day.lowercase()) {
        "sat" -> "شنبه"
        "sun" -> "یکشنبه"
        "mon" -> "دوشنبه"
        "tue" -> "سه‌شنبه"
        "wed" -> "چهارشنبه"
        "thu" -> "پنجشنبه"
        "fri" -> "جمعه"
        else -> day
    }

    private fun styleSelected(btn: MaterialButton) {
        btn.strokeColor = ContextCompat.getColorStateList(requireContext(), R.color.aurora_cyan)
        btn.strokeWidth = (resources.displayMetrics.density * 2).toInt()
        btn.setTextColor(ContextCompat.getColor(requireContext(), R.color.aurora_cyan))
        btn.backgroundTintList = ColorStateList.valueOf(Color.parseColor("#2661D7C4"))
        btn.animate().cancel()
        btn.animate().scaleX(1.035f).scaleY(1.035f).alpha(1f).setDuration(180)
            .setInterpolator(OvershootInterpolator(0.8f)).start()
    }

    private fun styleUnselected(btn: MaterialButton) {
        btn.strokeColor = ContextCompat.getColorStateList(requireContext(), R.color.card_border)
        btn.strokeWidth = resources.displayMetrics.density.toInt()
        btn.setTextColor(ContextCompat.getColor(requireContext(), R.color.text_secondary))
        btn.backgroundTintList = ColorStateList.valueOf(Color.TRANSPARENT)
        btn.animate().cancel()
        btn.animate().scaleX(1f).scaleY(1f).alpha(0.82f).setDuration(160).start()
    }

    private fun highlightButton(selected: MaterialButton) {
        for (i in 0 until dayContainer.childCount) {
            (dayContainer.getChildAt(i) as? MaterialButton)?.let { styleUnselected(it) }
        }
        styleSelected(selected)
    }

    // ─── Time Pickers ─────────────────────────────────────
    private fun setupTimePickers() {
        btnTimeFrom.setOnClickListener {
            android.app.TimePickerDialog(
                requireContext(),
                { _, hour, minute ->
                    selectedStartHour = hour
                    selectedStartMinute = minute
                    btnTimeFrom.text = String.format("%02d:%02d", hour, minute)
                },
                selectedStartHour, selectedStartMinute, true
            ).show()
        }

        btnTimeTo.setOnClickListener {
            android.app.TimePickerDialog(
                requireContext(),
                { _, hour, minute ->
                    selectedEndHour = hour
                    selectedEndMinute = minute
                    btnTimeTo.text = String.format("%02d:%02d", hour, minute)
                },
                selectedEndHour, selectedEndMinute, true
            ).show()
        }

        btnApplyTimeFilter.setOnClickListener {
            viewModel.setTimeFilter(selectedStartHour, selectedStartMinute, selectedEndHour, selectedEndMinute)
        }
    }

    // ─── Observe ViewModel ─────────────────────────────────

    private fun observeViewModel() {
        viewModel.points.observe(viewLifecycleOwner) { points ->
            map.overlays.removeAll { it is Polyline || it is Marker }

            if (points.isEmpty()) {
                emptyState.visibility = if (progressBar.visibility == View.VISIBLE) View.GONE else View.VISIBLE
                map.invalidate()
                return@observe
            }
            emptyState.visibility = View.GONE

            val geoPoints = points.map { GeoPoint(it.lat, it.lng) }

            // ── رنگ‌بندی مسیر بر اساس سرعت ──
            drawSpeedColoredRoute(points, geoPoints)

            // Start / end markers
            val startMarker = Marker(map).apply {
                position = geoPoints.first()
                setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
                icon = ContextCompat.getDrawable(requireContext(), R.drawable.ic_history_start)
                title = "شروع"
            }
            map.overlays.add(startMarker)

            val lastPoint = points.last()
            val endMarker = Marker(map).apply {
                position = geoPoints.last()
                setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_BOTTOM)
                icon = ContextCompat.getDrawable(requireContext(), R.drawable.ic_history_finish)
                title = formatTimeAgo(lastPoint.timestamp)
            }
            map.overlays.add(endMarker)

            map.zoomToBoundingBox(BoundingBox.fromGeoPoints(geoPoints), true)
            map.invalidate()
        }

        viewModel.stats.observe(viewLifecycleOwner) { stats ->
            if (stats != null) {
                animateStat(tvDistance, stats.distance)
                animateStat(tvDuration, stats.movingTime)
                animateStat(tvMaxSpeed, "${stats.maxSpeed}")
                animateStat(tvAvgSpeed, stats.avgSpeed)
            } else {
                animateStat(tvDistance, "--")
                animateStat(tvDuration, "--")
                animateStat(tvMaxSpeed, "--")
                animateStat(tvAvgSpeed, "--")
            }
        }

        viewModel.stopEvents.observe(viewLifecycleOwner) { stops ->
            if (stops.isNotEmpty()) {
                // نمایش بزرگ‌ترین توقف
                val longestStop = stops.maxByOrNull { it.durationMin }
                longestStop?.let { stop ->
                    val wasHidden = stopInfoCard.visibility != View.VISIBLE
                    stopInfoCard.visibility = View.VISIBLE
                    tvStopTitle.text = "اینجا وایستادی"
                    tvStopDuration.text = formatStopDuration(stop.durationMin)
                    if (wasHidden) {
                        stopInfoCard.alpha = 0f
                        stopInfoCard.translationY = 10f * resources.displayMetrics.density
                        stopInfoCard.animate().alpha(1f).translationY(0f).setDuration(240).start()
                    }

                    // مارکر توقف روی نقشه
                    val stopMarker = Marker(map).apply {
                        position = GeoPoint(stop.lat, stop.lng)
                        setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
                        title = "توقف: ${formatStopDuration(stop.durationMin)}"
                    }
                    map.overlays.add(stopMarker)
                }
            } else {
                stopInfoCard.visibility = View.GONE
            }
        }

        viewModel.loading.observe(viewLifecycleOwner) { loading ->
            progressBar.visibility = if (loading) View.VISIBLE else View.GONE
            if (loading) emptyState.visibility = View.GONE
            else if (viewModel.points.value.isNullOrEmpty()) emptyState.visibility = View.VISIBLE
        }

        viewModel.error.observe(viewLifecycleOwner) { error ->
            error?.takeIf { it != "داده‌ای یافت نشد" }?.let {
                Toast.makeText(requireContext(), it, Toast.LENGTH_SHORT).show()
            }
        }
    }

    // ─── رسم مسیر رنگی بر اساس سرعت ──────────────────────
    private fun drawSpeedColoredRoute(points: List<HistoryViewModel.HistoryPoint>, geoPoints: List<GeoPoint>) {
        if (points.size < 2) return

        // خط نرم (glow)
        val glowLine = Polyline().apply {
            setPoints(geoPoints)
            outlinePaint.color = Color.parseColor("#2E61D7C4")
            outlinePaint.strokeWidth = 14f * resources.displayMetrics.density
        }
        map.overlays.add(glowLine)

        // Cap map overlay count for large days while retaining the full route shape.
        // A single overlay per sampled segment avoids tens of thousands of map overlays.
        val maxSegments = 600
        val stride = kotlin.math.ceil((points.size - 1).toDouble() / maxSegments).toInt().coerceAtLeast(1)
        var start = 0
        while (start < points.lastIndex) {
            val end = (start + stride).coerceAtMost(points.lastIndex)
            val speed = points.subList(start + 1, end + 1).map { it.speed }.average().toFloat()
            val segment = Polyline().apply {
                setPoints(geoPoints.subList(start, end + 1))
                outlinePaint.color = getSpeedColor(speed)
                outlinePaint.strokeWidth = 5f * resources.displayMetrics.density
                outlinePaint.strokeCap = android.graphics.Paint.Cap.ROUND
                outlinePaint.strokeJoin = android.graphics.Paint.Join.ROUND
            }
            map.overlays.add(segment)
            start = end
        }

        // مارکر حداکثر سرعت
        val maxSpeedPoint = points.maxByOrNull { it.speed }
        maxSpeedPoint?.let { point ->
            if (point.speed > SPEED_HIGH) {
                val maxMarker = Marker(map).apply {
                    position = GeoPoint(point.lat, point.lng)
                    setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_BOTTOM)
                    title = "حداکثر سرعت: ${point.speed.toInt()} km/h"
                }
                map.overlays.add(maxMarker)
            }
        }
    }

    // ─── رنگ بر اساس سرعت ────────────────────────────────
    private fun getSpeedColor(speed: Float): Int = when {
        speed < SPEED_LOW -> ContextCompat.getColor(requireContext(), R.color.aurora_cyan)
        speed < SPEED_MEDIUM -> ContextCompat.getColor(requireContext(), R.color.aurora_green)
        speed < SPEED_HIGH -> ContextCompat.getColor(requireContext(), R.color.aurora_amber)
        speed < SPEED_VERY_HIGH -> Color.parseColor("#FFE89A72")
        else -> ContextCompat.getColor(requireContext(), R.color.aurora_red)
    }

    // ─── فرمت مدت توقف ────────────────────────────────────
    private fun formatStopDuration(minutes: Int): String {
        val hours = minutes / 60
        val mins = minutes % 60
        return when {
            hours > 0 -> "$hours ساعت و $mins دقیقه"
            mins > 0 -> "$mins دقیقه"
            else -> "کمتر از ۱ دقیقه"
        }
    }

    // ─── Time-ago formatter ────────────────────────────────

    private fun formatTimeAgo(timestamp: Long): String {
        val diffMs = System.currentTimeMillis() - timestamp
        val diffMin = (diffMs / 60000).toInt()

        return when {
            diffMin < 1 -> "همین الان"
            diffMin < 60 -> "$diffMin دقیقه پیش"
            diffMin < 1440 -> {
                val hours = diffMin / 60
                val mins = diffMin % 60
                if (mins > 0) "$hours ساعت و $mins دقیقه پیش"
                else "$hours ساعت پیش"
            }
            else -> {
                val days = diffMin / 1440
                "$days روز پیش"
            }
        }
    }

    // ─── Entry animation for stat cards ─────────────────────

    private fun animateStatsIn() {
        view?.post {
            val root = view ?: return@post
            val statIds = intArrayOf(R.id.tvDistance, R.id.tvDuration, R.id.tvMaxSpeed, R.id.tvAvgSpeed)
            for (i in statIds.indices) {
                val card = root.findViewById<TextView>(statIds[i]).parent as? View ?: continue
                card.alpha = 0f
                card.translationY = 12f * resources.displayMetrics.density
                card.animate()
                    .alpha(1f).translationY(0f)
                    .setStartDelay(120L * i)
                    .setDuration(360)
                    .setInterpolator(OvershootInterpolator(0.65f))
                    .start()
            }
        }
    }

    private fun animateStat(label: TextView, value: String) {
        if (label.text.toString() == value) return
        label.animate().cancel()
        label.animate().alpha(0f).translationY(-4f * resources.displayMetrics.density)
            .setDuration(100).withEndAction {
                if (!isAdded || view == null) return@withEndAction
                label.text = value
                label.translationY = 4f * resources.displayMetrics.density
                label.animate().alpha(1f).translationY(0f).setDuration(180).start()
            }.start()
    }

    // ─── Lifecycle ─────────────────────────────────────────

    override fun onResume() {
        super.onResume()
        map.onResume()
    }

    override fun onPause() {
        super.onPause()
        map.onPause()
    }
}
