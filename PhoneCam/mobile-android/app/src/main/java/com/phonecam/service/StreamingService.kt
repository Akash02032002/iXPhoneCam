package com.phonecam.service

import android.app.Notification
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.Binder
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import android.util.Size
import androidx.camera.core.ImageProxy
import androidx.core.app.NotificationCompat
import com.phonecam.PhoneCamApp
import com.phonecam.R
import com.phonecam.camera.AudioCapture
import com.phonecam.camera.PhoneSpeakerPlayer
import com.phonecam.camera.VideoEncoder
import com.phonecam.protocol.ControlHandler
import com.phonecam.protocol.Protocol
import com.phonecam.transport.BluetoothTransport
import com.phonecam.transport.TransportClient
import com.phonecam.transport.TransportListener
import com.phonecam.transport.TransportServer
import com.phonecam.transport.UsbWifiTransport
import com.phonecam.ui.MainActivity
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/**
 * Foreground service that manages the streaming pipeline.
 *
 * Lifecycle:
 * 1. Service starts → starts transport server (USB/WiFi/BT)
 * 2. PC client connects → starts encoder
 * 3. Camera frames → encoder → protocol → transport → PC
 * 4. PC sends control commands → control handler → camera adjustments
 * 5. PC disconnects → stops encoder, waits for new connection
 */
class StreamingService : Service() {

    companion object {
        private const val TAG = "StreamingService"
        private const val NOTIFICATION_ID = 1001
        private const val VIDEO_QUEUE_CAPACITY = 4
        private const val AUDIO_QUEUE_CAPACITY = 24
        private const val SEND_QUEUE_POLL_MS = 50L
        private const val HEARTBEAT_INTERVAL_MS = 2_000L
        private const val WAKE_LOCK_TIMEOUT_MS = 10 * 60 * 1000L
    }

    private val binder = LocalBinder()
    private var wakeLock: PowerManager.WakeLock? = null

    // Pipeline components
    val videoEncoder = VideoEncoder()
    val audioCapture by lazy { AudioCapture(this) }
    private val phoneSpeakerPlayer by lazy { PhoneSpeakerPlayer(this) }
    private val controlHandler = ControlHandler()

    // Transport
    private var usbWifiTransport: UsbWifiTransport? = null
    private var bluetoothTransport: BluetoothTransport? = null
    private var activeTransport: TransportServer? = null

    // Connection state
    private val connectedClient = AtomicReference<TransportClient?>(null)
    private val isStreaming = AtomicBoolean(false)
    private var controlListenerThread: Thread? = null
    private var heartbeatThread: Thread? = null
    private val isAudioActive = AtomicBoolean(false)

    // One sender thread owns the socket, but media is queued by priority.
    // Video must not be starved by audio/heartbeat packets or Zoom sees a
    // live camera device with no fresh frames.
    private sealed class SendPacket {
        data class Video(val data: ByteArray, val timestampUs: Long, val isKeyFrame: Boolean) : SendPacket()
        data class Audio(val data: ByteArray, val size: Int, val timestampUs: Long) : SendPacket()
        object Heartbeat : SendPacket()
    }
    private val videoQueue = ArrayBlockingQueue<SendPacket.Video>(VIDEO_QUEUE_CAPACITY)
    private val audioQueue = ArrayBlockingQueue<SendPacket.Audio>(AUDIO_QUEUE_CAPACITY)
    private val heartbeatPending = AtomicBoolean(false)
    private var senderThread: Thread? = null

    // Stats
    val framesSent = AtomicLong(0)
    val bytesSent = AtomicLong(0)

    // Callbacks to UI
    var onConnectionChanged: ((connected: Boolean, info: String) -> Unit)? = null
    var onStatsUpdated: ((fps: Int, kbps: Int) -> Unit)? = null
    var onAudioStateChanged: ((enabled: Boolean) -> Unit)? = null

    // Control command callbacks (forwarded to activity)
    var onSwitchCameraRequested: (() -> Unit)? = null
    var onToggleFlashRequested: (() -> Unit)? = null
    var onSetResolutionRequested: ((width: Int, height: Int) -> Unit)? = null
    var onAutoFocusRequested: (() -> Unit)? = null
    var onSetAutoFocusModeRequested: ((enabled: Boolean) -> Unit)? = null
    var onSetZoomRequested: ((zoomRatio: Float) -> Unit)? = null
    var onRotateCameraRequested: (() -> Unit)? = null

