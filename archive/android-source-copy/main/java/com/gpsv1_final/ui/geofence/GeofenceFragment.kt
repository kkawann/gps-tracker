package com.gpsv1_final.ui.geofence

import android.content.Context
import android.graphics.Color
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Switch
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.fragment.app.viewModels
import com.gpsv1_final.R
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.floatingactionbutton.FloatingActionButton
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
    private lateinit var dialogGeofence: LinearLayout
    private lateinit var etName: EditText
    private lateinit var etRadius: EditText
    private lateinit var btnSave: MaterialButton
    private lateinit var btnCancel: MaterialButton
    private lateinit var fabAdd: FloatingActionButton

    private var selectedPoint: GeoPoint? = null
    private var selectedMarker: Marker? = null

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
        etName = view.findViewById(R.id.etGeofenceName)
        etRadius = view.findViewById(R.id.etGeofenceRadius)
        btnSave = view.findViewById(R.id.btnSaveGeofence)
        btnCancel = view.findViewById(R.id.btnCancelGeofence)
        fabAdd = view.findViewById(R.id.fabAddGeofence)

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

        btnSave.setOnClickListener { saveGeofence() }
        btnCancel.setOnClickListener {
            showGeofenceDialog(false)
            selectedPoint = null
            selectedMarker?.let { map.overlays.remove(it) }
            selectedMarker = null
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

    private fun drawGeofencesOnMap(geofences: List<GeofenceViewModel.GeofenceItem>) {
        // پاک کردن حصارهای قبلی
        map.overlays.removeAll { it is Polygon || (it is Marker && it.title?.startsWith("حصار:") == true) }

        geofences.forEach { gf ->
            val center = GeoPoint(gf.centerLat, gf.centerLng)
            val circle = createCirclePolygon(center, gf.radius, gf.enabled)
            map.overlays.add(circle)

            val marker = Marker(map).apply {
                position = center
                setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
                title = "حصار: ${gf.name}"
                snippet = if (gf.enabled) "فعال" else "غیرفعال"
                setOnMarkerClickListener { _, _ ->
                    showDeleteDialog(gf)
                    true
                }
            }
            map.overlays.add(marker)
        }
        map.invalidate()
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

        val fillColor = if (enabled) "#2E22D3EE" else "#33808080"
        val outlineColor = if (enabled) "#FF22D3EE" else "#FF808080"

        polygon.fillPaint.color = Color.parseColor(fillColor)
        polygon.fillPaint.style = android.graphics.Paint.Style.FILL
        polygon.outlinePaint.color = Color.parseColor(outlineColor)
        polygon.outlinePaint.strokeWidth = 5f
        return polygon
    }

    // ─── Save geofence ─────────────────────────────────────

    private fun saveGeofence() {
        val name = etName.text.toString().trim()
        val radiusText = etRadius.text.toString().trim()
        val point = selectedPoint

        if (name.isEmpty() || radiusText.isEmpty() || point == null) {
            Toast.makeText(requireContext(), "نام و شعاع را وارد کرده و روی نقشه ضربه بزنید", Toast.LENGTH_SHORT).show()
            return
        }

        val radius = radiusText.toIntOrNull() ?: 100

        viewModel.createGeofence(
            name = name,
            lat = point.latitude,
            lng = point.longitude,
            radius = radius,
            alertOnExit = true,
            alertOnEnter = true,
            exitMethod = "call",
            enterMethod = "sms",
            speedLimit = 0
        )

        etName.text.clear()
        etRadius.text.clear()
        showGeofenceDialog(false)
        selectedPoint = null
        selectedMarker?.let { map.overlays.remove(it) }
        selectedMarker = null
    }

    /** نمایش/مخفی کردن پنل حصار با انیمیشن slide-up */
    private fun showGeofenceDialog(show: Boolean) {
        if (show) {
            dialogGeofence.visibility = View.VISIBLE
            dialogGeofence.translationY = 300f
            dialogGeofence.alpha = 0f
            dialogGeofence.animate()
                .translationY(0f).alpha(1f)
                .setDuration(320)
                .setInterpolator(android.view.animation.DecelerateInterpolator(1.6f))
                .start()
        } else {
            dialogGeofence.animate()
                .translationY(300f).alpha(0f)
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

    // ─── Long press to delete ──────────────────────────────

    private fun showDeleteDialog(geofence: GeofenceViewModel.GeofenceItem) {
        MaterialAlertDialogBuilder(requireContext())
            .setTitle("حذف حصار")
            .setMessage("حصار «${geofence.name}» حذف شود؟")
            .setPositiveButton("حذف") { _, _ ->
                viewModel.deleteGeofence(geofence.id)
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
