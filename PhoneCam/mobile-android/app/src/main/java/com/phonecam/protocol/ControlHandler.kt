package com.phonecam.protocol

import android.util.Log
import java.io.InputStream

/**
 * Handles incoming control commands from the PC client.
 */
class ControlHandler {

    companion object {
        private const val TAG = "ControlHandler"
        private const val MAX_CONTROL_PAYLOAD_SIZE = 64 * 1024
    }

    // Control command callbacks
    var onSwitchCamera: (() -> Unit)? = null
    var onToggleFlash: (() -> Unit)? = null
    var onSetResolution: ((width: Int, height: Int) -> Unit)? = null
    var onSetBitrate: ((bitrate: Int) -> Unit)? = null
    var onRequestKeyFrame: (() -> Unit)? = null
    var onAutoFocus: (() -> Unit)? = null
    var onSetAutoFocusMode: ((enabled: Boolean) -> Unit)? = null
    var onSetZoom: ((zoomRatio: Float) -> Unit)? = null
    var onRotateCamera: (() -> Unit)? = null
    var onAudioOn: (() -> Unit)? = null
    var onDisconnect: (() -> Unit)? = null
    var onReverseAudioData: ((data: ByteArray) -> Unit)? = null

    /**
     * Process a control payload received from the PC.
     */
    fun handleControlPacket(payload: ByteArray) {
        if (payload.isEmpty()) return

        when (payload[0]) {
            Protocol.CMD_SWITCH_CAMERA -> {
                Log.i(TAG, "Control: Switch camera")
                onSwitchCamera?.invoke()
            }
            Protocol.CMD_TOGGLE_FLASH -> {
                Log.i(TAG, "Control: Toggle flash")
                onToggleFlash?.invoke()
            }
            Protocol.CMD_SET_RESOLUTION -> {
                if (payload.size >= 5) {
                    val width = ((payload[1].toInt() and 0xFF) shl 8) or (payload[2].toInt() and 0xFF)
                    val height = ((payload[3].toInt() and 0xFF) shl 8) or (payload[4].toInt() and 0xFF)
                    Log.i(TAG, "Control: Set resolution ${width}x${height}")
                    onSetResolution?.invoke(width, height)
                }
            }
            Protocol.CMD_SET_BITRATE -> {
                if (payload.size >= 5) {
                    val bitrate = ((payload[1].toInt() and 0xFF) shl 24) or
                            ((payload[2].toInt() and 0xFF) shl 16) or
                            ((payload[3].toInt() and 0xFF) shl 8) or
                            (payload[4].toInt() and 0xFF)
                    Log.i(TAG, "Control: Set bitrate $bitrate")
                    onSetBitrate?.invoke(bitrate)
                }
            }
            Protocol.CMD_REQUEST_KEYFRAME -> {
                Log.i(TAG, "Control: Request keyframe")
                onRequestKeyFrame?.invoke()
            }
            Protocol.CMD_AUTOFOCUS -> {
                Log.i(TAG, "Control: Auto focus")
                onAutoFocus?.invoke()
            }
            Protocol.CMD_SET_AUTOFOCUS_MODE -> {
                if (payload.size >= 3) {
                    val enabled = (((payload[1].toInt() and 0xFF) shl 8) or
                            (payload[2].toInt() and 0xFF)) != 0
                    Log.i(TAG, "Control: Auto focus mode ${if (enabled) "on" else "off"}")
                    onSetAutoFocusMode?.invoke(enabled)
                }
            }
            Protocol.CMD_SET_ZOOM -> {
                if (payload.size >= 3) {
                    val zoomX100 = ((payload[1].toInt() and 0xFF) shl 8) or (payload[2].toInt() and 0xFF)
                    val zoomRatio = zoomX100 / 100f
                    Log.i(TAG, "Control: Set zoom ${zoomRatio}x")
                    onSetZoom?.invoke(zoomRatio)
                }
            }
            Protocol.CMD_ROTATE_CAMERA -> {
                Log.i(TAG, "Control: Rotate camera")
                onRotateCamera?.invoke()
            }
            Protocol.CMD_AUDIO_ON -> {
                Log.i(TAG, "Control: Audio on")
                onAudioOn?.invoke()
            }
            Protocol.CMD_AUDIO_OFF -> {
                Log.i(TAG, "Control: Audio off ignored; phone microphone stays always on while streaming")
            }
            Protocol.CMD_DISCONNECT -> {
                Log.i(TAG, "Control: Disconnect")
                onDisconnect?.invoke()
            }
        }
    }

    /**
     * Continuously read incoming control packets from input stream.
     * Runs on a separate thread.
     */
    fun listenForCommands(input: InputStream) {
        val headerBuffer = ByteArray(Protocol.HEADER_SIZE)

        try {
            while (true) {
                // Read header
                var totalRead = 0
                while (totalRead < Protocol.HEADER_SIZE) {
                    val read = input.read(headerBuffer, totalRead, Protocol.HEADER_SIZE - totalRead)
                    if (read == -1) return
                    totalRead += read
                }

                val header = Protocol.parseHeader(headerBuffer) ?: continue
                if (header.payloadLength < 0 || header.payloadLength > MAX_CONTROL_PAYLOAD_SIZE) {
                    Log.w(TAG, "Ignoring invalid control payload length: ${header.payloadLength}")
                    return
                }

                // Read payload
                val payload = ByteArray(header.payloadLength)
                totalRead = 0
                while (totalRead < header.payloadLength) {
                    val read = input.read(payload, totalRead, header.payloadLength - totalRead)
                    if (read == -1) return
                    totalRead += read
                }

                when (header.type) {
                    Protocol.TYPE_CONTROL -> handleControlPacket(payload)
                    Protocol.TYPE_REVERSE_AUDIO -> onReverseAudioData?.invoke(payload)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Control listener error", e)
        }
    }
}
