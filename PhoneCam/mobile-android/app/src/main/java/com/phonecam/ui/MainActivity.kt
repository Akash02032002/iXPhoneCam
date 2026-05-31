package com.phonecam.ui

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.util.Size
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.view.PreviewView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import com.phonecam.camera.CameraManager
import com.phonecam.service.StreamingService

class MainActivity : ComponentActivity() {

    private var cameraManager: CameraManager? = null
    private var streamingService: StreamingService? = null
    private var serviceBound = false

    // Observable state
    private val isConnected = mutableStateOf(false)
    private val connectionInfo = mutableStateOf("Starting...")
    private val wifiIp = mutableStateOf("...")
    private val bluetoothAddress = mutableStateOf("...")
    private val port = mutableStateOf(4747)
    private val isFlashOn = mutableStateOf(false)
    private val isPhoneMicEnabled = mutableStateOf(true)
    private val selectedTransport = mutableStateOf("usb")
    private val isServerRunning = mutableStateOf(false)
    private val serverStatusText = mutableStateOf("Tap Connect to start")
    private val cameraRotationDegrees = mutableStateOf(0)
    private var pendingAutoStartServer = false
    private var objectAutoFocusEnabled = false

    // Store preview view ref for camera switching
    private var previewView: PreviewView? = null

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            val service = (binder as StreamingService.LocalBinder).getService()
            streamingService = service
            serviceBound = true

            // Setup callbacks
            service.onConnectionChanged = { connected, info ->
                runOnUiThread {
                    isConnected.value = connected
                    connectionInfo.value = info
                    isPhoneMicEnabled.value = if (connected) service.isAudioEnabled() else true
                    if (connected) {
                        setObjectAutoFocus(false)
                        serverStatusText.value = "PC connected — streaming!"
                    } else if (isServerRunning.value) {
                        setObjectAutoFocus(false)
                        serverStatusText.value = "Server running on port ${service.getPort()}\nListening for connections..."
                    }
                }
            }

            service.onAudioStateChanged = { enabled ->
                runOnUiThread {
                    isPhoneMicEnabled.value = enabled
                }
            }

            service.onSwitchCameraRequested = {
                runOnUiThread { switchCamera() }
            }

            service.onToggleFlashRequested = {
                runOnUiThread { toggleFlash() }
            }

            service.onAutoFocusRequested = {
                runOnUiThread { setObjectAutoFocus(true) }
            }

            service.onSetAutoFocusModeRequested = { enabled ->
                runOnUiThread { setObjectAutoFocus(enabled) }
            }

            service.onSetZoomRequested = { ratio ->
                cameraManager?.setZoom(ratio)
            }

            service.onRotateCameraRequested = {
                runOnUiThread { rotateCamera() }
            }

            service.onSetResolutionRequested = { w, h ->
                runOnUiThread {
                    val pv = previewView ?: return@runOnUiThread
                    cameraManager?.setResolution(w, h, this@MainActivity, pv)
                    if (objectAutoFocusEnabled) {
                        setObjectAutoFocus(true)
                    }
                    val profile = videoProfileFor(w, h)
                    service.videoEncoder.stop()
                    service.videoEncoder.start(
                        resolution = Size(w, h),
                        bitrateValue = profile.bitrate,
                        fps = profile.fps
                    )
                    service.videoEncoder.requestKeyFrame()
                }
            }

            // Don't auto-start server — wait for user to tap Connect
            wifiIp.value = service.getWifiIp()
            bluetoothAddress.value = service.getBluetoothAddress()
            port.value = service.getPort()
            connectionInfo.value = "Ready. Tap Connect to start."

            // Connect encoder to camera
            val initialResolution = cameraManager?.targetResolution ?: Size(640, 480)
            val initialProfile = videoProfileFor(initialResolution.width, initialResolution.height)
            service.videoEncoder.start(
                resolution = initialResolution,
                bitrateValue = initialProfile.bitrate,
                fps = initialProfile.fps
            )

