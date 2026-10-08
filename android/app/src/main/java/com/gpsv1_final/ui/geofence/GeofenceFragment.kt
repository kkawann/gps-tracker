package com.gpsv1_final.ui.geofence

import android.content.Context
import android.graphics.Color
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.viewModels
import com.gpsv1_final.R
import com.gpsv1_final.model.GeofenceItem
import com.google.android.material.button.MaterialButton
import com.google.android.material.button.MaterialButtonToggleGroup
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.floatingactionbutton.FloatingActionButton
import com.google.android.material.materialswitch.MaterialSwitch
import com.google.android.material.textfield.TextInputEditText
import org.osmdroid.config.Configuration
import org.osmdroid.events.MapEventsReceiver
import org.osmdroid.tileprovider.tilesource.TileSourceFactory
import org.osmdroid.util.GeoPoint
import org.osmdroid.views.MapView
import org.osmdroid.views.overlay.MapEventsOverlay
import org.osmdroid.views.overlay.Marker
import org.osmdroid.views.overlay.Polygon

class GeofenceFragment : Fragment() {

    private val viewModel: GeofenceViewModel by viewModels()

    private lateinit var map: MapView
    private lateinit var dialogGeofence: android.widget.ScrollView
    private lateinit var tvDialogTitle: TextView
    private lateinit var etName: TextInputEditText
    private lateinit var etRadius: TextInputEditText
    private lateinit var etSpeedLimit: TextInputEditText
    private lateinit var switchExitAlert: MaterialSwitch
    private lateinit var exitMethodRow: LinearLayout
    private lateinit var toggleExitMethod: MaterialButtonToggleGroup
    private lateinit var switchEnterAlert: MaterialSwitch
    private lateinit var enterMethodRow: LinearLayout
    private lateinit var toggleEnterMethod: MaterialButtonToggleGroup
    private lateinit var btnSave: MaterialButton
    private lateinit var btnCancel: MaterialButton
    private lateinit var fabAdd: FloatingActionButton

    private var selectedPoint: GeoPoint? = null
    private var selectedMarker: Marker? = null
    private var editingItem: GeofenceItem? = null

    private lateinit var geofenceChipList: LinearLayout

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_geofence, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        Configuration.getInstance().load(
            requireContext(),
            requireContext().getSharedPreferences("osmdroid", Context.MODE_PRIVATE)
        )
        Configuration.getInstance().userAgentValue = requireContext().packageName

        map = view.findViewById(R.id.mapGeofence)
        dialogGeofence = view.findViewById(R.id.dialogGeofence)
        tvDialogTitle = view.findViewById(R.id.tvDialogTitle)
        etName = view.findViewById(R.id.etGeofenceName)
        etRadius = view.findViewById(R.id.etGeofenceRadius)
        etSpeedLimit = view.findViewById(R.id.etGeofenceSpeedLimit)
        switchExitAlert = view.findViewById(R.id.switchExitAlert)
        exitMethodRow = view.findViewById(R.id.exitMethodRow)
        toggleExitMethod = view.findViewById(R.id.toggleExitMethod)
        switchEnterAlert = view.findViewById(R.id.switchEnterAlert)
        enterMethodRow = view.findViewById(R.id.enterMethodRow)
        toggleEnterMethod = view.findViewById(R.id.toggleEnterMethod)
        btnSave = view.findViewById(R.id.btnSaveGeofence)
        btnCancel = view.findViewById(R.id.btnCancelGeofence)
        fabAdd = view.findViewById(R.id.fabAddGeofence)
        geofenceChipList = view.findViewById(R.id.geofenceChipList)

        map.setTileSource(TileSourceFactory.MAPNIK)
        map.setMultiTouchControls(true)
        map.controller.setZoom(15.0)
        map.controller.setCenter(GeoPoint(35.6892, 51.3890))

        // لمس نقشه برای انتخاب مرکز
        val mapEventsOverlay = MapEventsOverlay(object : MapEventsReceiver {
            override fun singleTapConfirmedHelper(p: GeoPoint): Boolean {
                selectedPoint = p
                selectedMarker?.let { map.overlays.remove(it) }
                selectedMarker = Marker(map).apply {
                    position = p
                    setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
                    title = "مرکز حصار"
                }
                map.overlays.add(selectedMarker)
                map.invalidate()
                showGeofenceDialog(true)
                return true
            }
            override fun longPressHelper(p: GeoPoint): Boolean = false
        })
        map.overlays.add(0, mapEventsOverlay)

        fabAdd.setOnClickListener {
            Toast.makeText(requireContext(), "روی نقشه ضربه بزنید", Toast.LENGTH_SHORT).show()
        }

        switchExitAlert.setOnCheckedChangeListener { _, checked ->
            exitMethodRow.visibility = if (checked) View.VISIBLE else View.GONE
        }
        switchEnterAlert.setOnCheckedChangeListener { _, checked ->
            enterMethodRow.visibility = if (checked) View.VISIBLE else View.GONE
        }

        btnSave.setOnClickListener { saveGeofence() }
        btnCancel.setOnClickListener {
            resetDialog()
            showGeofenceDialog(false)
        }

