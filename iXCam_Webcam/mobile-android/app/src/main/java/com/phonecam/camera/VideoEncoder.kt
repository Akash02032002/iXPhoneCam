package com.phonecam.camera

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.util.Log
import android.util.Size
import androidx.camera.core.ImageProxy
import java.nio.ByteBuffer
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Hardware-accelerated H.264 encoder using Android MediaCodec.
 * Receives raw YUV frames from CameraManager, outputs encoded NAL units.
 */
class VideoEncoder {

    companion object {
        private const val TAG = "VideoEncoder"
        private const val MIME_TYPE = MediaFormat.MIMETYPE_VIDEO_AVC  // H.264
        private const val IFRAME_INTERVAL = 2  // seconds between I-frames
        private const val TIMEOUT_US = 10_000L // 10ms dequeue timeout
    }

    private var codec: MediaCodec? = null
    private val isRunning = AtomicBoolean(false)
    private var encoderThread: Thread? = null
    private var width = 1280
    private var height = 720
    private var sourceWidth = 1280
    private var sourceHeight = 720
    private var bitrate = 1_200_000
    private var frameRate = 10
    private var rotationDegrees = 0

    // Callback for encoded output
    var onEncodedFrame: ((data: ByteArray, presentationTimeUs: Long, isKeyFrame: Boolean) -> Unit)? = null

    // Store SPS/PPS codec config to prepend to keyframes
    @Volatile
    private var codecConfigData: ByteArray? = null

    // Debug counters
    private val framesIn = java.util.concurrent.atomic.AtomicLong(0)
    private val framesEncoded = java.util.concurrent.atomic.AtomicLong(0)
    private val framesDropped = java.util.concurrent.atomic.AtomicLong(0)

    // Thread-safe queue for raw input frames
    private val inputQueue = LinkedBlockingQueue<FrameData>(3)

    data class FrameData(
        val yuvData: ByteArray,
        val timestampUs: Long
    )

    /**
     * Configure and start the encoder.
     */
    fun start(resolution: Size, bitrateValue: Int = 1_200_000, fps: Int = 10) {
        if (isRunning.get()) stop()

        sourceWidth = resolution.width
        sourceHeight = resolution.height
        width = if (rotationDegrees == 90 || rotationDegrees == 270) sourceHeight else sourceWidth
        height = if (rotationDegrees == 90 || rotationDegrees == 270) sourceWidth else sourceHeight
        bitrate = bitrateValue
        frameRate = fps

        // Reset all counters so auto-detection works after camera switch
        framesIn.set(0)
        framesEncoded.set(0)
        framesDropped.set(0)
        codecConfigData = null
        yuvLoggedOnce = false

        val format = MediaFormat.createVideoFormat(MIME_TYPE, width, height).apply {
            setInteger(MediaFormat.KEY_BIT_RATE, bitrate)
            setInteger(MediaFormat.KEY_FRAME_RATE, frameRate)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, IFRAME_INTERVAL)
            setInteger(
                MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible
            )
            // Low bitrate/fps keeps the ADB USB bridge from blocking under Zoom load.
            setInteger(MediaFormat.KEY_BITRATE_MODE,
                MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR)
            try {
                setInteger(MediaFormat.KEY_PROFILE,
                    MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline)
                setInteger(MediaFormat.KEY_LEVEL,
                    MediaCodecInfo.CodecProfileLevel.AVCLevel31)
            } catch (e: Exception) {
                Log.w(TAG, "Baseline profile not supported, using default", e)
            }
            // Low latency mode (API 30+)
            try {
                setInteger(MediaFormat.KEY_LATENCY, 1)
            } catch (_: Exception) { }
            // Realtime priority for encoding
            setInteger(MediaFormat.KEY_PRIORITY, 0)
        }

