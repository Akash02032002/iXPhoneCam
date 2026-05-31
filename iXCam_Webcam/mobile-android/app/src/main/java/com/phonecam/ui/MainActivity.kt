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
    private val port = mutableStateOf(4747)
    private val isFlashOn = mutableStateOf(false)
    private val isAudioEnabled = mutableStateOf(true)
    private val selectedTransport = mutableStateOf("all")
    private val isServerRunning = mutableStateOf(false)
    private val serverStatusText = mutableStateOf("Tap Connect to start")
    private val cameraRotationDegrees = mutableStateOf(0)
    private var pendingAutoStartServer = false

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
                    if (connected) {
                        serverStatusText.value = "PC connected — streaming!"
                    } else if (isServerRunning.value) {
                        serverStatusText.value = "Server running on port ${service.getPort()}\nListening for connections..."
                    }
                }
            }

            service.onSwitchCameraRequested = {
                runOnUiThread { switchCamera() }
            }

            service.onToggleFlashRequested = {
                runOnUiThread { toggleFlash() }
            }

            service.onAutoFocusRequested = {
                cameraManager?.triggerAutoFocus()
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
                    // Restart encoder for the new resolution
                    service.videoEncoder.stop()
                    service.videoEncoder.start(resolution = Size(w, h))
                    service.videoEncoder.requestKeyFrame()
                }
            }

            // Don't auto-start server — wait for user to tap Connect
            wifiIp.value = service.getWifiIp()
            port.value = service.getPort()
            connectionInfo.value = "Ready. Tap Connect to start."

            // Connect encoder to camera
            service.videoEncoder.start(
                resolution = cameraManager?.targetResolution ?: Size(1280, 720)
            )

            cameraManager?.onFrameAvailable = { imageProxy ->
                service.onCameraFrame(imageProxy)
            }

            // Sync audio state with UI
            isAudioEnabled.value = service.isAudioEnabled()
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
        val portNum by port
        val transport by selectedTransport
        val serverOn by isServerRunning
        val statusText by serverStatusText

        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(Color(0xFF1A1A2E))
        ) {
            StatusBar(connected, connInfo)

            // Transport mode selector (only when server is NOT running)
            if (!serverOn && !connected) {
                TransportSelector(transport)
            }

            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
            ) {
                CameraPreview()

                if (!connected) {
                    ConnectOverlay(ip, portNum, transport, serverOn, statusText)
                }
            }

            ControlPanel(connected)
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
                    horizontalArrangement = Arrangement.SpaceEvenly
                ) {
                    TransportChip("All", currentTransport == "all") { selectedTransport.value = "all" }
                    TransportChip("USB/WiFi", currentTransport == "wifi") { selectedTransport.value = "wifi" }
                    TransportChip("Bluetooth", currentTransport == "bluetooth") { selectedTransport.value = "bluetooth" }
                }
            }
        }
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    fun TransportChip(label: String, selected: Boolean, onClick: () -> Unit) {
        FilterChip(
            onClick = onClick,
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
                    pv.implementationMode = PreviewView.ImplementationMode.PERFORMANCE
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
    fun ConnectOverlay(ip: String, portNum: Int, transport: String, serverOn: Boolean, statusText: String) {
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

                        val steps = listOf(
                            "1. Open PhoneCamUI on your PC",
                            "2. Select USB mode, click Connect",
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
                    }
                }
            }
        }
    }

    @Composable
    fun ControlPanel(connected: Boolean) {
        val flashOn by isFlashOn
        val audioOn by isAudioEnabled

        Surface(
            modifier = Modifier.fillMaxWidth(),
            color = Color(0xFF16213E)
        ) {
            Row(
                modifier = Modifier
                    .padding(16.dp)
                    .fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically
            ) {
                ControlButton(
                    label = "Flip",
                    enabled = true,
                    onClick = { switchCamera() }
                )

                ControlButton(
                    label = "Rotate",
                    enabled = true,
                    onClick = { rotateCamera() }
                )

                ControlButton(
                    label = if (flashOn) "Flash ON" else "Flash",
                    enabled = cameraManager?.isUsingFrontCamera == false,
                    onClick = { toggleFlash() }
                )

                ControlButton(
                    label = if (audioOn) "Mute" else "Unmute",
                    enabled = connected,
                    onClick = { toggleAudio() }
                )
            }
        }
    }

    @Composable
    fun ControlButton(label: String, enabled: Boolean, onClick: () -> Unit) {
        OutlinedButton(
            onClick = onClick,
            enabled = enabled,
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

        // Restart encoder — it will auto-detect the new camera's actual resolution
        // on the first incoming frame and reconfigure if needed
        streamingService?.videoEncoder?.start(
            resolution = cameraManager?.targetResolution ?: Size(1280, 720)
        )

        // Request a keyframe so the PC client can decode immediately
        streamingService?.videoEncoder?.requestKeyFrame()
    }

    private fun rotateCamera() {
        val nextRotation = (cameraRotationDegrees.value + 90) % 360
        cameraRotationDegrees.value = nextRotation
        streamingService?.videoEncoder?.setRotationDegrees(nextRotation)
        streamingService?.videoEncoder?.requestKeyFrame()
    }

    /**
     * Start the streaming server and listen for PC connections.
     */
    private fun startServer() {
        val service = streamingService ?: return
        val transport = selectedTransport.value
        val useWifi = transport == "all" || transport == "wifi"
        val useBluetooth = transport == "all" || transport == "bluetooth"

        service.startServer(useWifi = useWifi, useBluetooth = useBluetooth)

        wifiIp.value = service.getWifiIp()
        port.value = service.getPort()
        connectionInfo.value = service.getConnectionInfo()
        isServerRunning.value = true
        serverStatusText.value = "Server running on port ${service.getPort()}\nListening for connections..."
    }

    private fun maybeAutoStartServer() {
        if (!pendingAutoStartServer || isServerRunning.value) return
        if (!serviceBound || streamingService == null || cameraManager == null) return

        pendingAutoStartServer = false
        selectedTransport.value = "wifi"
        startServer()
    }

    /**
     * Stop the streaming server and disconnect any PC client.
     */
    private fun stopServer() {
        val service = streamingService ?: return
        service.stopServer()
        isServerRunning.value = false
        isConnected.value = false
        serverStatusText.value = "Tap Connect to start"
        connectionInfo.value = "Disconnected"
    }

    private fun toggleFlash() {
        val result = cameraManager?.toggleFlash() ?: false
        isFlashOn.value = result
    }

    private fun toggleAudio() {
        if (isAudioEnabled.value) {
            streamingService?.stopAudio()
            isAudioEnabled.value = false
        } else {
            streamingService?.startAudio()
            isAudioEnabled.value = true
        }
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
