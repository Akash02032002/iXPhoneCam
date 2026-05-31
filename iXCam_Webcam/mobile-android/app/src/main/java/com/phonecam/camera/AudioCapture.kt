package com.phonecam.camera

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Log
import androidx.core.app.ActivityCompat
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Captures raw PCM audio from the microphone.
 * Audio is sent as raw PCM 16-bit, 44100Hz, mono.
 */
class AudioCapture(private val context: Context) {

    companion object {
        private const val TAG = "AudioCapture"
        const val SAMPLE_RATE = 44100
        const val CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_MONO
        const val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_16BIT
    }

    private var audioRecord: AudioRecord? = null
    private var captureThread: Thread? = null
    private val isRunning = AtomicBoolean(false)

    var onAudioData: ((data: ByteArray, size: Int) -> Unit)? = null

    val bufferSize: Int by lazy {
        AudioRecord.getMinBufferSize(SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT) * 2
    }

    fun start(): Boolean {
        if (ActivityCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO)
            != PackageManager.PERMISSION_GRANTED
        ) {
            Log.e(TAG, "RECORD_AUDIO permission not granted")
            return false
        }

        try {
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.MIC,
                SAMPLE_RATE,
                CHANNEL_CONFIG,
                AUDIO_FORMAT,
                bufferSize
            )

            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                Log.e(TAG, "AudioRecord failed to initialize")
                return false
            }

            audioRecord?.startRecording()
            isRunning.set(true)

            captureThread = Thread({
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_AUDIO)
                val buffer = ByteArray(bufferSize)

                while (isRunning.get()) {
                    val bytesRead = audioRecord?.read(buffer, 0, buffer.size) ?: -1
                    if (bytesRead > 0) {
                        onAudioData?.invoke(buffer.copyOf(bytesRead), bytesRead)
                    }
                }
            }, "AudioCaptureThread").also { it.start() }

            Log.i(TAG, "Audio capture started: ${SAMPLE_RATE}Hz, bufferSize=$bufferSize")
            return true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start audio capture", e)
            return false
        }
    }

    fun stop() {
        isRunning.set(false)
        captureThread?.join(2000)
        captureThread = null
        try {
            audioRecord?.stop()
            audioRecord?.release()
        } catch (e: Exception) {
            Log.e(TAG, "Error stopping audio", e)
        }
        audioRecord = null
        Log.i(TAG, "Audio capture stopped")
    }
}