        observeViewModel()
        viewModel.loadGeofences()
    }

    // ─── Observe ViewModel ─────────────────────────────────

    private fun observeViewModel() {
        viewModel.geofences.observe(viewLifecycleOwner) { geofences ->
            drawGeofencesOnMap(geofences)
        }

        viewModel.toastMessage.observe(viewLifecycleOwner) { msg ->
            Toast.makeText(requireContext(), msg, Toast.LENGTH_SHORT).show()
        }

        viewModel.loading.observe(viewLifecycleOwner) { _ -> }
    }

    // ─── Draw geofences on map ─────────────────────────────

    private fun drawGeofencesOnMap(geofences: List<GeofenceItem>) {
        // پاک کردن حصارهای قبلی
        map.overlays.removeAll { it is Polygon || (it is Marker && it.title?.startsWith("حصار:") == true) }

        geofences.forEach { gf ->
            val center = GeoPoint(gf.centerLat, gf.centerLng)
            val circle = createCirclePolygon(center, gf.radius.toDouble(), gf.enabled)
            map.overlays.add(circle)

            val marker = Marker(map).apply {
                position = center
                setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
                title = "حصار: ${gf.name}"
                snippet = buildString {
                    if (gf.enabled) "فعال" else "غیرفعال"
                    if (gf.speedLimit > 0) append(" | سرعت: ${gf.speedLimit}")
                }
                setOnMarkerClickListener { _, _ ->
                    showActionDialog(gf)
                    true
                }
            }
            map.overlays.add(marker)
        }
        map.invalidate()

        buildGeofenceChips(geofences)
    }

    // ─── فهرست شیشه‌ای حصارها — کلیک = پرواز نقشه به مرکز حصار ──

    private fun buildGeofenceChips(geofences: List<GeofenceItem>) {
        geofenceChipList.removeAllViews()

        geofences.forEach { gf ->
            val chip = TextView(requireContext()).apply {
                text = buildString {
                    append(if (gf.enabled) "● " else "○ ")
                    append(gf.name)
                    if (gf.speedLimit > 0) append("  ${gf.speedLimit}km/h")
                }
                textSize = 12f
                setTextColor(
                    ContextCompat.getColor(
                        requireContext(),
                        if (gf.enabled) R.color.aurora_cyan else R.color.text_hint
                    )
                )
                // فونت از res/font (نه assets) — createFromAsset برای این فونت crash می‌کرد
                typeface = androidx.core.content.res.ResourcesCompat.getFont(requireContext(), R.font.vazirmatn_semi_bold)
                setBackgroundResource(R.drawable.bg_pill_glow)
                val density = resources.displayMetrics.density
                setPadding((14 * density).toInt(), (9 * density).toInt(), (14 * density).toInt(), (9 * density).toInt())
                val lp = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                )
                lp.bottomMargin = 8
                layoutParams = lp
                // کلیک: نقشه به مرکز حصار پرتاب شود + زوم مناسب شعاع
                setOnClickListener {
                    val zoom = when {
                        gf.radius <= 200 -> 16.5
                        gf.radius <= 800 -> 15.0
                        gf.radius <= 2000 -> 13.5
                        else -> 12.0
                    }
                    map.controller.animateTo(GeoPoint(gf.centerLat, gf.centerLng))
                    map.controller.setZoom(zoom)
                    map.invalidate()
                }
            }
            geofenceChipList.addView(chip)
        }
    }

    private fun createCirclePolygon(center: GeoPoint, radiusMeters: Double, enabled: Boolean): Polygon {
        val polygon = Polygon()
        val points = mutableListOf<GeoPoint>()
        val segments = 64
        for (i in 0 until segments) {
            val angle = (2.0 * Math.PI * i) / segments
            val lat = center.latitude + (radiusMeters / 111320.0) * Math.cos(angle)
            val lng = center.longitude + (radiusMeters / (111320.0 * Math.cos(Math.toRadians(center.latitude)))) * Math.sin(angle)
            points.add(GeoPoint(lat, lng))
        }
        points.add(points[0])
        polygon.points = points

        val accent = ContextCompat.getColor(requireContext(), R.color.aurora_cyan)
        polygon.fillPaint.color = if (enabled) android.graphics.Color.argb(34, Color.red(accent), Color.green(accent), Color.blue(accent)) else Color.parseColor("#33808080")
        polygon.fillPaint.style = android.graphics.Paint.Style.FILL
        polygon.outlinePaint.color = if (enabled) accent else Color.parseColor("#FF808080")
        polygon.outlinePaint.strokeWidth = 3f * resources.displayMetrics.density
        return polygon
    }

    // ─── Save / edit dialog ────────────────────────────────────

    private fun saveGeofence() {
        val name = etName.text.toString().trim()
        val radiusText = etRadius.text.toString().trim()
        val point = selectedPoint

        if (name.isEmpty() || radiusText.isEmpty() || point == null) {
            Toast.makeText(requireContext(), "نام و شعاع را وارد کرده و روی نقشه ضربه بزنید", Toast.LENGTH_SHORT).show()
            return
        }

        val radius = radiusText.toIntOrNull() ?: 100
        val speedLimit = etSpeedLimit.text.toString().trim().toIntOrNull() ?: 0
        val alertOnExit = switchExitAlert.isChecked
        val alertOnEnter = switchEnterAlert.isChecked
        val exitMethod = if (toggleExitMethod.checkedButtonId == R.id.btnExitCall) "call" else "sms"
        val enterMethod = if (toggleEnterMethod.checkedButtonId == R.id.btnEnterCall) "call" else "sms"

        val editing = editingItem
        if (editing != null) {
            viewModel.updateGeofence(
                item = editing,
                name = name, lat = point.latitude, lng = point.longitude, radius = radius,
                alertOnExit = alertOnExit, alertOnEnter = alertOnEnter,
                exitMethod = exitMethod, enterMethod = enterMethod, speedLimit = speedLimit
            )
        } else {
            viewModel.createGeofence(
                name = name, lat = point.latitude, lng = point.longitude, radius = radius,
                alertOnExit = alertOnExit, alertOnEnter = alertOnEnter,
                exitMethod = exitMethod, enterMethod = enterMethod, speedLimit = speedLimit
            )
        }

        resetDialog()
        showGeofenceDialog(false)
    }

    /** نمایش/مخفی کردن پنل حصار با انیمیشن slide-up */
    private fun showGeofenceDialog(show: Boolean) {
        if (show) {
            dialogGeofence.visibility = View.VISIBLE
            dialogGeofence.translationY = 300f * resources.displayMetrics.density
            dialogGeofence.alpha = 0f
            dialogGeofence.animate()
                .translationY(0f).alpha(1f)
                .setDuration(320)
                .setInterpolator(android.view.animation.DecelerateInterpolator(1.6f))
                .start()
        } else {
            dialogGeofence.animate()
                .translationY(300f * resources.displayMetrics.density).alpha(0f)
                .setDuration(220)
                .setInterpolator(android.view.animation.AccelerateInterpolator())
                .withEndAction {
                    dialogGeofence.visibility = View.GONE
                    dialogGeofence.translationY = 0f
                    dialogGeofence.alpha = 1f
                }
                .start()
        }
    }

    // ─── Marker click: toggle / delete / edit ──────────────────────────────

    private fun showActionDialog(gf: GeofenceItem) {
        MaterialAlertDialogBuilder(requireContext())
            .setTitle(gf.name)
            .setMessage(if (gf.enabled) "حصار فعال است" else "حصار غیرفعال است")
            .setPositiveButton(if (gf.enabled) "غیرفعال کردن" else "فعال کردن") { _, _ ->
                viewModel.toggleGeofence(gf)
            }
            .setNeutralButton("ویرایش") { _, _ ->
                showEditDialog(gf)
            }
            .setNegativeButton("حذف") { _, _ ->
                showDeleteDialog(gf)
            }
            .show()
    }

    private fun showEditDialog(gf: GeofenceItem) {
        editingItem = gf
        tvDialogTitle.text = "ویرایش حصار"
        etName.setText(gf.name)
        etRadius.setText(gf.radius.toInt().toString())
        etSpeedLimit.setText(gf.speedLimit.toString())
        switchExitAlert.isChecked = gf.alertOnExit
        switchEnterAlert.isChecked = gf.alertOnEnter
        toggleExitMethod.check(if (gf.exitMethod == "call") R.id.btnExitCall else R.id.btnExitSms)
        toggleEnterMethod.check(if (gf.enterMethod == "call") R.id.btnEnterCall else R.id.btnEnterSms)
        exitMethodRow.visibility = if (gf.alertOnExit) View.VISIBLE else View.GONE
        enterMethodRow.visibility = if (gf.alertOnEnter) View.VISIBLE else View.GONE

        selectedPoint = GeoPoint(gf.centerLat, gf.centerLng)
        selectedMarker?.let { map.overlays.remove(it) }
        selectedMarker = Marker(map).apply {
            position = selectedPoint!!
            setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
            title = "مرکز حصار (ویرایش)"
        }
        map.overlays.add(selectedMarker!!)
        map.invalidate()

        showGeofenceDialog(true)
    }

    private fun resetDialog() {
        editingItem = null
        etName.text?.clear()
        etRadius.setText("500")
        etSpeedLimit.setText("0")
        switchExitAlert.isChecked = true
        switchEnterAlert.isChecked = true
        toggleExitMethod.check(R.id.btnExitCall)
        toggleEnterMethod.check(R.id.btnEnterSms)
        exitMethodRow.visibility = View.VISIBLE
        enterMethodRow.visibility = View.VISIBLE
        tvDialogTitle.text = "حصار جدید"
        selectedPoint = null
        selectedMarker?.let { map.overlays.remove(it) }
        selectedMarker = null
    }

    private fun showDeleteDialog(geofence: GeofenceItem) {
        MaterialAlertDialogBuilder(requireContext())
            .setTitle("حذف حصار")
            .setMessage("حصار «${geofence.name}» حذف شود؟")
            .setPositiveButton("حذف") { _, _ ->
                viewModel.deleteGeofence(geofence)
            }
            .setNegativeButton("لغو", null)
            .show()
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
