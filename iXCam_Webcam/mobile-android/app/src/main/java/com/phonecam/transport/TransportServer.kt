package com.phonecam.transport

import java.io.InputStream
import java.io.OutputStream

/**
 * Represents a connected transport client (one PC connection).
 */
data class TransportClient(
    val outputStream: OutputStream,
    val inputStream: InputStream,
    val description: String,
    val onDisconnected: () -> Unit
)

/**
 * Callback interface for transport events.
 */
interface TransportListener {
    fun onClientConnected(client: TransportClient)
    fun onClientDisconnected(client: TransportClient)
    fun onError(error: String)
}

/**
 * Abstract base class for all transport implementations.
 */
abstract class TransportServer {
    var listener: TransportListener? = null
    var isRunning: Boolean = false
        protected set

    abstract fun start()
    abstract fun stop()
    abstract fun getConnectionInfo(): String
}