        try {
            codec = MediaCodec.createEncoderByType(MIME_TYPE)
            codec!!.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            codec!!.start()
            isRunning.set(true)

            // Start encoder output thread
            encoderThread = Thread(::encoderLoop, "VideoEncoderThread").also { it.start() }

            Log.i(TAG, "Encoder started: ${width}x${height} @ ${bitrate / 1_000}kbps, ${frameRate}fps, rotation=${rotationDegrees}")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start encoder", e)
            stop()
        }
    }

    /**
     * Feed a raw YUV frame from CameraX ImageProxy into the encoder.
     */
    fun encodeFrame(imageProxy: ImageProxy) {
        val count = framesIn.incrementAndGet()
        if (!isRunning.get()) {
            imageProxy.close()
            if (count % 30 == 0L) Log.w(TAG, "encodeFrame: encoder not running, dropping frame #$count")
            return
        }

        // Auto-detect actual camera resolution and reconfigure encoder if needed
        // Check every frame (not just the first) to handle camera switches
        val imgW = imageProxy.width
        val imgH = imageProxy.height
        if (imgW != sourceWidth || imgH != sourceHeight) {
            Log.w(TAG, "Camera gives ${imgW}x${imgH} but encoder source is ${sourceWidth}x${sourceHeight}. Reconfiguring...")
            imageProxy.close()
            val savedBitrate = bitrate
            val savedFps = frameRate
            stop()
            start(Size(imgW, imgH), savedBitrate, savedFps)
            requestKeyFrame()
            return
        }

        try {
            val sourceYuvBytes = yuvImageToByteArray(imageProxy)
            val yuvBytes = rotateNv12(sourceYuvBytes, imageProxy.width, imageProxy.height, rotationDegrees)
            val timestamp = imageProxy.imageInfo.timestamp / 1000 // ns -> us

            // Non-blocking: drop oldest frame if queue is full
            if (!inputQueue.offer(FrameData(yuvBytes, timestamp))) {
                inputQueue.poll()
                inputQueue.offer(FrameData(yuvBytes, timestamp))
                framesDropped.incrementAndGet()
            }
        } finally {
            imageProxy.close()
        }

        drainInput()

        if (count % 100 == 0L) {
            Log.i(TAG, "Stats: framesIn=$count, encoded=${framesEncoded.get()}, dropped=${framesDropped.get()}, queueSize=${inputQueue.size}")
        }
    }

    /**
     * Convert ImageProxy YUV_420_888 to NV12 byte array for MediaCodec.
     *
     * Android's YUV_420_888 can have different internal layouts:
     * - Planar (pixelStride=1): separate U and V planes
     * - Semi-planar (pixelStride=2): interleaved UV in shared buffer (NV12 or NV21)
     */
    private var yuvLoggedOnce = false

    private fun yuvImageToByteArray(image: ImageProxy): ByteArray {
        val yPlane = image.planes[0]
        val uPlane = image.planes[1]
        val vPlane = image.planes[2]

        // CRITICAL: Use the IMAGE's actual dimensions, not the encoder's.
        // CameraX may deliver a different resolution than requested (e.g., 960x720 instead of 1280x720).
        val imgWidth = image.width
        val imgHeight = image.height
        val nv12 = ByteArray(imgWidth * imgHeight * 3 / 2)

        // Copy Y plane
        val yBuffer = yPlane.buffer
        val ySize = yBuffer.remaining()
        yBuffer.get(nv12, 0, minOf(ySize, imgWidth * imgHeight))

        // Handle UV planes
        val uvWidth = imgWidth / 2
        val uvHeight = imgHeight / 2
        var uvIndex = imgWidth * imgHeight

        val uBuffer = uPlane.buffer
        val vBuffer = vPlane.buffer

        val uPixelStride = uPlane.pixelStride
        val vPixelStride = vPlane.pixelStride
        val uRowStride = uPlane.rowStride
        val vRowStride = vPlane.rowStride

        // Get the actual buffer sizes before any reads change positions.
        val uSize = uBuffer.remaining()
        val vSize = vBuffer.remaining()

        if (!yuvLoggedOnce) {
            yuvLoggedOnce = true
            Log.i(TAG, "YUV buffer: U pxStride=$uPixelStride rowStride=$uRowStride remaining=$uSize cap=${uBuffer.capacity()} limit=${uBuffer.limit()}")
            Log.i(TAG, "  V pxStride=$vPixelStride rowStride=$vRowStride remaining=$vSize cap=${vBuffer.capacity()} limit=${vBuffer.limit()}")
            Log.i(TAG, "  Image ${imgWidth}x${imgHeight}, encoder ${width}x${height}, rotation=$rotationDegrees, ySize=$ySize, uvWidth=$uvWidth uvHeight=$uvHeight")
            val maxUPos = (uvHeight - 1) * uRowStride + (uvWidth - 1) * uPixelStride
            Log.i(TAG, "  Max U pos needed: $maxUPos, U limit: ${uBuffer.limit()}")
        }

        // Safely interleave U and V using individual pixel access.
        // The buffer limit may be smaller than the max position we need to access
        // (e.g., limit=230400 but max pos=460798 when pixelStride=2 and rowStride=1280).
        // In this case, we try to expand the limit to capacity first.
        try {
            val neededU = (uvHeight - 1) * uRowStride + (uvWidth - 1) * uPixelStride + 1
            val neededV = (uvHeight - 1) * vRowStride + (uvWidth - 1) * vPixelStride + 1
            if (uBuffer.limit() < neededU && uBuffer.capacity() >= neededU) {
                uBuffer.limit(neededU)
            }
            if (vBuffer.limit() < neededV && vBuffer.capacity() >= neededV) {
                vBuffer.limit(neededV)
            }
        } catch (_: Exception) { }

        val uLimit = uBuffer.limit()
        val vLimit = vBuffer.limit()

        for (row in 0 until uvHeight) {
            for (col in 0 until uvWidth) {
                if (uvIndex >= nv12.size - 1) break

                val uPos = row * uRowStride + col * uPixelStride
                val vPos = row * vRowStride + col * vPixelStride

                val u: Byte = if (uPos < uLimit) uBuffer.get(uPos) else 128.toByte()
                val v: Byte = if (vPos < vLimit) vBuffer.get(vPos) else 128.toByte()

                // NV12 format: U V U V ...
                nv12[uvIndex++] = u
                nv12[uvIndex++] = v
            }
        }

        return nv12
    }

    /**
     * Feed queued input frames to the codec.
     */
    private fun drainInput() {
        val encoder = codec ?: return

        try {
            while (inputQueue.isNotEmpty()) {
                if (!isRunning.get()) break

                val inputIndex = encoder.dequeueInputBuffer(0)
                if (inputIndex < 0) break

                val frame = inputQueue.poll() ?: break
                val inputBuffer = encoder.getInputBuffer(inputIndex) ?: continue

                inputBuffer.clear()
                inputBuffer.put(frame.yuvData, 0, minOf(frame.yuvData.size, inputBuffer.capacity()))

                encoder.queueInputBuffer(
                    inputIndex, 0, frame.yuvData.size,
                    frame.timestampUs, 0
                )
            }
        } catch (e: IllegalStateException) {
            inputQueue.clear()
            if (isRunning.get()) {
                Log.w(TAG, "Encoder input unavailable; dropping queued frames", e)
            }
        }
    }

    private fun rotateNv12(source: ByteArray, srcWidth: Int, srcHeight: Int, rotation: Int): ByteArray {
        return when (rotation) {
            90 -> rotateNv12Clockwise(source, srcWidth, srcHeight)
            180 -> rotateNv12HalfTurn(source, srcWidth, srcHeight)
            270 -> rotateNv12CounterClockwise(source, srcWidth, srcHeight)
            else -> source
        }
    }

    private fun rotateNv12Clockwise(source: ByteArray, srcWidth: Int, srcHeight: Int): ByteArray {
        val ySize = srcWidth * srcHeight
        val output = ByteArray(source.size)
        var dst = 0

        for (x in 0 until srcWidth) {
            for (y in srcHeight - 1 downTo 0) {
                output[dst++] = source[y * srcWidth + x]
            }
        }

        val dstUvStart = ySize
        val srcUvStart = ySize
        val uvWidth = srcWidth / 2
        val uvHeight = srcHeight / 2
        var dstUv = dstUvStart

        for (x in 0 until uvWidth) {
            for (y in uvHeight - 1 downTo 0) {
                val srcUv = srcUvStart + y * srcWidth + x * 2
                output[dstUv++] = source[srcUv]
                output[dstUv++] = source[srcUv + 1]
            }
        }

        return output
    }

    private fun rotateNv12CounterClockwise(source: ByteArray, srcWidth: Int, srcHeight: Int): ByteArray {
        val ySize = srcWidth * srcHeight
        val output = ByteArray(source.size)
        var dst = 0

        for (x in srcWidth - 1 downTo 0) {
            for (y in 0 until srcHeight) {
                output[dst++] = source[y * srcWidth + x]
            }
        }

        val dstUvStart = ySize
        val srcUvStart = ySize
        val uvWidth = srcWidth / 2
        val uvHeight = srcHeight / 2
        var dstUv = dstUvStart

        for (x in uvWidth - 1 downTo 0) {
            for (y in 0 until uvHeight) {
                val srcUv = srcUvStart + y * srcWidth + x * 2
                output[dstUv++] = source[srcUv]
                output[dstUv++] = source[srcUv + 1]
            }
        }

        return output
    }

    private fun rotateNv12HalfTurn(source: ByteArray, srcWidth: Int, srcHeight: Int): ByteArray {
        val ySize = srcWidth * srcHeight
        val output = ByteArray(source.size)
        var dst = 0

        for (y in srcHeight - 1 downTo 0) {
            for (x in srcWidth - 1 downTo 0) {
                output[dst++] = source[y * srcWidth + x]
            }
        }

        val srcUvStart = ySize
        val uvHeight = srcHeight / 2
        var dstUv = ySize

        for (y in uvHeight - 1 downTo 0) {
            for (x in srcWidth - 2 downTo 0 step 2) {
                val srcUv = srcUvStart + y * srcWidth + x
                output[dstUv++] = source[srcUv]
                output[dstUv++] = source[srcUv + 1]
            }
        }

        return output
    }

    /**
     * Main encoder output loop - runs on a separate thread.
     * Dequeues encoded frames and passes them to onEncodedFrame callback.
     */
    private fun encoderLoop() {
        val bufferInfo = MediaCodec.BufferInfo()

        while (isRunning.get()) {
            try {
                val encoder = codec ?: break
                val outputIndex = encoder.dequeueOutputBuffer(bufferInfo, TIMEOUT_US)

                when {
                    outputIndex >= 0 -> {
                        val outputBuffer = encoder.getOutputBuffer(outputIndex)
                        if (outputBuffer != null && bufferInfo.size > 0) {
                            val encodedData = ByteArray(bufferInfo.size)
                            outputBuffer.position(bufferInfo.offset)
                            outputBuffer.limit(bufferInfo.offset + bufferInfo.size)
                            outputBuffer.get(encodedData)

                            val isCodecConfig = bufferInfo.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0
                            val isKeyFrame = bufferInfo.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME != 0

                            if (isCodecConfig) {
                                // Store SPS/PPS for prepending to keyframes
                                codecConfigData = encodedData.copyOf()
                                Log.i(TAG, "Stored codec config (SPS/PPS): ${encodedData.size} bytes")
                            } else {
                                framesEncoded.incrementAndGet()
                                // For keyframes, prepend SPS/PPS so decoder can always start
                                val dataToSend = if (isKeyFrame) {
                                    val config = codecConfigData
                                    if (config != null) {
                                        config + encodedData
                                    } else {
                                        encodedData
                                    }
                                } else {
                                    encodedData
                                }
                                onEncodedFrame?.invoke(
                                    dataToSend,
                                    bufferInfo.presentationTimeUs,
                                    isKeyFrame
                                )
                            }
                        }
                        encoder.releaseOutputBuffer(outputIndex, false)
                    }
                    outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        Log.i(TAG, "Output format changed: ${encoder.outputFormat}")
                    }
                }
            } catch (e: IllegalStateException) {
                if (isRunning.get()) {
                    Log.e(TAG, "Encoder error", e)
                }
            }
        }
    }

    /**
     * Request an immediate I-frame (key frame).
     */
    fun requestKeyFrame() {
        try {
            val params = android.os.Bundle()
            params.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0)
            codec?.setParameters(params)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to request key frame", e)
        }
    }

    /**
     * Dynamically change bitrate.
     */
    fun setBitrate(newBitrate: Int) {
        try {
            val params = android.os.Bundle()
            params.putInt(MediaCodec.PARAMETER_KEY_VIDEO_BITRATE, newBitrate)
            codec?.setParameters(params)
            bitrate = newBitrate
            Log.i(TAG, "Bitrate changed to ${newBitrate / 1000}kbps")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to set bitrate", e)
        }
    }

    fun setRotationDegrees(degrees: Int) {
        val normalized = ((degrees % 360) + 360) % 360
        val rounded = when (normalized) {
            in 45 until 135 -> 90
            in 135 until 225 -> 180
            in 225 until 315 -> 270
            else -> 0
        }

        if (rounded == rotationDegrees) return

        rotationDegrees = rounded
        val wasRunning = isRunning.get()
        val savedBitrate = bitrate
        val savedFps = frameRate
        val savedSource = Size(sourceWidth, sourceHeight)

        if (wasRunning) {
            stop()
            start(savedSource, savedBitrate, savedFps)
            requestKeyFrame()
        }
    }

    fun stop() {
        isRunning.set(false)
        encoderThread?.join(2000)
        encoderThread = null

        try {
            codec?.stop()
            codec?.release()
        } catch (e: Exception) {
            Log.e(TAG, "Error stopping encoder", e)
        }
        codec = null
        inputQueue.clear()
        Log.i(TAG, "Encoder stopped")
    }
}
