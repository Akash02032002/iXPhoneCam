#pragma once
#ifndef PHONECAM_PACKET_READER_H
#define PHONECAM_PACKET_READER_H

#include "Protocol.h"
#include <cstdint>
#include <vector>
#include <functional>

#ifdef _WIN32
#include <WinSock2.h>
typedef SOCKET socket_t;
#else
typedef int socket_t;
#endif

namespace phonecam {

/**
 * Reads PhoneCam protocol packets from a socket.
 * Handles buffering and packet reassembly.
 */
class PacketReader {
public:
    PacketReader();
    ~PacketReader();

    /**
     * Set callback for received video frames.
     */
    void onVideoFrame(std::function<void(const uint8_t* data, int size,
                                          int64_t timestampUs, bool isKeyFrame)> callback);

    /**
     * Set callback for received audio data.
     */
    void onAudioData(std::function<void(const uint8_t* data, int size,
                                         int64_t timestampUs)> callback);

    /**
     * Set callback for heartbeat received.
     */
    void onHeartbeat(std::function<void()> callback);

    /**
     * Start reading packets from the given socket.
     * Blocks until the socket is closed or an error occurs.
     */
    bool readLoop(socket_t sock);

private:
    bool readExact(socket_t sock, uint8_t* buffer, int size);

    std::function<void(const uint8_t*, int, int64_t, bool)> m_videoCallback;
    std::function<void(const uint8_t*, int, int64_t)>       m_audioCallback;
    std::function<void()>                                     m_heartbeatCallback;
};

} // namespace phonecam

#endif // PHONECAM_PACKET_READER_H