    inner class LocalBinder : Binder() {
        fun getService(): StreamingService = this@StreamingService
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        setupControlHandler()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        startForeground(NOTIFICATION_ID, createNotification("Waiting for connection..."))
        acquireWakeLock()
        return START_STICKY
    }

    /**
     * Start the transport server(s).
     */
    fun startServer(useWifi: Boolean = true, useBluetooth: Boolean = false) {
        val transportListener = object : TransportListener {
            override fun onClientConnected(client: TransportClient) {
                handleClientConnected(client)
            }

            override fun onClientDisconnected(client: TransportClient) {
                handleClientDisconnected()
            }

            override fun onError(error: String) {
                Log.e(TAG, "Transport error: $error")
            }
        }

        if (useWifi) {
            usbWifiTransport = UsbWifiTransport(this).apply {
                listener = transportListener
                start()
            }
            activeTransport = usbWifiTransport
        }

        if (useBluetooth) {
            bluetoothTransport = BluetoothTransport(this).apply {
                listener = transportListener
                start()
            }
            if (activeTransport == null) activeTransport = bluetoothTransport
        }

        Log.i(TAG, "Server started. ${getConnectionInfo()}")
    }

    private fun handleClientConnected(client: TransportClient) {
        stopConnectionThreads()
        isStreaming.set(false)
        stopAudio()
        phoneSpeakerPlayer.stop()
        clearSendQueues()

        // Now set up the new connection
        connectedClient.set(client)
        isStreaming.set(true)

        // Start encoder callback - puts frames in queue (non-blocking)
        videoEncoder.onEncodedFrame = { data, timestamp, isKeyFrame ->
            enqueueVideoFrame(data, timestamp, isKeyFrame)
        }

        // Start the unified sender thread (reads from queue, writes to socket)
        startSenderThread()

        // Request a keyframe so SPS/PPS + IDR are sent immediately to the new client
        videoEncoder.requestKeyFrame()

        // Start listening for control commands
        val currentClient = client
        controlListenerThread = Thread({
            controlHandler.listenForCommands(currentClient.inputStream)
            Log.i(TAG, "Control listener stopped for ${currentClient.description}")
        }, "ControlListener").also { it.start() }

        // Start heartbeat
        startHeartbeat()

        // Auto-start audio capture alongside video
        startAudio()

        updateNotification("Streaming to: ${client.description}")
        onConnectionChanged?.invoke(true, client.description)
        Log.i(TAG, "Client connected: ${client.description}")
    }

    private fun handleClientDisconnected() {
        if (!isStreaming.compareAndSet(true, false)) return

        // Stop audio when disconnected
        stopAudio()
        phoneSpeakerPlayer.stop()

        stopConnectionThreads()
        clearSendQueues()

        connectedClient.set(null)

        updateNotification("Waiting for connection...")
        onConnectionChanged?.invoke(false, "Disconnected")
        Log.i(TAG, "Client disconnected")
    }

    private fun stopConnectionThreads() {
        controlListenerThread?.interrupt()
        controlListenerThread = null
        senderThread?.interrupt()
        senderThread = null
        heartbeatThread?.interrupt()
        heartbeatThread = null
    }

    private fun clearSendQueues() {
        videoQueue.clear()
        audioQueue.clear()
        heartbeatPending.set(false)
    }

    /**
     * Enqueue a video frame for sending. Non-blocking: drops oldest frame if queue is full.
     */
    private fun enqueueVideoFrame(data: ByteArray, timestampUs: Long, isKeyFrame: Boolean) {
        val packet = SendPacket.Video(data, timestampUs, isKeyFrame)
        if (isKeyFrame) {
            videoQueue.clear()
        }
        if (!videoQueue.offer(packet)) {
            // Queue full - drop oldest to keep latency low.
            videoQueue.poll()
            videoQueue.offer(packet)
        }
    }

