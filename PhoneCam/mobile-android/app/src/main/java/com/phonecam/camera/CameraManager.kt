package com.phonecam.camera

import android.content.Context
import android.util.Log
import android.util.Size
import androidx.camera.core.*
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * Manages CameraX lifecycle, preview, and frame analysis.
 * Provides raw YUV frames to the VideoEncoder for H.264 encoding.
 */
class CameraManager(private val context: Context) {

    companion object {
        private const val TAG = "CameraManager"
        private const val OBJECT_FOCUS_ZOOM = 1.8f
    }

    private var cameraProvider: ProcessCameraProvider? = null
    private var camera: Camera? = null
    private var preview: Preview? = null
    private var imageAnalysis: ImageAnalysis? = null
    private var cameraSelector = CameraSelector.DEFAULT_BACK_CAMERA
    private val cameraExecutor: ExecutorService = Executors.newSingleThreadExecutor()

    var targetResolution: Size = Size(640, 480)
    var isUsingFrontCamera: Boolean = false
        private set

    // Callback to receive raw frames
    var onFrameAvailable: ((image: ImageProxy) -> Unit)? = null

    /**
     * Initialize and bind camera to the lifecycle owner.
     */
    fun startCamera(
        lifecycleOwner: LifecycleOwner,
        previewView: PreviewView
    ) {
        val cameraProviderFuture = ProcessCameraProvider.getInstance(context)
        cameraProviderFuture.addListener({
            try {
                cameraProvider = cameraProviderFuture.get()
                bindCameraUseCases(lifecycleOwner, previewView)
            } catch (e: Exception) {
                Log.e(TAG, "Camera initialization failed", e)
            }
        }, ContextCompat.getMainExecutor(context))
    }

    private fun bindCameraUseCases(
        lifecycleOwner: LifecycleOwner,
        previewView: PreviewView
    ) {
        val provider = cameraProvider ?: return

        // Preview use case
        preview = Preview.Builder()
            .setTargetResolution(targetResolution)
            .build()
            .also {
                it.setSurfaceProvider(previewView.surfaceProvider)
            }

        // Image analysis for capturing raw frames
        imageAnalysis = ImageAnalysis.Builder()
            .setTargetResolution(targetResolution)
            .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
            .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888)
            .build()
            .also { analysis ->
                analysis.setAnalyzer(cameraExecutor) { imageProxy ->
                    onFrameAvailable?.invoke(imageProxy)
                        ?: imageProxy.close()
                }
            }

        cameraSelector = if (isUsingFrontCamera) {
            CameraSelector.DEFAULT_FRONT_CAMERA
        } else {
            CameraSelector.DEFAULT_BACK_CAMERA
        }

        try {
            provider.unbindAll()
            camera = provider.bindToLifecycle(
                lifecycleOwner,
                cameraSelector,
                preview,
                imageAnalysis
            )
            Log.i(TAG, "Camera bound successfully: resolution=$targetResolution, front=$isUsingFrontCamera")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to bind camera use cases", e)
        }
    }

    /**
     * Switch between front and back camera.
     */
    fun switchCamera(lifecycleOwner: LifecycleOwner, previewView: PreviewView) {
        isUsingFrontCamera = !isUsingFrontCamera
        bindCameraUseCases(lifecycleOwner, previewView)
    }

    /**
     * Toggle flashlight (only available on back camera).
     */
    fun toggleFlash(): Boolean {
        val cam = camera ?: return false
        if (!cam.cameraInfo.hasFlashUnit()) return false
        val current = cam.cameraInfo.torchState.value == TorchState.ON
        cam.cameraControl.enableTorch(!current)
        return !current
    }

    /**
     * Reset focus/metering to the default continuous camera behavior.
     */
    fun triggerAutoFocus() {
        camera?.cameraControl?.cancelFocusAndMetering()
    }

    /**
     * Toggle center-object focus mode.
     *
     * Off returns the camera to the full scene: no focus/metering lock and no digital zoom.
     * On applies center focus/metering and a modest crop so the focused object is visually emphasized.
     */
    fun setObjectAutoFocus(enabled: Boolean, previewView: PreviewView) {
        val cam = camera ?: return
        val control = cam.cameraControl

        if (!enabled) {
            control.cancelFocusAndMetering()
            control.setZoomRatio(1f)
            Log.i(TAG, "Object autofocus disabled: full-scene capture restored")
            return
        }

        val viewWidth = previewView.width
        val viewHeight = previewView.height
        if (viewWidth <= 0 || viewHeight <= 0) {
                Log.w(TAG, "Object autofocus ignored: preview view is not ready")
            return
        }

        val focusZoom = OBJECT_FOCUS_ZOOM.coerceAtMost(getMaxZoom()).coerceAtLeast(1f)
        control.setZoomRatio(focusZoom)

        val centerPoint = previewView.meteringPointFactory.createPoint(
            viewWidth / 2f,
            viewHeight / 2f
        )
        val action = FocusMeteringAction.Builder(
            centerPoint,
            FocusMeteringAction.FLAG_AF or FocusMeteringAction.FLAG_AE or FocusMeteringAction.FLAG_AWB
        )
            .disableAutoCancel()
            .build()

        val focusResult = control.startFocusAndMetering(action)
        focusResult.addListener({
            try {
                val result = focusResult.get()
                Log.i(
                    TAG,
                    "Object autofocus enabled: zoom=${focusZoom}x, focusSuccessful=${result.isFocusSuccessful}"
                )
            } catch (e: Exception) {
                Log.w(TAG, "Object autofocus request failed", e)
            }
        }, ContextCompat.getMainExecutor(context))
    }

    /**
     * Set zoom ratio (1.0 = no zoom).
     */
    fun setZoom(ratio: Float) {
        camera?.cameraControl?.setZoomRatio(ratio.coerceIn(1f, getMaxZoom()))
    }

    fun getMaxZoom(): Float {
        return camera?.cameraInfo?.zoomState?.value?.maxZoomRatio ?: 1f
    }

    /**
     * Change resolution and rebind.
     */
    fun setResolution(width: Int, height: Int, lifecycleOwner: LifecycleOwner, previewView: PreviewView) {
        targetResolution = Size(width, height)
        bindCameraUseCases(lifecycleOwner, previewView)
    }

    fun shutdown() {
        cameraProvider?.unbindAll()
        cameraExecutor.shutdown()
    }
}
