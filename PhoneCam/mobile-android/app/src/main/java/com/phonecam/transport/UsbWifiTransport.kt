package com.phonecam.transport

import android.content.Context
import android.net.wifi.WifiManager
import android.util.Log
import com.phonecam.protocol.Protocol
import java.io.IOException
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.nio.ByteOrder

/**
 * TCP server for USB (via ADB port forward) and WiFi connections.
 *
 * USB mode:  Phone runs TCP server on localhost:4747.
 *            PC runs: adb forward tcp:4747 tcp:4747
 *            PC connects to localhost:4747 on its side.
 *
 * WiFi mode: Phone runs TCP server on its WiFi IP:4747.
 *            PC connects directly to phone's IP.
 *
 * Both use the same TCP server implementation.
 */
class UsbWifiTransport(
    private val context: Context,
    private val port: Int = Protocol.DEFAULT_PORT
) : TransportServer() {

    companion object {
        private const val TAG = "UsbWifiTransport"
    }

    private var serverSocket: ServerSocket? = null
    private var acceptThread: Thread? = null
    private var connectedSocket: Socket? = null

    override fun start() {
        if (isRunning) return

        try {
            serverSocket = ServerSocket(port)
            serverSocket?.reuseAddress = true
            isRunning = true

            acceptThread = Thread({
                Log.i(TAG, "Server listening on port $port")
                while (isRunning) {
                    try {
                        val socket = serverSocket?.accept() ?: break
                        socket.tcpNoDelay = true  // Disable Nagle's for low latency
                        socket.sendBufferSize = 2 * 1024 * 1024 // 2MB send buffer
                        socket.soTimeout = 0 // No read timeout (reads block normally)
                        socket.setSoLinger(false, 0) // Don't block on close

                        // Disconnect any existing client
                        disconnectClient()
                        connectedSocket = socket

                        val client = TransportClient(
                            outputStream = socket.getOutputStream(),
                            inputStream = socket.getInputStream(),
                            description = "TCP ${socket.remoteSocketAddress}",
                            onDisconnected = { disconnectClient() }
                        )

                        Log.i(TAG, "Client connected: ${socket.remoteSocketAddress}")
                        listener?.onClientConnected(client)

                    } catch (e: IOException) {
                        if (isRunning) {
                            Log.e(TAG, "Accept error", e)
                            listener?.onError("Accept error: ${e.message}")
                        }
                    }
                }
            }, "TCPAcceptThread").also { it.start() }

        } catch (e: Exception) {
            Log.e(TAG, "Failed to start server", e)
            listener?.onError("Server start failed: ${e.message}")
        }
    }

    private fun disconnectClient() {
        try {
            connectedSocket?.close()
        } catch (e: Exception) {
            Log.w(TAG, "Error closing client socket", e)
        }
        connectedSocket = null
    }

    override fun stop() {
        isRunning = false
        disconnectClient()
        try {
            serverSocket?.close()
        } catch (e: Exception) {
            Log.w(TAG, "Error closing server socket", e)
        }
        serverSocket = null
        acceptThread?.join(3000)
        acceptThread = null
        Log.i(TAG, "Server stopped")
    }

    /**
     * Get the device's WiFi IP address for display.
     */
    fun getWifiIpAddress(): String {
        try {
            val wifiManager = context.applicationContext
                .getSystemService(Context.WIFI_SERVICE) as WifiManager
            val wifiInfo = wifiManager.connectionInfo
            val ipInt = wifiInfo.ipAddress

            if (ipInt == 0) return "Not connected to WiFi"

            // Convert int IP to string (little-endian on Android)
            return if (ByteOrder.nativeOrder() == ByteOrder.LITTLE_ENDIAN) {
                "${ipInt and 0xFF}.${(ipInt shr 8) and 0xFF}.${(ipInt shr 16) and 0xFF}.${(ipInt shr 24) and 0xFF}"
            } else {
                "${(ipInt shr 24) and 0xFF}.${(ipInt shr 16) and 0xFF}.${(ipInt shr 8) and 0xFF}.${ipInt and 0xFF}"
            }
        } catch (e: Exception) {
            return "Unknown"
        }
    }

    override fun getConnectionInfo(): String {
        val ip = getWifiIpAddress()
        return "WiFi: $ip:$port\nUSB: Connect via ADB (port $port)"
    }
}
