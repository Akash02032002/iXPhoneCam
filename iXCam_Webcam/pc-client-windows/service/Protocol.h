#pragma once

/**
 * PhoneCam Wire Protocol - shared constants between Android and Windows.
 *
 * Packet format (16-byte header + variable payload):
 *   [0-1]  Magic bytes: 0xCA 0xFE
 *   [2]    Packet type
 *   [3]    Flags
 *   [4-7]  Payload length (big-endian uint32)
 *   [8-15] Timestamp in microseconds (big-endian int64)
 *   [16+]  Payload data
 */

#ifndef PHONECAM_PROTOCOL_H
#define PHONECAM_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <vector>

namespace phonecam {

    constexpr uint8_t  MAGIC_1 = 0xCA;
    constexpr uint8_t  MAGIC_2 = 0xFE;
    constexpr int      HEADER_SIZE = 16;
    constexpr int      DEFAULT_PORT = 4747;

    // Packet types
    constexpr uint8_t  TYPE_VIDEO     = 0x01;
    constexpr uint8_t  TYPE_AUDIO     = 0x02;
    constexpr uint8_t  TYPE_CONTROL   = 0x03;
    constexpr uint8_t  TYPE_HEARTBEAT = 0x04;
    constexpr uint8_t  TYPE_REVERSE_AUDIO = 0x05;

    // Flags
    constexpr uint8_t  FLAG_NONE      = 0x00;
    constexpr uint8_t  FLAG_KEYFRAME  = 0x01;
    constexpr uint8_t  FLAG_CONFIG    = 0x02;

    // Control commands
    constexpr uint8_t  CMD_SWITCH_CAMERA    = 0x01;
    constexpr uint8_t  CMD_TOGGLE_FLASH     = 0x02;
    constexpr uint8_t  CMD_SET_RESOLUTION   = 0x03;
    constexpr uint8_t  CMD_SET_BITRATE      = 0x04;
    constexpr uint8_t  CMD_REQUEST_KEYFRAME = 0x05;
    constexpr uint8_t  CMD_AUTOFOCUS        = 0x06;
    constexpr uint8_t  CMD_SET_ZOOM         = 0x07;  // followed by 2 bytes: zoom ratio * 100
    constexpr uint8_t  CMD_ROTATE_CAMERA    = 0x08;
    constexpr uint8_t  CMD_DISCONNECT       = 0x0F;

    // Bluetooth RFCOMM UUID (must match Android side)
    // a1b2c3d4-e5f6-7890-abcd-ef1234567890

    #pragma pack(push, 1)
    struct PacketHeader {
        uint8_t  magic1;
        uint8_t  magic2;
        uint8_t  type;
        uint8_t  flags;
        uint32_t payloadLength;  // big-endian
        int64_t  timestampUs;    // big-endian
    };
    #pragma pack(pop)

    // Helper: convert big-endian to host byte order
    inline uint32_t ntoh32(uint32_t val) {
        return ((val & 0xFF000000) >> 24) |
               ((val & 0x00FF0000) >> 8)  |
               ((val & 0x0000FF00) << 8)  |
               ((val & 0x000000FF) << 24);
    }

    inline int64_t ntoh64(int64_t val) {
        uint64_t v = static_cast<uint64_t>(val);
        return static_cast<int64_t>(
            ((v & 0xFF00000000000000ULL) >> 56) |
            ((v & 0x00FF000000000000ULL) >> 40) |
            ((v & 0x0000FF0000000000ULL) >> 24) |
            ((v & 0x000000FF00000000ULL) >> 8)  |
            ((v & 0x00000000FF000000ULL) << 8)  |
            ((v & 0x0000000000FF0000ULL) << 24) |
            ((v & 0x000000000000FF00ULL) << 40) |
            ((v & 0x00000000000000FFULL) << 56)
        );
    }

    // Parse header from raw bytes
    inline bool parseHeader(const uint8_t* data, PacketHeader& out) {
        if (data[0] != MAGIC_1 || data[1] != MAGIC_2) return false;

        out.magic1 = data[0];
        out.magic2 = data[1];
        out.type   = data[2];
        out.flags  = data[3];

        uint32_t rawLen;
        memcpy(&rawLen, data + 4, 4);
        out.payloadLength = ntoh32(rawLen);

        int64_t rawTs;
        memcpy(&rawTs, data + 8, 8);
        out.timestampUs = ntoh64(rawTs);

        return true;
    }

    // Build a control command packet (no parameters)
    inline std::vector<uint8_t> buildControlPacket(uint8_t command) {
        std::vector<uint8_t> packet(HEADER_SIZE + 1);
        packet[0] = MAGIC_1;
        packet[1] = MAGIC_2;
        packet[2] = TYPE_CONTROL;
        packet[3] = FLAG_NONE;
        // Length = 1 (big-endian)
        packet[4] = 0; packet[5] = 0; packet[6] = 0; packet[7] = 1;
        // Timestamp = 0
        for (int i = 8; i < 16; i++) packet[i] = 0;
        // Payload
        packet[16] = command;
        return packet;
    }

