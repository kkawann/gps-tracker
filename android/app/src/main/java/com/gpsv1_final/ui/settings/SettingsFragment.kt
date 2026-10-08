package com.gpsv1_final.ui.settings

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.fragment.app.viewModels
import androidx.navigation.fragment.findNavController
import com.gpsv1_final.App
import com.gpsv1_final.R
import com.google.android.material.button.MaterialButton
import com.google.android.material.textfield.TextInputEditText

class SettingsFragment : Fragment() {

    private val viewModel: SettingsViewModel by viewModels()

    private lateinit var tvDeviceName: TextView
    private lateinit var tvDeviceId: TextView
    private lateinit var tvOwnerPhone: TextView
    private lateinit var tvAuthorizedPhones: TextView
    private lateinit var etCustomName: TextInputEditText
    private lateinit var btnSaveName: MaterialButton
    private lateinit var etSpeedLimit: TextInputEditText
    private lateinit var btnSaveSpeedLimit: MaterialButton
    private lateinit var etServerUrl: TextInputEditText
    private lateinit var btnSaveServerUrl: MaterialButton
    private lateinit var etBrokerUrl: TextInputEditText
    private lateinit var btnSaveBrokerUrl: MaterialButton
    private lateinit var btnManagePhones: MaterialButton
    private lateinit var btnLogout: MaterialButton
    private lateinit var phoneDialog: LinearLayout
    private lateinit var etPhones: EditText
    private lateinit var btnSavePhones: MaterialButton
    private lateinit var btnCancelPhones: MaterialButton

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_settings, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        bindViews(view)
        setupListeners()
        observeViewModel()
    }

    private fun bindViews(view: View) {
        tvDeviceName = view.findViewById(R.id.tvDeviceName)
        tvDeviceId = view.findViewById(R.id.tvDeviceId)
        tvOwnerPhone = view.findViewById(R.id.tvOwnerPhone)
        tvAuthorizedPhones = view.findViewById(R.id.tvAuthorizedPhones)
        etCustomName = view.findViewById(R.id.etCustomName)
        btnSaveName = view.findViewById(R.id.btnSaveName)
        etSpeedLimit = view.findViewById(R.id.etSpeedLimit)
        btnSaveSpeedLimit = view.findViewById(R.id.btnSaveSpeedLimit)
        etServerUrl = view.findViewById(R.id.etServerUrl)
        btnSaveServerUrl = view.findViewById(R.id.btnSaveServerUrl)
        etBrokerUrl = view.findViewById(R.id.etBrokerUrl)
        btnSaveBrokerUrl = view.findViewById(R.id.btnSaveBrokerUrl)
        btnManagePhones = view.findViewById(R.id.btnManagePhones)
        btnLogout = view.findViewById(R.id.btnLogout)
        phoneDialog = view.findViewById(R.id.phoneDialog)
        etPhones = view.findViewById(R.id.etPhones)
        btnSavePhones = view.findViewById(R.id.btnSavePhones)
        btnCancelPhones = view.findViewById(R.id.btnCancelPhones)
    }

    private fun setupListeners() {
        btnSaveName.setOnClickListener {
            val name = etCustomName.text.toString().trim()
            if (name.isNotEmpty()) viewModel.saveCustomName(name)
        }

        btnSaveSpeedLimit.setOnClickListener {
            val limit = etSpeedLimit.text.toString().toIntOrNull() ?: 0
            viewModel.saveSpeedLimit(limit)
        }

        btnSaveServerUrl.setOnClickListener {
            val url = etServerUrl.text.toString().trim()
            if (url.isNotEmpty()) viewModel.saveServerUrl(url)
        }

        btnSaveBrokerUrl.setOnClickListener {
            val url = etBrokerUrl.text.toString().trim()
            if (url.isNotEmpty()) viewModel.saveBrokerUrl(url)
        }

        btnManagePhones.setOnClickListener {
            phoneDialog.visibility = View.VISIBLE
        }

        btnSavePhones.setOnClickListener {
            val phonesText = etPhones.text.toString().trim()
            val phones = phonesText.split("\n").map { it.trim() }.filter { it.isNotEmpty() }
            viewModel.saveAuthorizedPhones(phones)
            phoneDialog.visibility = View.GONE
        }

        btnCancelPhones.setOnClickListener {
            phoneDialog.visibility = View.GONE
        }

        btnLogout.setOnClickListener {
            viewModel.logout()
        }
    }

    private fun observeViewModel() {
        viewModel.config.observe(viewLifecycleOwner) { config ->
            config ?: return@observe
            tvDeviceName.text = config.customName
            etCustomName.setText(config.customName)
            tvOwnerPhone.text = config.ownerPhone
            tvAuthorizedPhones.text = "${config.authorizedPhones.size} شماره مجاز"
            etSpeedLimit.setText(if (config.speedLimit > 0) config.speedLimit.toString() else "")
        }

        viewModel.serverUrl.observe(viewLifecycleOwner) { url ->
            etServerUrl.setText(url)
        }

        viewModel.brokerUrl.observe(viewLifecycleOwner) { url ->
            etBrokerUrl.setText(url)
        }

        viewModel.toastMessage.observe(viewLifecycleOwner) { msg ->
            Toast.makeText(requireContext(), msg, Toast.LENGTH_SHORT).show()
        }

        viewModel.logoutEvent.observe(viewLifecycleOwner) {
            (requireActivity().application as App).mqttManager?.disconnect()
            findNavController().navigate(R.id.action_settings_to_auth)
        }

        // نمایش اطلاعات اولیه
        val prefs = requireContext().getSharedPreferences("gps_prefs", android.content.Context.MODE_PRIVATE)
        tvDeviceId.text = prefs.getString("device_uid", "--")
    }
}
