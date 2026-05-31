#include "PacketReader.h"
#include <cstdio>
#include <cstring>

namespace phonecam {

PacketReader::PacketReader() {}
PacketReader::~PacketReader() {}

void PacketReader::onVideoFrame(
    std::function<void(const uint8_t*, int, int64_t, bool)> callback) {
    m_videoCallback = callback;
}

void PacketReader::onAudioData(
    std::function<void(const uint8_t*, int, int64_t)> callback) {
    m_audioCallback = callback;
}

void PacketReader::onHeartbeat(std::function<void()> callback) {
    m_heartbeatCallback = callback;
}

bool PacketReader::readExact(socket_t sock, uint8_t* buffer, int size) {
    int totalRead = 0;
    while (totalRead < size) {
        // Use select() with timeout to detect stalled connections
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);
        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;

        int sel = select(0, &readSet, nullptr, nullptr, &tv);
        if (sel == 0) {
            printf("PacketReader: recv timeout (10s), connection stalled\n");
            return false;
        }
        if (sel < 0) {
            printf("PacketReader: select error\n");
            return false;
        }

        int result = recv(sock, reinterpret_cast<char*>(buffer + totalRead),
                          size - totalRead, 0);
        if (result <= 0) {
            printf("PacketReader: recv failed or connection closed (result=%d)\n", result);
            return false;
        }
        totalRead += result;
    }
    return true;
}

bool PacketReader::readLoop(socket_t sock) {
    uint8_t headerBuf[HEADER_SIZE];
    std::vector<uint8_t> payloadBuf;
    payloadBuf.reserve(1024 * 1024); // 1MB initial reserve

    printf("PacketReader: Starting read loop\n");

    while (true) {
        // Read header
        if (!readExact(sock, headerBuf, HEADER_SIZE)) {
            printf("PacketReader: Failed to read header, disconnecting\n");
            return false;
        }

        // Parse header
        PacketHeader header;
        if (!parseHeader(headerBuf, header)) {
            printf("PacketReader: Invalid magic bytes, skipping\n");
            // Try to resync by reading one byte at a time until we find magic
            continue;
        }

        // Validate payload length
        if (header.payloadLength > 10 * 1024 * 1024) { // 10MB max
            printf("PacketReader: Payload too large (%u bytes), disconnecting\n",
                   header.payloadLength);
            return false;
        }

        // Read payload
        if (header.payloadLength > 0) {
            payloadBuf.resize(header.payloadLength);
            if (!readExact(sock, payloadBuf.data(), header.payloadLength)) {
                printf("PacketReader: Failed to read payload\n");
                return false;
            }
        }

        // Dispatch based on type
        switch (header.type) {
            case TYPE_VIDEO:
                if (m_videoCallback && header.payloadLength > 0) {
                    bool isKeyFrame = (header.flags & FLAG_KEYFRAME) != 0;
                    m_videoCallback(payloadBuf.data(), header.payloadLength,
                                    header.timestampUs, isKeyFrame);
                }
                break;

            case TYPE_AUDIO:
                if (m_audioCallback && header.payloadLength > 0) {
                    m_audioCallback(payloadBuf.data(), header.payloadLength,
                                    header.timestampUs);
                }
                break;

            case TYPE_HEARTBEAT:
                if (m_heartbeatCallback) {
                    m_heartbeatCallback();
                }
                break;

            case TYPE_CONTROL:
                // PC shouldn't receive control packets (it sends them)
                printf("PacketReader: Unexpected control packet received\n");
                break;

            default:
                printf("PacketReader: Unknown packet type 0x%02X\n", header.type);
                break;
        }
    }
}

} // namespace phonecam