    // Build a control command packet with 2-byte parameter (e.g. zoom)
    inline std::vector<uint8_t> buildControlPacket2(uint8_t command, uint16_t param) {
        std::vector<uint8_t> packet(HEADER_SIZE + 3);
        packet[0] = MAGIC_1;
        packet[1] = MAGIC_2;
        packet[2] = TYPE_CONTROL;
        packet[3] = FLAG_NONE;
        // Length = 3 (big-endian)
        packet[4] = 0; packet[5] = 0; packet[6] = 0; packet[7] = 3;
        // Timestamp = 0
        for (int i = 8; i < 16; i++) packet[i] = 0;
        // Payload: command + param (big-endian)
        packet[16] = command;
        packet[17] = static_cast<uint8_t>((param >> 8) & 0xFF);
        packet[18] = static_cast<uint8_t>(param & 0xFF);
        return packet;
    }

    // Build a control command packet with 4-byte parameter (e.g. bitrate)
    inline std::vector<uint8_t> buildControlPacket4(uint8_t command, uint32_t param) {
        std::vector<uint8_t> packet(HEADER_SIZE + 5);
        packet[0] = MAGIC_1;
        packet[1] = MAGIC_2;
        packet[2] = TYPE_CONTROL;
        packet[3] = FLAG_NONE;
        // Length = 5 (big-endian)
        packet[4] = 0; packet[5] = 0; packet[6] = 0; packet[7] = 5;
        // Timestamp = 0
        for (int i = 8; i < 16; i++) packet[i] = 0;
        // Payload: command + param (big-endian)
        packet[16] = command;
        packet[17] = static_cast<uint8_t>((param >> 24) & 0xFF);
        packet[18] = static_cast<uint8_t>((param >> 16) & 0xFF);
        packet[19] = static_cast<uint8_t>((param >> 8) & 0xFF);
        packet[20] = static_cast<uint8_t>(param & 0xFF);
        return packet;
    }

    // Build a control command with 2x 2-byte params (e.g. resolution: width + height)
    inline std::vector<uint8_t> buildControlPacketRes(uint8_t command, uint16_t w, uint16_t h) {
        std::vector<uint8_t> packet(HEADER_SIZE + 5);
        packet[0] = MAGIC_1;
        packet[1] = MAGIC_2;
        packet[2] = TYPE_CONTROL;
        packet[3] = FLAG_NONE;
        // Length = 5 (big-endian)
        packet[4] = 0; packet[5] = 0; packet[6] = 0; packet[7] = 5;
        // Timestamp = 0
        for (int i = 8; i < 16; i++) packet[i] = 0;
        // Payload: command + width(2B) + height(2B)
        packet[16] = command;
        packet[17] = static_cast<uint8_t>((w >> 8) & 0xFF);
        packet[18] = static_cast<uint8_t>(w & 0xFF);
        packet[19] = static_cast<uint8_t>((h >> 8) & 0xFF);
        packet[20] = static_cast<uint8_t>(h & 0xFF);
        return packet;
    }

    inline std::vector<uint8_t> buildPacket(uint8_t type, uint8_t flags,
                                            int64_t timestampUs,
                                            const uint8_t* payload,
                                            uint32_t payloadLength) {
        std::vector<uint8_t> packet(HEADER_SIZE + payloadLength);
        packet[0] = MAGIC_1;
        packet[1] = MAGIC_2;
        packet[2] = type;
        packet[3] = flags;
        packet[4] = static_cast<uint8_t>((payloadLength >> 24) & 0xFF);
        packet[5] = static_cast<uint8_t>((payloadLength >> 16) & 0xFF);
        packet[6] = static_cast<uint8_t>((payloadLength >> 8) & 0xFF);
        packet[7] = static_cast<uint8_t>(payloadLength & 0xFF);

        uint64_t ts = static_cast<uint64_t>(timestampUs);
        packet[8]  = static_cast<uint8_t>((ts >> 56) & 0xFF);
        packet[9]  = static_cast<uint8_t>((ts >> 48) & 0xFF);
        packet[10] = static_cast<uint8_t>((ts >> 40) & 0xFF);
        packet[11] = static_cast<uint8_t>((ts >> 32) & 0xFF);
        packet[12] = static_cast<uint8_t>((ts >> 24) & 0xFF);
        packet[13] = static_cast<uint8_t>((ts >> 16) & 0xFF);
        packet[14] = static_cast<uint8_t>((ts >> 8) & 0xFF);
        packet[15] = static_cast<uint8_t>(ts & 0xFF);

        if (payload && payloadLength > 0) {
            memcpy(packet.data() + HEADER_SIZE, payload, payloadLength);
        }
        return packet;
    }

    inline std::vector<uint8_t> buildReverseAudioPacket(const uint8_t* pcmData,
                                                        uint32_t size,
                                                        int64_t timestampUs) {
        return buildPacket(TYPE_REVERSE_AUDIO, FLAG_NONE, timestampUs, pcmData, size);
    }

} // namespace phonecam

#endif // PHONECAM_PROTOCOL_H