            cameraManager?.onFrameAvailable = { imageProxy ->
                service.onCameraFrame(imageProxy)
            }

            maybeAutoStartServer()
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            streamingService = null
            serviceBound = false
        }
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        val allGranted = permissions.values.all { it }
        if (allGranted) {
            initializeApp()
        } else {
            Toast.makeText(this, "Camera & Audio permissions required", Toast.LENGTH_LONG).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        pendingAutoStartServer = intent?.getBooleanExtra("start_server", false) == true
        window.statusBarColor = android.graphics.Color.parseColor("#1A1A2E")
        window.navigationBarColor = android.graphics.Color.parseColor("#16213E")
        // Keep screen on while streaming (prevents CameraX from pausing)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        requestPermissions()
    }

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        setIntent(intent)
        if (intent?.getBooleanExtra("start_server", false) == true) {
            pendingAutoStartServer = true
            maybeAutoStartServer()
        }
    }

    private fun requestPermissions() {
        val required = mutableListOf(
            Manifest.permission.CAMERA,
            Manifest.permission.RECORD_AUDIO,
        )
        // Bluetooth permissions for Android 12+
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            required.add(Manifest.permission.BLUETOOTH_CONNECT)
            required.add(Manifest.permission.BLUETOOTH_ADVERTISE)
        }
        val allGranted = required.all {
            ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
        }
        if (allGranted) {
            initializeApp()
        } else {
            permissionLauncher.launch(required.toTypedArray())
        }
    }

    private fun initializeApp() {
        cameraManager = CameraManager(this)

        setContent {
            PhoneCamTheme {
                PhoneCamScreen()
            }
        }

        // Start foreground service
        val serviceIntent = Intent(this, StreamingService::class.java)
        ContextCompat.startForegroundService(this, serviceIntent)
        bindService(serviceIntent, serviceConnection, Context.BIND_AUTO_CREATE)
    }

    @Composable
    fun PhoneCamScreen() {
        val connected by isConnected
        val connInfo by connectionInfo
        val ip by wifiIp
        val btAddress by bluetoothAddress
        val portNum by port
        val transport by selectedTransport
        val serverOn by isServerRunning
        val statusText by serverStatusText

        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(Color(0xFF1A1A2E))
                .statusBarsPadding()
                .navigationBarsPadding()
        ) {
            StatusBar(connected, connInfo)

            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
            ) {
                CameraPreview()
            }

            if (!connected) {
                ConnectionPanel(ip, btAddress, portNum, transport, serverOn, statusText)
            }

            ControlPanel()
        }
    }

    @Composable
    fun StatusBar(connected: Boolean, info: String) {
        Surface(
            modifier = Modifier.fillMaxWidth(),
            color = if (connected) Color(0xFF00C853) else Color(0xFF424242),
            shadowElevation = 4.dp
        ) {
            Row(
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    modifier = Modifier
                        .size(10.dp)
                        .background(
                            if (connected) Color.White else Color(0xFFFF5252),
                            CircleShape
                        )
                )
                Spacer(modifier = Modifier.width(10.dp))
                Text(
                    text = if (connected) "Streaming (Audio + Video)" else "Waiting for connection",
                    color = Color.White,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Medium
                )
                Spacer(modifier = Modifier.weight(1f))
                Text(
                    text = "PhoneCam",
                    color = Color.White,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Bold
                )
            }
        }
    }

    @Composable
    fun TransportSelector(currentTransport: String) {
        Surface(
            modifier = Modifier.fillMaxWidth(),
            color = Color(0xFF16213E)
        ) {
            Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
                Text(
                    "Connection Mode",
                    color = Color(0xFF90CAF9),
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Medium
                )
                Spacer(modifier = Modifier.height(4.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    TransportChip("USB", currentTransport == "usb", Modifier.weight(1f)) {
                        selectedTransport.value = "usb"
                    }
                    TransportChip("WiFi", currentTransport == "wifi", Modifier.weight(1f)) {
                        selectedTransport.value = "wifi"
                    }
                    TransportChip("Bluetooth", currentTransport == "bluetooth", Modifier.weight(1f)) {
                        selectedTransport.value = "bluetooth"
                    }
                }
            }
        }
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    fun TransportChip(label: String, selected: Boolean, modifier: Modifier = Modifier, onClick: () -> Unit) {
        FilterChip(
            onClick = onClick,
            modifier = modifier,
            label = { Text(label, fontSize = 12.sp) },
            selected = selected,
            colors = FilterChipDefaults.filterChipColors(
                selectedContainerColor = Color(0xFF4FC3F7),
                selectedLabelColor = Color.Black,
                containerColor = Color(0xFF2D2D30),
                labelColor = Color(0xFFCCCCCC)
            )
        )
    }

    @Composable
    fun CameraPreview() {
        val lifecycleOwner = LocalLifecycleOwner.current
        val rotation by cameraRotationDegrees

        AndroidView(
            factory = { ctx ->
                PreviewView(ctx).also { pv ->
                    pv.implementationMode = PreviewView.ImplementationMode.COMPATIBLE
                    pv.scaleType = PreviewView.ScaleType.FIT_CENTER
                    previewView = pv
                    cameraManager?.startCamera(lifecycleOwner, pv)
                }
            },
            modifier = Modifier
                .fillMaxSize()
                .graphicsLayer(rotationZ = rotation.toFloat())
        )
    }

    @Composable
    fun ConnectionPanel(ip: String, btAddress: String, portNum: Int, transport: String, serverOn: Boolean, statusText: String) {
        Surface(
            modifier = Modifier.fillMaxWidth(),
            color = Color(0xFF10182D),
            shadowElevation = 8.dp
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                TransportSelector(transport)

                Spacer(modifier = Modifier.height(12.dp))

                Text(
                    statusText,
                    color = if (serverOn) Color(0xFF81C784) else Color(0xFFB0BEC5),
                    fontSize = 12.sp,
                    textAlign = TextAlign.Center
                )

                Spacer(modifier = Modifier.height(10.dp))
                @Suppress("DEPRECATION")
                Divider(color = Color(0xFF31405C))
                Spacer(modifier = Modifier.height(10.dp))

                ModeDetails(ip, btAddress, portNum, transport, serverOn)
            }
        }
    }

    @Composable
    fun ModeDetails(ip: String, btAddress: String, portNum: Int, transport: String, serverOn: Boolean) {
        Column(modifier = Modifier.fillMaxWidth()) {
            val pcMode = when (transport) {
                "bluetooth" -> "Bluetooth"
                "wifi" -> "WiFi"
                else -> "USB"
            }

            if (serverOn) {
                Text(
                    "Waiting for PC to connect in $pcMode mode",
                    color = Color(0xFFFFD54F),
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Medium
                )
                Spacer(modifier = Modifier.height(6.dp))
            }

            when (transport) {
                "bluetooth" -> BluetoothAddressInfo(btAddress)
                "wifi" -> {
                    DetailLine("Phone IP", "$ip:$portNum")
                    Text(
                        "Select WiFi in PC PhoneCamUI and enter this address.",
                        color = Color(0xFF8A94A6),
                        fontSize = 10.sp
                    )
                }
                else -> {
                    DetailLine("USB Port", "$portNum")
                    Text(
                        "Connect USB debugging, then select USB in PC PhoneCamUI.",
                        color = Color(0xFF8A94A6),
                        fontSize = 10.sp
                    )
                }
            }

            if (serverOn) {
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    "Then select PhoneCam Virtual Camera in Zoom.",
                    color = Color(0xFFB0BEC5),
                    fontSize = 11.sp
                )
            }
        }
    }

    @Composable
    fun DetailLine(label: String, value: String) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("$label: ", color = Color(0xFF90CAF9), fontSize = 12.sp)
            Text(
                value,
                color = Color(0xFF4FC3F7),
                fontSize = 15.sp,
                fontWeight = FontWeight.Bold
            )
        }
    }

    @Composable
    fun ConnectOverlay(ip: String, btAddress: String, portNum: Int, transport: String, serverOn: Boolean, statusText: String) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(24.dp),
            contentAlignment = Alignment.Center
        ) {
            Card(
                colors = CardDefaults.cardColors(
                    containerColor = Color(0xCC000000)
                ),
                shape = RoundedCornerShape(16.dp)
            ) {
                Column(
                    modifier = Modifier.padding(24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Text(
                        "PhoneCam",
                        color = Color.White,
                        fontSize = 20.sp,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        "Use your phone as a webcam",
                        color = Color(0xFFB0BEC5),
                        fontSize = 12.sp
                    )
                    Spacer(modifier = Modifier.height(16.dp))

                    // ── Big Connect / Disconnect Button ──
                    Button(
                        onClick = {
                            if (serverOn) stopServer() else startServer()
                        },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(56.dp),
                        shape = RoundedCornerShape(16.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (serverOn) Color(0xFFE53935) else Color(0xFF00C853)
                        )
                    ) {
                        Text(
                            text = if (serverOn) "Disconnect" else "Connect",
                            fontSize = 20.sp,
                            fontWeight = FontWeight.Bold,
                            color = Color.White
                        )
                    }

                    Spacer(modifier = Modifier.height(12.dp))

                    // ── Status text ──
                    Text(
                        statusText,
                        color = if (serverOn) Color(0xFF81C784) else Color(0xFFB0BEC5),
                        fontSize = 12.sp,
                        textAlign = TextAlign.Center
                    )

                    if (!serverOn && (transport == "all" || transport == "bluetooth")) {
                        Spacer(modifier = Modifier.height(12.dp))
                        @Suppress("DEPRECATION")
                        Divider(color = Color(0xFF444444))
                        Spacer(modifier = Modifier.height(8.dp))
                        BluetoothAddressInfo(btAddress)
                    }

                    // ── Connection details (shown when server is running) ──
                    if (serverOn) {
                        Spacer(modifier = Modifier.height(12.dp))
                        @Suppress("DEPRECATION")
                        Divider(color = Color(0xFF444444))
                        Spacer(modifier = Modifier.height(12.dp))

                        Text(
                            "Waiting for PC to connect...",
                            color = Color(0xFFFFD54F),
                            fontSize = 13.sp,
                            fontWeight = FontWeight.Medium
                        )

                        Spacer(modifier = Modifier.height(12.dp))

                        // Steps for user
                        Text(
                            "How to use with Zoom / Teams:",
                            color = Color(0xFF90CAF9),
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Medium
                        )
                        Spacer(modifier = Modifier.height(6.dp))

                        val pcMode = when (transport) {
                            "bluetooth" -> "Bluetooth mode"
                            "wifi" -> "USB/WiFi mode"
                            else -> "USB/WiFi or Bluetooth mode"
                        }
                        val steps = listOf(
                            "1. Open PhoneCamUI on your PC",
                            "2. Select $pcMode, click Connect",
                            "3. Open Zoom → Settings → Video",
                            "4. Select \"PhoneCam Virtual Camera\""
                        )
                        steps.forEach { step ->
                            Text(
                                step,
                                color = Color(0xFFB0BEC5),
                                fontSize = 11.sp,
                                modifier = Modifier.fillMaxWidth()
                            )
                        }

                        Spacer(modifier = Modifier.height(12.dp))
                        @Suppress("DEPRECATION")
                        Divider(color = Color(0xFF444444))
                        Spacer(modifier = Modifier.height(8.dp))

                        // WiFi/USB info
                        if (transport == "all" || transport == "wifi") {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text("IP: ", color = Color(0xFF90CAF9), fontSize = 12.sp)
                                Text(
                                    "$ip:$portNum",
                                    color = Color(0xFF4FC3F7),
                                    fontSize = 16.sp,
                                    fontWeight = FontWeight.Bold
                                )
                            }
                            Text(
                                "USB: adb forward tcp:$portNum tcp:$portNum",
                                color = Color(0xFF888888),
                                fontSize = 10.sp
                            )
                        }

                        if (transport == "all" || transport == "bluetooth") {
                            if (transport == "all" || transport == "wifi") {
                                Spacer(modifier = Modifier.height(8.dp))
                            }
                            BluetoothAddressInfo(btAddress)
                        }
                    }
                }
            }
        }
    }

    @Composable
    fun BluetoothAddressInfo(btAddress: String) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("BT MAC: ", color = Color(0xFF90CAF9), fontSize = 12.sp)
            Text(
                btAddress,
                color = if (btAddress.startsWith("Unavailable"))
                    Color(0xFFFFD54F)
                else
                    Color(0xFF4FC3F7),
                fontSize = if (btAddress.startsWith("Unavailable")) 12.sp else 16.sp,
                fontWeight = FontWeight.Bold
            )
        }
        Text(
            if (btAddress.startsWith("Unavailable"))
                "Check Android Settings > About phone > Status > Bluetooth address"
            else
                "Use this address in PC PhoneCamUI Bluetooth mode",
            color = Color(0xFF888888),
            fontSize = 10.sp
        )
    }

    @Composable
    fun ControlPanel() {
        val flashOn by isFlashOn
        val connected by isConnected
        val micEnabled by isPhoneMicEnabled
        val serverOn by isServerRunning

        Surface(
            modifier = Modifier.fillMaxWidth(),
            color = Color(0xFF16213E)
        ) {
            Column(
                modifier = Modifier
                    .padding(16.dp)
                    .fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                ConnectionControlButton(
                    serverOn = serverOn,
                    onClick = {
                        if (serverOn) stopServer() else startServer()
                    }
                )

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    ControlButton(
                        label = "Flip",
                        enabled = true,
                        modifier = Modifier.weight(1f),
                        onClick = { switchCamera() }
                    )

                    ControlButton(
                        label = "Rotate",
                        enabled = true,
                        modifier = Modifier.weight(1f),
                        onClick = { rotateCamera() }
                    )
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    ControlButton(
                        label = if (micEnabled) "Mute Mic" else "Unmute Mic",
                        enabled = connected,
                        modifier = Modifier.weight(1f),
                        onClick = { togglePhoneMic() }
                    )

                    ControlButton(
                        label = if (flashOn) "Flash ON" else "Flash",
                        enabled = cameraManager?.isUsingFrontCamera == false,
                        modifier = Modifier.weight(1f),
                        onClick = { toggleFlash() }
                    )
                }
            }
        }
    }

    @Composable
    fun ConnectionControlButton(serverOn: Boolean, onClick: () -> Unit) {
        Button(
            onClick = onClick,
            modifier = Modifier
                .fillMaxWidth()
                .height(48.dp),
            shape = RoundedCornerShape(12.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = if (serverOn) Color(0xFFE53935) else Color(0xFF00C853)
            )
        ) {
            Text(
                text = if (serverOn) "Disconnect" else "Connect",
                fontSize = 16.sp,
                fontWeight = FontWeight.Bold,
                color = Color.White
            )
        }
    }

    @Composable
    fun ControlButton(label: String, enabled: Boolean, modifier: Modifier = Modifier, onClick: () -> Unit) {
        OutlinedButton(
            onClick = onClick,
            enabled = enabled,
            modifier = modifier.height(44.dp),
            shape = RoundedCornerShape(12.dp),
            colors = ButtonDefaults.outlinedButtonColors(
                contentColor = Color(0xFF90CAF9)
            )
        ) {
            Text(label, fontSize = 12.sp)
        }
    }

    private fun switchCamera() {
        val pv = previewView ?: return

        // Stop encoder before switching so it restarts with the new camera's resolution
        streamingService?.videoEncoder?.stop()

        cameraManager?.switchCamera(this, pv)
        isFlashOn.value = false
        if (objectAutoFocusEnabled) {
            setObjectAutoFocus(true)
        }

        // Restart encoder — it will auto-detect the new camera's actual resolution
        // on the first incoming frame and reconfigure if needed
        val resolution = cameraManager?.targetResolution ?: Size(640, 480)
        val profile = videoProfileFor(resolution.width, resolution.height)
        streamingService?.videoEncoder?.start(
            resolution = resolution,
            bitrateValue = profile.bitrate,
            fps = profile.fps
        )

        // Request a keyframe so the PC client can decode immediately
        streamingService?.videoEncoder?.requestKeyFrame()
    }

    private fun setObjectAutoFocus(enabled: Boolean) {
        objectAutoFocusEnabled = enabled
        val pv = previewView ?: return
        cameraManager?.setObjectAutoFocus(enabled, pv)
    }

    private fun rotateCamera() {
        val nextRotation = (cameraRotationDegrees.value + 90) % 360
        cameraRotationDegrees.value = nextRotation
        streamingService?.videoEncoder?.setRotationDegrees(nextRotation)
        streamingService?.videoEncoder?.requestKeyFrame()
    }

    private data class VideoProfile(val bitrate: Int, val fps: Int)

    private fun videoProfileFor(width: Int, height: Int): VideoProfile {
        return when {
            width >= 1920 || height >= 1080 -> VideoProfile(bitrate = 6_000_000, fps = 30)
            width >= 1280 || height >= 720 -> VideoProfile(bitrate = 3_500_000, fps = 30)
            else -> VideoProfile(bitrate = 800_000, fps = 10)
        }
    }

    /**
     * Start the streaming server and listen for PC connections.
     */
    private fun startServer() {
        val service = streamingService ?: return
        val transport = selectedTransport.value
        val useWifi = transport == "usb" || transport == "wifi"
        val useBluetooth = transport == "bluetooth"

        service.startServer(useWifi = useWifi, useBluetooth = useBluetooth)

        wifiIp.value = service.getWifiIp()
        bluetoothAddress.value = service.getBluetoothAddress()
        port.value = service.getPort()
        connectionInfo.value = service.getConnectionInfo()
        isServerRunning.value = true
        serverStatusText.value = "Server running on port ${service.getPort()}\nListening for connections..."
    }

    private fun maybeAutoStartServer() {
        if (!pendingAutoStartServer || isServerRunning.value) return
        if (!serviceBound || streamingService == null || cameraManager == null) return

        pendingAutoStartServer = false
        selectedTransport.value = "usb"
        startServer()
    }

    /**
     * Stop the streaming server and disconnect any PC client.
     */
    private fun stopServer() {
        val service = streamingService ?: return
        service.stopServer()
        setObjectAutoFocus(false)
        isServerRunning.value = false
        isConnected.value = false
        isPhoneMicEnabled.value = true
        serverStatusText.value = "Tap Connect to start"
        connectionInfo.value = "Disconnected"
    }

    private fun togglePhoneMic() {
        val service = streamingService ?: return
        val enabled = service.setAudioEnabled(!isPhoneMicEnabled.value)
        isPhoneMicEnabled.value = enabled
    }

    private fun toggleFlash() {
        val result = cameraManager?.toggleFlash() ?: false
        isFlashOn.value = result
    }

    override fun onDestroy() {
        if (serviceBound) {
            unbindService(serviceConnection)
            serviceBound = false
        }
        cameraManager?.shutdown()
        super.onDestroy()
    }
}

@Composable
fun PhoneCamTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Color(0xFF4FC3F7),
            secondary = Color(0xFF00C853),
            background = Color(0xFF1A1A2E),
            surface = Color(0xFF16213E)
        ),
        content = content
    )
}
