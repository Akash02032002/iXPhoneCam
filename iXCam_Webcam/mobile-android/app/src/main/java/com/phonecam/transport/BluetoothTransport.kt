package com.phonecam.transport

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothServerSocket
import android.bluetooth.BluetoothSocket
import android.content.Context
import android.util.Log
import java.io.IOException
import java.util.UUID

/**
 * Bluetooth RFCOMM (Serial Port Profile) transport server.
 *
 * The phone acts as a Bluetooth server, the PC connects as a client.
 * Requires the devices to be paired first via Android Bluetooth settings.
 *
 * Note: Bluetooth Classic (RFCOMM) max bandwidth is ~2-3 Mbps for SPP.
 * Recommended to use lower resolution/bitrate when streaming over Bluetooth.
 */
class BluetoothTransport(
    private val context: Context
) : TransportServer() {

    companion object {
        private const val TAG = "BluetoothTransport"
        private const val SERVICE_NAME = "PhoneCam"
        // Custom UUID for PhoneCam RFCOMM service
        val SERVICE_UUID: UUID = UUID.fromString("a1b2c3d4-e5f6-7890-abcd-ef1234567890")
    }

    private var bluetoothAdapter: BluetoothAdapter? = null
    private var serverSocket: BluetoothServerSocket? = null
    private var connectedSocket: BluetoothSocket? = null
    private var acceptThread: Thread? = null

    @SuppressLint("MissingPermission")
    override fun start() {
        if (isRunning) return

        val manager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        bluetoothAdapter = manager?.adapter

        if (bluetoothAdapter == null || !bluetoothAdapter!!.isEnabled) {
            listener?.onError("Bluetooth is not available or not enabled")
            return
        }

        try {
            serverSocket = bluetoothAdapter!!.listenUsingRfcommWithServiceRecord(
                SERVICE_NAME, SERVICE_UUID
            )
            isRunning = true

            acceptThread = Thread({
                Log.i(TAG, "Bluetooth server listening...")
                while (isRunning) {
                    try {
                        val socket = serverSocket?.accept() ?: break

                        // Disconnect any existing client
                        disconnectClient()
                        connectedSocket = socket

                        val deviceName = try {
                            socket.remoteDevice?.name ?: "Unknown"
                        } catch (e: SecurityException) {
                            "Unknown"
                        }

                        val client = TransportClient(
                            outputStream = socket.outputStream,
                            inputStream = socket.inputStream,
                            description = "Bluetooth: $deviceName",
                            onDisconnected = { disconnectClient() }
                        )

                        Log.i(TAG, "Bluetooth client connected: $deviceName")
                        listener?.onClientConnected(client)

                    } catch (e: IOException) {
                        if (isRunning) {
                            Log.e(TAG, "BT accept error", e)
                            listener?.onError("Bluetooth error: ${e.message}")
                        }
                    }
                }
            }, "BTAcceptThread").also { it.start() }

        } catch (e: SecurityException) {
            Log.e(TAG, "Bluetooth permission denied", e)
            listener?.onError("Bluetooth permission denied")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start Bluetooth server", e)
            listener?.onError("Bluetooth start failed: ${e.message}")
        }
    }

    private fun disconnectClient() {
        try {
            connectedSocket?.close()
        } catch (e: Exception) {
            Log.w(TAG, "Error closing BT client", e)
        }
        connectedSocket = null
    }

    override fun stop() {
        isRunning = false
        disconnectClient()
        try {
            serverSocket?.close()
        } catch (e: Exception) {
            Log.w(TAG, "Error closing BT server", e)
        }
        serverSocket = null
        acceptThread?.join(3000)
        acceptThread = null
        Log.i(TAG, "Bluetooth server stopped")
    }

    @SuppressLint("MissingPermission")
    override fun getConnectionInfo(): String {
        val name = try {
            bluetoothAdapter?.name ?: "Unknown"
        } catch (e: SecurityException) {
            "Unknown"
        }
        return "Bluetooth: $name (pair device first)"
    }
}
