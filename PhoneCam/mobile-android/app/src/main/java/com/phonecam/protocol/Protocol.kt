package com.phonecam.protocol

import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Wire protocol for PhoneCam video/audio streaming.
 *
 * Packet format:
 * ┌──────────────────────────────────────────────┐
 * │ Magic      (2 bytes) = 0xCA, 0xFE           │
 * │ Type       (1 byte)  = VIDEO|AUDIO|CONTROL   │
 * │ Flags      (1 byte)  = KEYFRAME, etc.        │
 * │ Length     (4 bytes) = payload size           │
 * │ Timestamp  (8 bytes) = presentation time µs   │
 * │ Payload    (variable) = encoded data          │
 * └──────────────────────────────────────────────┘
 * Total header size: 16 bytes
 */
object Protocol {

    const val MAGIC_BYTE_1: Byte = 0xCA.toByte()
    const val MAGIC_BYTE_2: Byte = 0xFE.toByte()
    const val HEADER_SIZE = 16

    // Default port for TCP connection
    const val DEFAULT_PORT = 4747

    // Packet types
    const val TYPE_VIDEO: Byte = 0x01
    const val TYPE_AUDIO: Byte = 0x02
    const val TYPE_CONTROL: Byte = 0x03
    const val TYPE_HEARTBEAT: Byte = 0x04
    const val TYPE_REVERSE_AUDIO: Byte = 0x05

    // Flags
    const val FLAG_NONE: Byte = 0x00
    const val FLAG_KEYFRAME: Byte = 0x01
    const val FLAG_CONFIG: Byte = 0x02  // SPS/PPS data

    // Control commands (sent in payload as single byte)
    const val CMD_SWITCH_CAMERA: Byte = 0x01
    const val CMD_TOGGLE_FLASH: Byte = 0x02
    const val CMD_SET_RESOLUTION: Byte = 0x03  // followed by 4 bytes: width(2) + height(2)
    const val CMD_SET_BITRATE: Byte = 0x04     // followed by 4 bytes: bitrate
    const val CMD_REQUEST_KEYFRAME: Byte = 0x05
    const val CMD_AUTOFOCUS: Byte = 0x06
    const val CMD_SET_ZOOM: Byte = 0x07       // followed by 2 bytes: zoom ratio * 100
    const val CMD_ROTATE_CAMERA: Byte = 0x08
    const val CMD_AUDIO_ON: Byte = 0x09
    const val CMD_AUDIO_OFF: Byte = 0x0A
    const val CMD_SET_AUTOFOCUS_MODE: Byte = 0x0B // followed by 2 bytes: 0/1
    const val CMD_DISCONNECT: Byte = 0x0F

    /**
     * Build a packet header + payload.
     */
    fun buildPacket(
        type: Byte,
        flags: Byte,
        timestampUs: Long,
        payload: ByteArray
    ): ByteArray {
        val packet = ByteBuffer.allocate(HEADER_SIZE + payload.size)
            .order(ByteOrder.BIG_ENDIAN)
        packet.put(MAGIC_BYTE_1)
        packet.put(MAGIC_BYTE_2)
        packet.put(type)
        packet.put(flags)
        packet.putInt(payload.size)
        packet.putLong(timestampUs)
        packet.put(payload)
        return packet.array()
    }

    /**
     * Write a video frame packet.
     */
    fun writeVideoFrame(
        output: OutputStream,
        encodedData: ByteArray,
        timestampUs: Long,
        isKeyFrame: Boolean
    ) {
        val flags = if (isKeyFrame) FLAG_KEYFRAME else FLAG_NONE
        val packet = buildPacket(TYPE_VIDEO, flags, timestampUs, encodedData)
        output.write(packet)
        output.flush()
    }

    /**
     * Write an audio packet.
     */
    fun writeAudioData(
        output: OutputStream,
        pcmData: ByteArray,
        size: Int,
        timestampUs: Long
    ) {
        val packet = buildPacket(TYPE_AUDIO, FLAG_NONE, timestampUs, pcmData.copyOf(size))
        output.write(packet)
        output.flush()
    }

    /**
     * Write a heartbeat packet.
     */
    fun writeHeartbeat(output: OutputStream) {
        val packet = buildPacket(TYPE_HEARTBEAT, FLAG_NONE, System.nanoTime() / 1000, ByteArray(0))
        output.write(packet)
        output.flush()
    }

    /**
     * Parse a packet header from raw bytes.
     */
    data class PacketHeader(
        val type: Byte,
        val flags: Byte,
        val payloadLength: Int,
        val timestampUs: Long
    )

    fun parseHeader(headerBytes: ByteArray): PacketHeader? {
        if (headerBytes.size < HEADER_SIZE) return null

        val buf = ByteBuffer.wrap(headerBytes).order(ByteOrder.BIG_ENDIAN)
        val magic1 = buf.get()
        val magic2 = buf.get()

        if (magic1 != MAGIC_BYTE_1 || magic2 != MAGIC_BYTE_2) return null

        val type = buf.get()
        val flags = buf.get()
        val length = buf.int
        val timestamp = buf.long

        return PacketHeader(type, flags, length, timestamp)
    }
}
