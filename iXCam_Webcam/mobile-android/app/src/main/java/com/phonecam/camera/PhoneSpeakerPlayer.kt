package com.phonecam.camera

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.util.Log
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Plays reverse-streamed PCM audio from the PC through the phone speaker.
 * Audio format: 44100 Hz, 16-bit, mono.
 */
class PhoneSpeakerPlayer {

    companion object {
        private const val TAG = "PhoneSpeakerPlayer"
        const val SAMPLE_RATE = 44100
        const val CHANNEL_CONFIG = AudioFormat.CHANNEL_OUT_MONO
        const val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_16BIT
        private const val QUEUE_CAPACITY = 12
    }

    private val isRunning = AtomicBoolean(false)
    private val queue = ArrayBlockingQueue<ByteArray>(QUEUE_CAPACITY)
    private var audioTrack: AudioTrack? = null
    private var playbackThread: Thread? = null

    fun start(): Boolean {
        if (isRunning.get()) return true

        val minBuffer = AudioTrack.getMinBufferSize(SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT)
        if (minBuffer <= 0) {
            Log.e(TAG, "Invalid AudioTrack min buffer size: $minBuffer")
            return false
        }

        audioTrack = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
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
            return false
        }

        isRunning.set(true)
        queue.clear()
        audioTrack?.play()

        playbackThread = Thread({
            android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_AUDIO)
            while (isRunning.get()) {
                try {
                    val data = queue.take()
                    audioTrack?.write(data, 0, data.size)
                } catch (_: InterruptedException) {
                    break
                } catch (e: Exception) {
                    Log.e(TAG, "AudioTrack write failed", e)
                    break
                }
            }
        }, "PhoneSpeakerPlayback").also { it.start() }

        Log.i(TAG, "Phone speaker playback started")
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
        Log.i(TAG, "Phone speaker playback stopped")
    }
}
