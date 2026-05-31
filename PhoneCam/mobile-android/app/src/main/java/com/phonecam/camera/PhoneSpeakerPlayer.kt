package com.phonecam.camera

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.util.Log
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Plays reverse-streamed PCM audio from the PC through the phone speaker.
 * Audio format: 44100 Hz, 16-bit, mono.
 */
class PhoneSpeakerPlayer(private val context: Context) {

    companion object {
        private const val TAG = "PhoneSpeakerPlayer"
        const val SAMPLE_RATE = 44100
        const val CHANNEL_CONFIG = AudioFormat.CHANNEL_OUT_MONO
        const val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_16BIT
        private const val QUEUE_CAPACITY = 8
    }

    private val isRunning = AtomicBoolean(false)
    private val queue = ArrayBlockingQueue<ByteArray>(QUEUE_CAPACITY)
    private var audioTrack: AudioTrack? = null
    private var playbackThread: Thread? = null
    private var previousMode: Int? = null
    private var previousSpeakerphone: Boolean? = null

    fun start(): Boolean {
        if (isRunning.get()) return true

        configureCommunicationAudio()

        val minBuffer = AudioTrack.getMinBufferSize(SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT)
        if (minBuffer <= 0) {
            Log.e(TAG, "Invalid AudioTrack min buffer size: $minBuffer")
            restoreAudioRouting()
            return false
        }

        audioTrack = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setSampleRate(SAMPLE_RATE)
                    .setChannelMask(CHANNEL_CONFIG)
                    .setEncoding(AUDIO_FORMAT)
                    .build()
            )
            .setBufferSizeInBytes(minBuffer * 2)
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()

        if (audioTrack?.state != AudioTrack.STATE_INITIALIZED) {
            Log.e(TAG, "AudioTrack failed to initialize")
            audioTrack?.release()
            audioTrack = null
            restoreAudioRouting()
            return false
        }

        isRunning.set(true)
        queue.clear()
        audioTrack?.setVolume(AudioTrack.getMaxVolume())
        audioTrack?.play()

        playbackThread = Thread({
            android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_AUDIO)
            while (isRunning.get()) {
                try {
                    val data = queue.take()
                    writeFully(data)
                } catch (_: InterruptedException) {
                    break
                } catch (e: Exception) {
                    Log.e(TAG, "AudioTrack write failed", e)
                    break
                }
            }
        }, "PhoneSpeakerPlayback").also { it.start() }

        Log.i(TAG, "Phone speaker playback started: ${SAMPLE_RATE}Hz, buffer=${minBuffer * 2}B")
        return true
    }

    fun play(data: ByteArray) {
        if (!isRunning.get() || data.isEmpty()) return
        if (!queue.offer(data.copyOf())) {
            queue.poll()
            queue.offer(data.copyOf())
        }
    }

    fun stop() {
        isRunning.set(false)
        playbackThread?.interrupt()
        playbackThread?.join(1000)
        playbackThread = null
        queue.clear()
        try {
            audioTrack?.stop()
            audioTrack?.release()
        } catch (e: Exception) {
            Log.w(TAG, "Error stopping phone speaker playback", e)
        }
        audioTrack = null
        restoreAudioRouting()
        Log.i(TAG, "Phone speaker playback stopped")
    }

    private fun writeFully(data: ByteArray) {
        var offset = 0
        val track = audioTrack ?: return
        while (offset < data.size && isRunning.get()) {
            val written = track.write(data, offset, data.size - offset, AudioTrack.WRITE_BLOCKING)
            if (written < 0) {
                Log.w(TAG, "AudioTrack write returned $written")
                return
            }
            if (written == 0) {
                Thread.sleep(2)
                continue
            }
            offset += written
        }
    }

    private fun configureCommunicationAudio() {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        previousMode = audioManager.mode
        @Suppress("DEPRECATION")
        previousSpeakerphone = audioManager.isSpeakerphoneOn
        audioManager.mode = AudioManager.MODE_NORMAL
        @Suppress("DEPRECATION")
        audioManager.isSpeakerphoneOn = true
    }

    private fun restoreAudioRouting() {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        previousMode?.let { audioManager.mode = it }
        previousSpeakerphone?.let {
            @Suppress("DEPRECATION")
            audioManager.isSpeakerphoneOn = it
        }
        previousMode = null
        previousSpeakerphone = null
    }
}