    /**
     * Enqueue audio data for sending. Non-blocking: drop if queue is full.
     */
    private fun enqueueAudio(data: ByteArray, size: Int) {
        val packet = SendPacket.Audio(data.copyOf(size), size, System.nanoTime() / 1000)
        if (!audioQueue.offer(packet)) {
            audioQueue.poll()
            audioQueue.offer(packet)
        }
    }

    /**
     * Unified sender thread that owns the socket and writes packets by priority.
     */
    private fun startSenderThread() {
        senderThread = Thread({
            Log.i(TAG, "Sender: thread started")
            var consecutiveVideoPackets = 0
            while (isStreaming.get()) {
                try {
                    val packet = when {
                        audioQueue.size >= 3 -> {
                            consecutiveVideoPackets = 0
                            audioQueue.poll()
                        }
                        consecutiveVideoPackets >= 2 && audioQueue.isNotEmpty() -> {
                            consecutiveVideoPackets = 0
                            audioQueue.poll()
                        }
                        else -> {
                            videoQueue.poll()
                                ?: audioQueue.poll()
                                ?: if (heartbeatPending.compareAndSet(true, false)) SendPacket.Heartbeat else null
                        }
                    }

                    if (packet == null) {
                        Thread.sleep(SEND_QUEUE_POLL_MS)
                        continue
                    }

                    val client = connectedClient.get() ?: break
                    val out = client.outputStream

                    when (packet) {
                        is SendPacket.Video -> {
                            consecutiveVideoPackets++
                            Protocol.writeVideoFrame(out, packet.data, packet.timestampUs, packet.isKeyFrame)
                            val sent = videoFramesSentOk.incrementAndGet()
                            if (sent % 100 == 0L) {
                                Log.i(TAG, "Sender: video #$sent (${packet.data.size}B, key=${packet.isKeyFrame}, vq=${videoQueue.size}, aq=${audioQueue.size})")
                            }
                            framesSent.incrementAndGet()
                            bytesSent.addAndGet(packet.data.size.toLong())
                        }
                        is SendPacket.Audio -> {
                            consecutiveVideoPackets = 0
                            Protocol.writeAudioData(out, packet.data, packet.size, packet.timestampUs)
                        }
                        is SendPacket.Heartbeat -> {
                            consecutiveVideoPackets = 0
                            Protocol.writeHeartbeat(out)
                        }
                    }
                } catch (e: InterruptedException) {
                    break
                } catch (e: Exception) {
                    Log.e(TAG, "Sender: send failed", e)
                    handleClientDisconnected()
                    break
                }
            }
            Log.i(TAG, "Sender: thread stopped")
        }, "Sender").also { it.isDaemon = true; it.start() }
    }

    // Debug counters
    private val cameraFramesReceived = java.util.concurrent.atomic.AtomicLong(0)
    private val videoFramesSentOk = java.util.concurrent.atomic.AtomicLong(0)

    /**
     * Called by CameraManager's frame callback.
     */
    fun onCameraFrame(imageProxy: ImageProxy) {
        val count = cameraFramesReceived.incrementAndGet()
        if (isStreaming.get()) {
            videoEncoder.encodeFrame(imageProxy)
        } else {
            imageProxy.close()
            if (count % 30 == 0L) {
                Log.w(TAG, "onCameraFrame: not streaming, discarding frame #$count")
            }
        }
    }

    /**
     * Start audio streaming.
     */
    fun startAudio() {
        if (isAudioActive.get()) return
        audioCapture.onAudioData = sendAudio@{ data, size ->
            if (connectedClient.get() == null) return@sendAudio
            enqueueAudio(data, size)
        }
        if (audioCapture.start()) {
            isAudioActive.set(true)
            onAudioStateChanged?.invoke(true)
            Log.i(TAG, "Phone microphone streaming started")
        } else {
            isAudioActive.set(false)
            onAudioStateChanged?.invoke(false)
            Log.w(TAG, "Phone microphone streaming failed to start")
        }
    }

    fun stopAudio() {
        audioCapture.stop()
        isAudioActive.set(false)
        onAudioStateChanged?.invoke(false)
        Log.i(TAG, "Phone microphone streaming stopped")
    }

    fun setAudioEnabled(enabled: Boolean): Boolean {
        if (enabled) {
            if (connectedClient.get() == null) {
                onAudioStateChanged?.invoke(false)
                return false
            }
            startAudio()
        } else {
            stopAudio()
        }
        return isAudioActive.get()
    }

