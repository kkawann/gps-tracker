package com.gpsv1_final.ui.dashboard

import android.animation.AnimatorSet
import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.content.Context
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.animation.BounceInterpolator
import android.view.animation.DecelerateInterpolator
import android.view.animation.OvershootInterpolator
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.viewModels
import com.gpsv1_final.App
import com.gpsv1_final.R
import com.google.android.material.button.MaterialButton
import com.google.android.material.textfield.TextInputEditText
import com.gpsv1_final.ui.widgets.GlowDot
import com.gpsv1_final.ui.widgets.PulseRadar
import com.gpsv1_final.ui.widgets.SpeedGauge
import com.gpsv1_final.ui.widgets.SparklineView
import org.osmdroid.config.Configuration
import org.osmdroid.tileprovider.tilesource.TileSourceFactory
import org.osmdroid.util.GeoPoint
import org.osmdroid.views.MapView
import org.osmdroid.views.overlay.Marker
import org.osmdroid.events.MapListener
import org.osmdroid.events.ScrollEvent
import org.osmdroid.events.ZoomEvent

class DashboardFragment : Fragment() {

    private val viewModel: DashboardViewModel by viewModels()

    // Map
    private lateinit var map: MapView
    private var marker: Marker? = null
    private var pulseRadar: PulseRadar? = null

    // Marker animation state
    private var markerAnim: ValueAnimator? = null
    private var markerShown = false

    // Views
    private lateinit var tvSpeed: TextView
    private lateinit var tvLastUpdate: TextView
    private lateinit var tvWarning: TextView
    private lateinit var tvConnectionStatus: TextView
    private lateinit var viewStatusDot: GlowDot
    private lateinit var tvEngineStatus: TextView
    private lateinit var viewEngineDot: GlowDot
    private lateinit var tvSignalBars: TextView
    private lateinit var tvSatellites: TextView
    private lateinit var btnStartEngine: MaterialButton
    private lateinit var tvRelayLabel: TextView
    private lateinit var btnRecenter: MaterialButton
    private lateinit var speedGauge: SpeedGauge
    private lateinit var sparkline: SparklineView

    // Kill OTP Panel
    private lateinit var killOtpPanel: LinearLayout
    private lateinit var etKillOtp: TextInputEditText
    private lateinit var btnConfirmKill: MaterialButton
    private lateinit var btnCancelKill: MaterialButton

    // Panel entrance animation (run once)
    private var panelAnimated = false

