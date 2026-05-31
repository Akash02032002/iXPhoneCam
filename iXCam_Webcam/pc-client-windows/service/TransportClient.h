#pragma once
#ifndef PHONECAM_TRANSPORT_CLIENT_H
#define PHONECAM_TRANSPORT_CLIENT_H

#ifdef _WIN32
#include <WinSock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#else
typedef int socket_t;
#define INVALID_SOCK -1
#endif

#include <string>
#include <functional>
#include <mutex>

namespace phonecam {

/**
 * Base class for transport clients (USB/WiFi TCP and Bluetooth RFCOMM).
 */
class TransportClient {
public:
    TransportClient();
    virtual ~TransportClient();

    virtual bool connect(const std::string& address, int port) = 0;
    virtual void disconnect();
    virtual bool isConnected() const;
    socket_t getSocket() const { return m_socket; }

    /**
     * Send raw data to the phone.
     */
    bool sendData(const uint8_t* data, int size);

    /**
     * Send a control command to the phone.
     */
    bool sendCommand(uint8_t command);

protected:
    socket_t m_socket;
    bool m_connected;
    std::mutex m_sendMutex;

    static bool s_wsaInitialized;
    static void initWsa();
};

} // namespace phonecam

#endif // PHONECAM_TRANSPORT_CLIENT_H