    fun isAudioEnabled(): Boolean = isAudioActive.get()

    private fun startHeartbeat() {
        heartbeatThread = Thread({
            while (isStreaming.get()) {
                try {
                    Thread.sleep(HEARTBEAT_INTERVAL_MS)
                    heartbeatPending.set(true)
                } catch (e: InterruptedException) {
                    break
                }
            }
        }, "Heartbeat").also { it.isDaemon = true; it.start() }
    }

    private fun setupControlHandler() {
        controlHandler.onSwitchCamera = { onSwitchCameraRequested?.invoke() }
        controlHandler.onToggleFlash = { onToggleFlashRequested?.invoke() }
        controlHandler.onSetResolution = { w, h -> onSetResolutionRequested?.invoke(w, h) }
        controlHandler.onRequestKeyFrame = { videoEncoder.requestKeyFrame() }
        controlHandler.onAutoFocus = { onAutoFocusRequested?.invoke() }
        controlHandler.onSetAutoFocusMode = { enabled -> onSetAutoFocusModeRequested?.invoke(enabled) }
        controlHandler.onSetBitrate = { bitrate -> videoEncoder.setBitrate(bitrate) }
        controlHandler.onSetZoom = { ratio -> onSetZoomRequested?.invoke(ratio) }
        controlHandler.onRotateCamera = { onRotateCameraRequested?.invoke() }
        controlHandler.onAudioOn = { startAudio() }
        controlHandler.onDisconnect = { handleClientDisconnected() }
        controlHandler.onReverseAudioData = { data ->
            if (phoneSpeakerPlayer.start()) {
                phoneSpeakerPlayer.play(data)
            }
        }
    }

    fun getConnectionInfo(): String {
        val parts = mutableListOf<String>()
        usbWifiTransport?.let { parts.add(it.getConnectionInfo()) }
        bluetoothTransport?.let { parts.add(it.getConnectionInfo()) }
        return parts.joinToString("\n")
    }

    fun getWifiIp(): String = usbWifiTransport?.getWifiIpAddress() ?: "N/A"
    fun getBluetoothAddress(): String =
        bluetoothTransport?.getLocalBluetoothAddress()
            ?: BluetoothTransport.getLocalBluetoothAddress(this)
    fun getPort(): Int = Protocol.DEFAULT_PORT
    fun isClientConnected(): Boolean = connectedClient.get() != null

    /**
     * Stop the transport servers and disconnect current client.
     */
    fun stopServer() {
        handleClientDisconnected()
        usbWifiTransport?.stop()
        usbWifiTransport = null
        bluetoothTransport?.stop()
        bluetoothTransport = null
        activeTransport = null
        updateNotification("Waiting for connection...")
        Log.i(TAG, "Server stopped by user")
    }

    private fun createNotification(text: String): Notification {
        val intent = Intent(this, MainActivity::class.java)
        val pendingIntent = PendingIntent.getActivity(
            this, 0, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, PhoneCamApp.CHANNEL_ID)
            .setContentTitle("PhoneCam")
            .setContentText(text)
            .setSmallIcon(R.drawable.ic_camera)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setSilent(true)
            .build()
    }

    private fun updateNotification(text: String) {
        val notification = createNotification(text)
        val manager = getSystemService(NOTIFICATION_SERVICE) as android.app.NotificationManager
        manager.notify(NOTIFICATION_ID, notification)
    }

    private fun acquireWakeLock() {
        val pm = getSystemService(POWER_SERVICE) as PowerManager
        wakeLock = pm.newWakeLock(
            PowerManager.PARTIAL_WAKE_LOCK,
            "PhoneCam::StreamingWakeLock"
        ).apply {
            acquire(WAKE_LOCK_TIMEOUT_MS)
        }
    }

    override fun onDestroy() {
        isStreaming.set(false)
        stopConnectionThreads()
        clearSendQueues()
        videoEncoder.stop()
        audioCapture.stop()
        phoneSpeakerPlayer.stop()
        usbWifiTransport?.stop()
        bluetoothTransport?.stop()
        wakeLock?.release()
        super.onDestroy()
        Log.i(TAG, "Service destroyed")
    }
}