    private var currentLat = 35.6892
    private var currentLng = 51.3890
    private var followVehicle = true

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_dashboard, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        initOsmDroid()
        bindViews(view)
        setupMap()
        setupMqtt()
        setupRelay()
        observeViewModel()
    }

    private fun initOsmDroid() {
        Configuration.getInstance().load(
            requireContext(),
            requireContext().getSharedPreferences("osmdroid", Context.MODE_PRIVATE)
        )
        Configuration.getInstance().userAgentValue = requireContext().packageName
    }

    private fun bindViews(view: View) {
        map = view.findViewById(R.id.map)
        pulseRadar = view.findViewById(R.id.pulseRadar)
        tvSpeed = view.findViewById(R.id.tvSpeed)
        tvLastUpdate = view.findViewById(R.id.tvLastUpdate)
        tvWarning = view.findViewById(R.id.tvWarning)
        tvConnectionStatus = view.findViewById(R.id.tvConnectionStatus)
        viewStatusDot = view.findViewById(R.id.viewStatusDot)
        tvEngineStatus = view.findViewById(R.id.tvEngineStatus)
        viewEngineDot = view.findViewById(R.id.viewEngineDot)
        tvSignalBars = view.findViewById(R.id.tvSignalBars)
        tvSatellites = view.findViewById(R.id.tvSatellites)
        btnStartEngine = view.findViewById(R.id.btnStartEngine)
        tvRelayLabel = view.findViewById(R.id.tvRelayLabel)
        btnRecenter = view.findViewById(R.id.btnRecenter)
        speedGauge = view.findViewById(R.id.speedGauge)
        sparkline = view.findViewById(R.id.sparkline)
        killOtpPanel = view.findViewById(R.id.killOtpPanel)
        etKillOtp = view.findViewById(R.id.etKillOtp)
        btnConfirmKill = view.findViewById(R.id.btnConfirmKill)
        btnCancelKill = view.findViewById(R.id.btnCancelKill)

        btnCancelKill.setOnClickListener { animateKillOtp(false) { viewModel.cancelKillOtp() } }
    }

    private fun setupMap() {
        map.setTileSource(TileSourceFactory.MAPNIK)
        map.setMultiTouchControls(true)
        map.controller.setZoom(15.0)

        val prefs = requireContext().getSharedPreferences("gps_prefs", Context.MODE_PRIVATE)
        val savedLat = prefs.getFloat("last_lat", 0f).toDouble()
        val savedLng = prefs.getFloat("last_lng", 0f).toDouble()
        if (savedLat != 0.0 && savedLng != 0.0) {
            currentLat = savedLat
            currentLng = savedLng
        }

        map.controller.setCenter(GeoPoint(currentLat, currentLng))

        // مارکر موقعیت فعلی
        marker = Marker(map).apply {
            position = GeoPoint(currentLat, currentLng)
            setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
            title = "موقعیت فعلی"
            icon = ContextCompat.getDrawable(requireContext(), R.drawable.ic_marker_vehicle)
            isEnabled = true
            isDraggable = false
            setInfoWindow(null)
        }
        map.overlays.add(marker)
        map.invalidate()

        // Drop-in انیمیشن بعد از layout شدن نقشه
        map.post {
            if (isAdded) dropInMarker()
        }

        // followVehicle: وقتی کاربر دستی پن می‌کند، تعقیب متوقف شود
        map.addMapListener(object : MapListener {
            override fun onScroll(event: ScrollEvent?): Boolean {
                repositionPulseRadar()
                return false
            }
            override fun onZoom(event: ZoomEvent?): Boolean {
                repositionPulseRadar()
                return false
            }
        })

        // تشخیص پن دستی کاربر
        val gestureDetector = android.view.GestureDetector(requireContext(),
            object : android.view.GestureDetector.SimpleOnGestureListener() {
                override fun onScroll(e1: android.view.MotionEvent?, e2: android.view.MotionEvent, dx: Float, dy: Float): Boolean {
                    if (followVehicle) {
                        followVehicle = false
                    }
                    return false
                }
            })
        map.setOnTouchListener { v, ev ->
            gestureDetector.onTouchEvent(ev)
            v.performClick()
            false
        }
    }

    // ─── Marker Animations ──────────────────────────────────

    private fun dropInMarker() {
        val m = marker ?: return
        val target = GeoPoint(currentLat, currentLng)
        val start = GeoPoint(currentLat + 0.006, currentLng)
        markerAnim?.cancel()
        markerAnim = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 700
            interpolator = BounceInterpolator()
            addUpdateListener { anim ->
                val t = anim.animatedValue as Float
                m.position = GeoPoint(
                    start.latitude + (target.latitude - start.latitude) * t,
                    start.longitude + (target.longitude - start.longitude) * t
                )
                map.invalidate()
            }
            start()
        }
        markerShown = true
    }

    private fun glideMarkerTo(newLat: Double, newLng: Double) {
        val m = marker ?: return
        val fromLat = m.position.latitude
        val fromLng = m.position.longitude
        if (Math.abs(fromLat - newLat) < 1e-7 && Math.abs(fromLng - newLng) < 1e-7) return
        markerAnim?.cancel()
        markerAnim = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 450
            interpolator = DecelerateInterpolator()
            addUpdateListener { anim ->
                val t = anim.animatedValue as Float
                m.position = GeoPoint(
                    fromLat + (newLat - fromLat) * t,
                    fromLng + (newLng - fromLng) * t
                )
                map.invalidate()
            }
            start()
        }
    }

    // ─── MQTT Setup ────────────────────────────────────────

    private fun setupMqtt() {
        val mqtt = (requireActivity().application as App).mqttManager
        if (mqtt != null) {
            viewModel.setupMqttCallbacks(mqtt)
            updateConnectionUI(mqtt.isConnected())
        } else {
            updateConnectionUI(false)
            android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                if (isAdded) {
                    val retryMqtt = (requireActivity().application as App).mqttManager
                    viewModel.setupMqttCallbacks(retryMqtt)
                    updateConnectionUI(retryMqtt?.isConnected() == true)
                }
            }, 2000)
        }
    }

    // ─── Relay Buttons ─────────────────────────────────────

    private fun setupRelay() {
        val mqtt = (requireActivity().application as App).mqttManager

        btnStartEngine.setOnClickListener {
            val engineCurrentlyOn = viewModel.engineOn.value == true
            if (engineCurrentlyOn) {
                viewModel.requestKillOtp(mqtt)
            } else {
                viewModel.startEngine(mqtt)
                Toast.makeText(requireContext(), "ارسال دستور روشن شدن...", Toast.LENGTH_SHORT).show()
            }
        }

        btnConfirmKill.setOnClickListener {
            val code = etKillOtp.text.toString().trim()
            if (code.isEmpty()) {
                Toast.makeText(requireContext(), "کد تایید را وارد کنید", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            viewModel.verifyKillOtp(code, mqtt)
        }

        btnRecenter.setOnClickListener {
            followVehicle = true
            map.controller.animateTo(GeoPoint(currentLat, currentLng))
        }
    }

    // ─── Observe ViewModel ─────────────────────────────────

    private fun observeViewModel() {
        viewModel.speed.observe(viewLifecycleOwner) { speed ->
            tvSpeed.text = speed.toInt().toString()
            speedGauge.setSpeed(speed)
            sparkline.addSample(speed)
        }

        viewModel.lastUpdate.observe(viewLifecycleOwner) { time ->
            tvLastUpdate.text = time
        }

        viewModel.satellites.observe(viewLifecycleOwner) { sat ->
            tvSatellites.text = " $sat ماهواره"
        }

        viewModel.signalBars.observe(viewLifecycleOwner) { bars ->
            val filled = "★".repeat(bars)
            val empty = "☆".repeat(4 - bars)
            tvSignalBars.text = filled + empty
            tvSignalBars.setTextColor(
                ContextCompat.getColor(requireContext(),
                    when {
                        bars >= 3 -> R.color.aurora_green
                        bars >= 2 -> R.color.aurora_amber
                        else -> R.color.aurora_red
                    }
                )
            )
        }

        viewModel.engineOn.observe(viewLifecycleOwner) { engineOn ->
            viewEngineDot.setColorRes(if (engineOn) R.color.aurora_green else R.color.aurora_red)
            viewEngineDot.setGlowing(engineOn)
            tvEngineStatus.text = if (engineOn) "روشن" else "خاموش"
            tvEngineStatus.setTextColor(
                ContextCompat.getColor(
                    requireContext(),
                    if (engineOn) R.color.aurora_green else R.color.text_secondary
                )
            )
            updateRelayToggleVisual(engineOn)
        }

        viewModel.mqttConnected.observe(viewLifecycleOwner) { connected ->
            updateConnectionUI(connected)
        }

        viewModel.relayMessage.observe(viewLifecycleOwner) { msg ->
            Toast.makeText(requireContext(), msg, Toast.LENGTH_SHORT).show()
        }

        viewModel.needsOtpForKill.observe(viewLifecycleOwner) { needs ->
            animateKillOtp(needs)
            etKillOtp.setText("")
        }

        viewModel.toastMessage.observe(viewLifecycleOwner) { msg ->
            Toast.makeText(requireContext(), msg, Toast.LENGTH_LONG).show()
        }

        viewModel.relayBusy.observe(viewLifecycleOwner) { busy ->
            btnStartEngine.isEnabled = !busy
            btnStartEngine.alpha = if (busy) 0.5f else 1f
        }

        // Map position updates
        viewModel.lat.observe(viewLifecycleOwner) { lat ->
            currentLat = lat
            updateMapPosition()
        }
        viewModel.lng.observe(viewLifecycleOwner) { lng ->
            currentLng = lng
            updateMapPosition()
        }
    }

    private fun updateRelayToggleVisual(engineOn: Boolean) {
        if (engineOn) {
            btnStartEngine.backgroundTintList =
                ContextCompat.getColorStateList(requireContext(), R.color.glass_button_bg)
            btnStartEngine.setTextColor(
                ContextCompat.getColor(requireContext(), R.color.aurora_green)
            )
            btnStartEngine.strokeColor =
                ContextCompat.getColorStateList(requireContext(), R.color.aurora_green)
            tvRelayLabel.text = "روشن"
        } else {
            btnStartEngine.backgroundTintList =
                ContextCompat.getColorStateList(requireContext(), R.color.glass_button_bg)
            btnStartEngine.setTextColor(
                ContextCompat.getColor(requireContext(), R.color.aurora_red)
            )
            btnStartEngine.strokeColor =
                ContextCompat.getColorStateList(requireContext(), R.color.aurora_red)
            tvRelayLabel.text = "خاموش"
        }
    }

    private fun updateConnectionUI(connected: Boolean) {
        tvConnectionStatus.text = if (connected) "متصل" else "قطع"
        viewStatusDot.setColorRes(if (connected) R.color.aurora_green else R.color.aurora_red)
        viewStatusDot.setGlowing(connected)

        if (!connected) {
            tvWarning.visibility = View.VISIBLE
            tvWarning.text = "⚠ اتصال MQTT قطع شده"
        } else {
            tvWarning.visibility = View.GONE
        }
    }

    private fun updateMapPosition() {
        val point = GeoPoint(currentLat, currentLng)
        if (markerShown) {
            glideMarkerTo(currentLat, currentLng)
        } else {
            marker?.position = point
        }
        if (followVehicle) {
            map.controller.animateTo(point)
        }
        placePulseRadar(point)
        map.invalidate()
    }

    private fun placePulseRadar(point: GeoPoint) {
        val radar = pulseRadar ?: return
        if (radar.width == 0 || radar.height == 0) {
            radar.post { if (isAdded) placePulseRadar(point) }
            return
        }
        val projection = map.projection
        val screenPt = projection.toPixels(point, null)
        radar.visibility = View.VISIBLE
        radar.translationX = screenPt.x - radar.width / 2f
        radar.translationY = screenPt.y - radar.height / 2f
        radar.start()
    }

    private fun repositionPulseRadar() {
        if (!isAdded) return
        val radar = pulseRadar ?: return
        if (radar.visibility != View.VISIBLE) return
        if (radar.width == 0 || radar.height == 0) return
        val projection = map.projection
        val screenPt = projection.toPixels(GeoPoint(currentLat, currentLng), null)
        radar.translationX = screenPt.x - radar.width / 2f
        radar.translationY = screenPt.y - radar.height / 2f
    }

    // ─── Kill OTP Panel animation ───────────────────────────

    private fun animateKillOtp(show: Boolean, onHidden: (() -> Unit)? = null) {
        if (show) {
            killOtpPanel.visibility = View.VISIBLE
            killOtpPanel.alpha = 0f
            killOtpPanel.scaleX = 0.92f
            killOtpPanel.scaleY = 0.92f
            killOtpPanel.animate()
                .alpha(1f).scaleX(1f).scaleY(1f)
                .setDuration(220)
                .setInterpolator(OvershootInterpolator(1.1f))
                .start()
        } else {
            killOtpPanel.animate()
                .alpha(0f).scaleX(0.92f).scaleY(0.92f)
                .setDuration(160)
                .withEndAction {
                    killOtpPanel.visibility = View.GONE
                    onHidden?.invoke()
                }
                .start()
        }
    }

    override fun onResume() {
        super.onResume()
        map.onResume()
        refreshDashboard()
        pulseRadar?.start()

        if (!panelAnimated) {
            panelAnimated = true
            val panel = requireView().findViewById<View>(R.id.speedGauge).parent as View
            panel.translationY = 180f
            panel.alpha = 0f
            panel.animate().translationY(0f).alpha(1f)
                .setDuration(600)
                .setStartDelay(200)
                .setInterpolator(OvershootInterpolator(0.9f))
                .start()

            btnStartEngine.scaleX = 0f
            btnStartEngine.scaleY = 0f
            AnimatorSet().apply {
                playTogether(
                    ObjectAnimator.ofFloat(btnStartEngine, View.SCALE_X, 0f, 1f),
                    ObjectAnimator.ofFloat(btnStartEngine, View.SCALE_Y, 0f, 1f)
                )
                duration = 500
                startDelay = 500
                interpolator = OvershootInterpolator(2f)
                start()
            }
        }
    }

    override fun onDestroyView() {
        markerAnim?.cancel()
        super.onDestroyView()
    }

    override fun onPause() {
        super.onPause()
        map.onPause()
        pulseRadar?.stop()
        markerAnim?.cancel()
    }

    private fun refreshDashboard() {
        val mqtt = (requireActivity().application as App).mqttManager
        viewModel.setupMqttCallbacks(mqtt)
        updateConnectionUI(mqtt?.isConnected() == true)

        val prefs = requireContext().getSharedPreferences("gps_prefs", Context.MODE_PRIVATE)
        val savedLat = prefs.getFloat("last_lat", 0f).toDouble()
        val savedLng = prefs.getFloat("last_lng", 0f).toDouble()
        if (savedLat != 0.0 && savedLng != 0.0) {
            currentLat = savedLat
            currentLng = savedLng
            updateMapPosition()
        }
    }
}
