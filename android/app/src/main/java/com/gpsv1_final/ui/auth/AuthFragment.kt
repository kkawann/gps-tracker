package com.gpsv1_final.ui.auth

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.animation.OvershootInterpolator
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.viewModels
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import com.gpsv1_final.App
import com.gpsv1_final.R
import com.gpsv1_final.mqtt.MqttManager
import com.gpsv1_final.service.GpsForegroundService
import com.google.android.material.button.MaterialButton
import com.google.android.material.progressindicator.LinearProgressIndicator
import com.google.android.material.textfield.TextInputEditText
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class AuthFragment : Fragment() {

    private val viewModel: AuthViewModel by viewModels()
    private var mqtt: MqttManager? = null

    private val locationPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        val fine = permissions[Manifest.permission.ACCESS_FINE_LOCATION] ?: false
        val coarse = permissions[Manifest.permission.ACCESS_COARSE_LOCATION] ?: false
        if (fine || coarse) {
            GpsForegroundService.start(requireContext())
        }
    }

    // Step 1: Request OTP
    private lateinit var stepRequestOtp: LinearLayout
    private lateinit var etDeviceUid: TextInputEditText
    private lateinit var etPhone: TextInputEditText
    private lateinit var btnRequestOtp: MaterialButton
    private lateinit var loadingRequest: LinearProgressIndicator

    // Step 2: Verify OTP
    private lateinit var stepVerifyOtp: LinearLayout
    private lateinit var etOtpCode: TextInputEditText
    private lateinit var btnVerifyOtp: MaterialButton
    private lateinit var loadingVerify: LinearProgressIndicator
    private lateinit var tvOtpDeviceUid: TextView

    // Step 3: Register Device
    private lateinit var stepRegister: LinearLayout
    private lateinit var etDeviceName: TextInputEditText
    private lateinit var btnRegister: MaterialButton
    private lateinit var loadingRegister: LinearProgressIndicator

    // Step Indicator
    private lateinit var stepDot1: TextView
    private lateinit var stepDot2: TextView
    private lateinit var stepDot3: TextView
    private lateinit var stepLine1: View
    private lateinit var stepLine2: View

    // Entrance animation state
    private var entranceDone = false
    private var navigationDone = false   // 🔒 جلوگیری از ناوبری دوبل (saved-session + Success state)

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_auth, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        bindViews(view)
        setupListeners()
        observeViewModel()
        playEntrance()

        val saved = viewModel.getSavedSession()
        if (saved != null) {
            connectMqttAndNavigate(saved.first, saved.first)
        }
    }

    private fun bindViews(view: View) {
        stepRequestOtp = view.findViewById(R.id.stepRequestOtp)
        etDeviceUid = view.findViewById(R.id.etDeviceUid)
        etPhone = view.findViewById(R.id.etPhone)
        btnRequestOtp = view.findViewById(R.id.btnRequestOtp)
        loadingRequest = view.findViewById(R.id.loadingRequest)

        stepVerifyOtp = view.findViewById(R.id.stepVerifyOtp)
        etOtpCode = view.findViewById(R.id.etOtpCode)
        btnVerifyOtp = view.findViewById(R.id.btnVerifyOtp)
        loadingVerify = view.findViewById(R.id.loadingVerify)
        tvOtpDeviceUid = view.findViewById(R.id.tvOtpDeviceUid)

        stepRegister = view.findViewById(R.id.stepRegister)
        etDeviceName = view.findViewById(R.id.etDeviceName)
        btnRegister = view.findViewById(R.id.btnRegister)
        loadingRegister = view.findViewById(R.id.loadingRegister)

        stepDot1 = view.findViewById(R.id.stepDot1)
        stepDot2 = view.findViewById(R.id.stepDot2)
        stepDot3 = view.findViewById(R.id.stepDot3)
        stepLine1 = view.findViewById(R.id.stepLine1)
        stepLine2 = view.findViewById(R.id.stepLine2)
    }

    /** ورود سینمایی: لوگو pop → عنوان و زیرنویس بالا می‌آیند → کارت محو ظاهر می‌شود */
    private fun playEntrance() {
        if (entranceDone) return
        entranceDone = true

        val root = view ?: return
        val logo = root.findViewById<View>(R.id.tvLogoMark)
        val title = root.findViewById<View>(R.id.tvLogoTitle)
        val subtitle = root.findViewById<View>(R.id.tvLogoSubtitle)
        val card = root.findViewById<View>(R.id.authCard)

        logo.scaleX = 0f
        logo.scaleY = 0f
        logo.rotation = -30f
        logo.animate()
            .scaleX(1f).scaleY(1f).rotation(0f)
            .setDuration(650)
            .setInterpolator(OvershootInterpolator(1.6f))
            .start()

        title.alpha = 0f
        title.translationY = 24f
        title.animate().alpha(1f).translationY(0f)
            .setStartDelay(200).setDuration(450)
            .setInterpolator(OvershootInterpolator(0.8f))
            .start()

        subtitle.alpha = 0f
        subtitle.translationY = 24f
        subtitle.animate().alpha(1f).translationY(0f)
            .setStartDelay(320).setDuration(450)
            .setInterpolator(OvershootInterpolator(0.8f))
            .start()

        card.alpha = 0f
        card.translationY = 60f
        card.animate().alpha(1f).translationY(0f)
            .setStartDelay(420).setDuration(550)
            .setInterpolator(OvershootInterpolator(0.7f))
            .start()
    }

    private fun setupListeners() {
        btnRequestOtp.setOnClickListener {
            val deviceUid = etDeviceUid.text.toString().trim()
            val phone = etPhone.text.toString().trim()
            viewModel.requestOtp(deviceUid, phone)
        }

        btnVerifyOtp.setOnClickListener {
            val code = etOtpCode.text.toString().trim()
            viewModel.verifyOtp(code)
        }

        btnRegister.setOnClickListener {
            val deviceName = etDeviceName.text.toString().trim()
            val deviceUid = etDeviceUid.text.toString().trim()
            viewModel.registerDevice(deviceName, deviceUid)
        }
    }

    private fun observeViewModel() {
        viewModel.uiState.observe(viewLifecycleOwner) { state ->
            when (state) {
                is AuthViewModel.UiState.LoginEntry -> showStepRequestOtp()
                is AuthViewModel.UiState.OtpSent -> showStepVerifyOtp()
                is AuthViewModel.UiState.DeviceEntry -> showStepRegister()
                is AuthViewModel.UiState.Loading -> showLoading()
                is AuthViewModel.UiState.Success -> connectMqttAndNavigate(state.deviceUid, state.deviceName)
            }
        }
        viewModel.toastMessage.observe(viewLifecycleOwner) { msg ->
            Toast.makeText(requireContext(), msg, Toast.LENGTH_SHORT).show()
        }
    }

    /** تعویض مرحله با انیمیشن slide+fade */
    private fun switchStep(hide: View, show: View) {
        hide.animate().alpha(0f).translationY(-18f).setDuration(160).withEndAction {
            hide.visibility = View.GONE
            show.alpha = 0f
            show.translationY = 26f
            show.visibility = View.VISIBLE
            show.animate().alpha(1f).translationY(0f).setDuration(240)
                .setInterpolator(OvershootInterpolator(0.7f))
                .start()
        }.start()
    }

    private fun showStepRequestOtp() {
        if (stepVerifyOtp.visibility == View.VISIBLE) switchStep(stepVerifyOtp, stepRequestOtp)
        else if (stepRegister.visibility == View.VISIBLE) switchStep(stepRegister, stepRequestOtp)
        else {
            stepRequestOtp.visibility = View.VISIBLE
            stepVerifyOtp.visibility = View.GONE
            stepRegister.visibility = View.GONE
        }
        loadingRequest.visibility = View.GONE
        loadingVerify.visibility = View.GONE
        loadingRegister.visibility = View.GONE
        btnRequestOtp.isEnabled = true
        updateStepIndicator(1)
    }

    private fun showStepVerifyOtp() {
        switchStep(stepRequestOtp, stepVerifyOtp)
        loadingRequest.visibility = View.GONE
        loadingVerify.visibility = View.GONE
        loadingRegister.visibility = View.GONE
        btnVerifyOtp.isEnabled = true
        val deviceUid = etDeviceUid.text.toString().trim()
        tvOtpDeviceUid.text = "دستگاه: $deviceUid"
        updateStepIndicator(2)
    }

    private fun showStepRegister() {
        switchStep(stepVerifyOtp, stepRegister)
        loadingRequest.visibility = View.GONE
        loadingVerify.visibility = View.GONE
        loadingRegister.visibility = View.GONE
        btnRegister.isEnabled = true
        updateStepIndicator(3)
    }

    private fun updateStepIndicator(currentStep: Int) {
        val activeColor = ContextCompat.getColor(requireContext(), R.color.aurora_cyan)
        val doneColor = ContextCompat.getColor(requireContext(), R.color.aurora_green)
        val inactiveColor = ContextCompat.getColor(requireContext(), R.color.text_hint)
        val lineActive = ContextCompat.getColor(requireContext(), R.color.aurora_green)
        val lineInactive = ContextCompat.getColor(requireContext(), R.color.md_theme_outlineVariant)

        stepDot1.setTextColor(if (currentStep >= 1) doneColor else inactiveColor)
        stepDot1.text = if (currentStep > 1) "✓" else "۱"
        stepLine1.setBackgroundColor(if (currentStep > 1) lineActive else lineInactive)

        stepDot2.setTextColor(if (currentStep >= 2) doneColor else inactiveColor)
        stepDot2.text = if (currentStep > 2) "✓" else "۲"
        stepLine2.setBackgroundColor(if (currentStep > 2) lineActive else lineInactive)

        stepDot3.setTextColor(if (currentStep >= 3) activeColor else inactiveColor)
        stepDot3.text = "۳"
    }

    private fun showLoading() {
        when {
            stepRequestOtp.visibility == View.VISIBLE -> {
                loadingRequest.visibility = View.VISIBLE
                btnRequestOtp.isEnabled = false
            }
            stepVerifyOtp.visibility == View.VISIBLE -> {
                loadingVerify.visibility = View.VISIBLE
                btnVerifyOtp.isEnabled = false
            }
            stepRegister.visibility == View.VISIBLE -> {
                loadingRegister.visibility = View.VISIBLE
                btnRegister.isEnabled = false
            }
        }
    }

    private fun connectMqttAndNavigate(deviceUid: String, deviceName: String) {
        if (navigationDone) return   // 🔒 ناوبری فقط یکبار
        navigationDone = true
        loadingRegister.visibility = View.VISIBLE
        loadingRequest.visibility = View.VISIBLE

        val prefs = requireContext().getSharedPreferences("gps_prefs", Context.MODE_PRIVATE)
        val broker = prefs.getString("broker", com.gpsv1_final.BuildConfig.DEFAULT_MQTT_BROKER) ?: com.gpsv1_final.BuildConfig.DEFAULT_MQTT_BROKER

        val app = requireActivity().application as App

        val existingMqtt = app.mqttManager
        if (existingMqtt != null && existingMqtt.isConnected()) {
            mqtt = existingMqtt
            Toast.makeText(requireContext(), "متصل به سرور", Toast.LENGTH_SHORT).show()
            requestLocationPermissionAndStartService()
            navigateSafely()
            return
        }

        mqtt = MqttManager(requireContext().applicationContext)
        app.mqttManager = mqtt

        lifecycleScope.launch {
            withContext(Dispatchers.IO) {
                mqtt!!.connect(broker, deviceUid,
                    prefs.getString("mqtt_user", com.gpsv1_final.BuildConfig.DEFAULT_MQTT_USERNAME) ?: "",
                    prefs.getString("mqtt_pass", com.gpsv1_final.BuildConfig.DEFAULT_MQTT_PASSWORD) ?: "") { success ->
                    activity?.runOnUiThread {
                        if (!isAdded) return@runOnUiThread   // 🔒 fragment مرده — کاری نکن
                        if (success) {
                            Toast.makeText(requireContext(), "متصل به سرور", Toast.LENGTH_SHORT).show()
                            requestLocationPermissionAndStartService()
                        } else {
                            Toast.makeText(requireContext(), "MQTT: اتصال برقرار نشد، حالت آفلاین", Toast.LENGTH_LONG).show()
                        }
                        navigateSafely()
                    }
                }
            }
        }
    }

    /** ناوبری فقط وقتی fragment هنوز attach است */
    private fun navigateSafely() {
        if (!isAdded) return
        try {
            findNavController().navigate(R.id.action_auth_to_dashboard)
        } catch (e: Exception) {
            android.util.Log.w("AuthFragment", "Navigation skipped: ${e.message}")
        }
    }

    private fun requestLocationPermissionAndStartService() {
        val fineGranted = ContextCompat.checkSelfPermission(
            requireContext(), Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
        val coarseGranted = ContextCompat.checkSelfPermission(
            requireContext(), Manifest.permission.ACCESS_COARSE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED

        if (fineGranted || coarseGranted) {
            GpsForegroundService.start(requireContext())
        } else {
            locationPermissionLauncher.launch(
                arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)
            )
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
    }
}
