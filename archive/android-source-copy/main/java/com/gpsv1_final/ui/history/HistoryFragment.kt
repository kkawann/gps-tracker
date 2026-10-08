package com.gpsv1_final.ui.history

import android.content.Context
import android.graphics.Color
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.animation.OvershootInterpolator
import android.widget.LinearLayout
import android.widget.ProgressBar
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
    private lateinit var progressBar: ProgressBar
    private lateinit var btnTimeFrom: MaterialButton
    private lateinit var btnTimeTo: MaterialButton
    private lateinit var btnApplyTimeFilter: MaterialButton

    private var polyline: Polyline? = null
    private var selectedDate: String = ""
    private var selectedStartHour = 0
    private var selectedStartMinute = 0
    private var selectedEndHour = 23
    private var selectedEndMinute = 59

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
        val labelSdf = SimpleDateFormat("dd MMM", Locale.US)

        for (i in 0 until 14) {
            val date = sdf.format(cal.time)
            val label = labelSdf.format(cal.time)

            val btn = MaterialButton(
                requireContext(), null,
                com.google.android.material.R.attr.materialButtonOutlinedStyle
            ).apply {
                text = label
                textSize = 12f
                setTextColor(ContextCompat.getColor(requireContext(), R.color.text_secondary))
                setBackgroundColor(Color.TRANSPARENT)
                minimumWidth = 0
                setPadding(28, 14, 28, 14)
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).apply { marginEnd = 10 }
                setStrokeColorResource(R.color.card_border)
                cornerRadius = (resources.displayMetrics.density * 16).toInt()

                setOnClickListener {
                    selectedDate = date
                    viewModel.loadDayHistory(date)
                    highlightButton(this)
                }
            }

            if (i == 0) {
                styleSelected(btn)
                selectedDate = date
            }

            dayContainer.addView(btn)
            cal.add(Calendar.DAY_OF_YEAR, -1)
        }
    }

    private fun styleSelected(btn: MaterialButton) {
        btn.strokeColor = ContextCompat.getColorStateList(requireContext(), R.color.aurora_cyan)
        btn.strokeWidth = (resources.displayMetrics.density * 2).toInt()
        btn.setTextColor(ContextCompat.getColor(requireContext(), R.color.aurora_cyan))
        btn.animate().scaleX(1.06f).scaleY(1.06f).setDuration(160)
            .setInterpolator(OvershootInterpolator(1.4f)).start()
    }

    private fun styleUnselected(btn: MaterialButton) {
        btn.strokeColor = ContextCompat.getColorStateList(requireContext(), R.color.card_border)
        btn.strokeWidth = resources.displayMetrics.density.toInt()
        btn.setTextColor(ContextCompat.getColor(requireContext(), R.color.text_secondary))
        btn.animate().scaleX(1f).scaleY(1f).setDuration(160).start()
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
                map.invalidate()
                return@observe
            }

            val geoPoints = points.map { GeoPoint(it.lat, it.lng) }

            // Neon route: soft glow underlay + crisp cyan main line
            val glowLine = Polyline().apply {
                setPoints(geoPoints)
                outlinePaint.color = Color.parseColor("#5522D3EE")
                outlinePaint.strokeWidth = 14f
            }
            polyline = Polyline().apply {
                setPoints(geoPoints)
                outlinePaint.color = Color.parseColor("#FF22D3EE")
                outlinePaint.strokeWidth = 7f
            }
            map.overlays.add(glowLine)
            map.overlays.add(polyline)

            // Start / end markers
            val startMarker = Marker(map).apply {
                position = geoPoints.first()
                setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
                title = "شروع"
            }
            map.overlays.add(startMarker)

            val lastPoint = points.last()
            val endMarker = Marker(map).apply {
                position = geoPoints.last()
                setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_BOTTOM)
                title = formatTimeAgo(lastPoint.timestamp)
            }
            map.overlays.add(endMarker)

            map.zoomToBoundingBox(BoundingBox.fromGeoPoints(geoPoints), true)
            map.invalidate()
        }

        viewModel.stats.observe(viewLifecycleOwner) { stats ->
            if (stats != null) {
                tvDistance.text = stats.distance
                tvDuration.text = "${stats.movingMin} min"
                tvMaxSpeed.text = "${stats.maxSpeed}"
                tvAvgSpeed.text = stats.avgSpeed
            } else {
                tvDistance.text = "--"
                tvDuration.text = "--"
                tvMaxSpeed.text = "--"
                tvAvgSpeed.text = "--"
            }
        }

        viewModel.loading.observe(viewLifecycleOwner) { loading ->
            progressBar.visibility = if (loading) View.VISIBLE else View.GONE
        }

        viewModel.error.observe(viewLifecycleOwner) { error ->
            error?.let {
                Toast.makeText(requireContext(), it, Toast.LENGTH_SHORT).show()
            }
        }
    }

    // ─── Entry animation for stat cards ─────────────────────

    private fun animateStatsIn() {
        view?.post {
            val root = view ?: return@post
            // The 4 stat cards live inside the LinearLayout above the map
            val statsRow = (root as? ViewGroup)?.let { findStatsRow(it) } ?: return@post
            for (i in 0 until statsRow.childCount) {
                val card = statsRow.getChildAt(i)
                card.alpha = 0f
                card.translationY = 30f
                card.animate()
                    .alpha(1f).translationY(0f)
                    .setStartDelay(120L * i)
                    .setDuration(420)
                    .setInterpolator(OvershootInterpolator(0.9f))
                    .start()
            }
        }
    }

    private fun findStatsRow(root: ViewGroup): ViewGroup? {
        for (i in 0 until root.childCount) {
            val c = root.getChildAt(i)
            if (c is ViewGroup && c.childCount == 4 && c.getChildAt(0) is ViewGroup) {
                return c
            }
        }
        return null
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
