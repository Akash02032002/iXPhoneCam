#include "TransportClient.h"
#include "Protocol.h"
#include <cstdio>
#include <vector>

namespace phonecam {

bool TransportClient::s_wsaInitialized = false;

void TransportClient::initWsa() {
    if (!s_wsaInitialized) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            printf("TransportClient: WSAStartup failed: %d\n", result);
        } else {
            s_wsaInitialized = true;
        }
    }
}

TransportClient::TransportClient()
    : m_socket(INVALID_SOCK), m_connected(false) {
    initWsa();
}

TransportClient::~TransportClient() {
    disconnect();
}

void TransportClient::disconnect() {
    if (m_socket != INVALID_SOCK) {
        shutdown(m_socket, SD_BOTH);
        closesocket(m_socket);
        m_socket = INVALID_SOCK;
    }
    m_connected = false;
}

bool TransportClient::isConnected() const {
    return m_connected && m_socket != INVALID_SOCK;
}

bool TransportClient::sendData(const uint8_t* data, int size) {
    if (!isConnected()) return false;
    std::lock_guard<std::mutex> lock(m_sendMutex);

    int totalSent = 0;
    while (totalSent < size) {
        int result = send(m_socket, reinterpret_cast<const char*>(data + totalSent),
                          size - totalSent, 0);
        if (result <= 0) {
            printf("TransportClient: send failed\n");
            m_connected = false;
            return false;
        }
        totalSent += result;
    }
    return true;
}

bool TransportClient::sendCommand(uint8_t command) {
    auto packet = buildControlPacket(command);
    return sendData(packet.data(), static_cast<int>(packet.size()));
}

} // namespace phonecam
