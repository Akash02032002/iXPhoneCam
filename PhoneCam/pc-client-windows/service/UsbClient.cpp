#include "UsbClient.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace phonecam {

UsbClient::UsbClient() {}
UsbClient::~UsbClient() {}

bool UsbClient::connect(const std::string& address, int port) {
    if (isConnected()) disconnect();

    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCK) {
        printf("UsbClient: Failed to create socket\n");
        return false;
    }

    // Set TCP_NODELAY for low latency
    int flag = 1;
    setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&flag), sizeof(flag));

    // Set receive buffer
    int bufSize = 256 * 1024; // 256KB
    setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

    // Set recv timeout (10 seconds) to detect stalled connections
    DWORD timeout = 10000;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    // Connect
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, address.c_str(), &serverAddr.sin_addr);

    printf("UsbClient: Connecting to %s:%d...\n", address.c_str(), port);

    int result = ::connect(m_socket, reinterpret_cast<sockaddr*>(&serverAddr),
                           sizeof(serverAddr));
    if (result == SOCKET_ERROR) {
        printf("UsbClient: Connection failed (error=%d)\n", WSAGetLastError());
        closesocket(m_socket);
        m_socket = INVALID_SOCK;
        return false;
    }

    m_connected = true;
    printf("UsbClient: Connected to %s:%d\n", address.c_str(), port);
    return true;
}

bool UsbClient::setupAdbForward(int port) {
    std::string adbPath = findAdbExecutable();
    if (adbPath.empty()) {
        printf("UsbClient: ADB not found. Install Android SDK Platform Tools or add adb.exe to PATH\n");
        return false;
    }

    char cmd[1024];
    sprintf_s(cmd, "\"%s\" forward tcp:%d tcp:%d", adbPath.c_str(), port, port);
    printf("UsbClient: Running '%s'\n", cmd);

    int result = system(cmd);
    if (result == 0) {
        printf("UsbClient: ADB forwarding set up successfully\n");
        return true;
    } else {
        printf("UsbClient: ADB forwarding failed (result=%d)\n", result);
        return false;
    }
}

bool UsbClient::isDeviceConnected() {
    std::string adbPath = findAdbExecutable();
    if (adbPath.empty()) {
        printf("UsbClient: ADB not found. Install Android SDK Platform Tools or add adb.exe to PATH\n");
        return false;
    }

    std::string command = "\"" + adbPath + "\" devices 2>&1";
    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) return false;

    char buffer[1024];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    int exitCode = _pclose(pipe);

    // Check if any device is listed (line with "device" after header)
    if (exitCode == 0 && output.find("device") != std::string::npos) {
        // Count lines with "device" (excluding "List of devices attached")
        size_t pos = output.find('\n');
        while (pos != std::string::npos) {
            std::string line = output.substr(pos + 1);
            if (line.find("device") != std::string::npos &&
                line.find("List") == std::string::npos) {
                return true;
            }
            pos = output.find('\n', pos + 1);
        }
    }
    return false;
}

std::string UsbClient::findAdbExecutable() {
    char resolvedPath[MAX_PATH] = {};
    if (SearchPathA(nullptr, "adb.exe", nullptr, MAX_PATH, resolvedPath, nullptr) > 0) {
        return resolvedPath;
    }

    std::vector<std::string> candidates;

    char localAppData[MAX_PATH] = {};
    if (GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        candidates.push_back(std::string(localAppData) + "\\Android\\Sdk\\platform-tools\\adb.exe");
    }

    char androidHome[MAX_PATH] = {};
    if (GetEnvironmentVariableA("ANDROID_HOME", androidHome, MAX_PATH) > 0) {
        candidates.push_back(std::string(androidHome) + "\\platform-tools\\adb.exe");
    }

    char androidSdkRoot[MAX_PATH] = {};
    if (GetEnvironmentVariableA("ANDROID_SDK_ROOT", androidSdkRoot, MAX_PATH) > 0) {
        candidates.push_back(std::string(androidSdkRoot) + "\\platform-tools\\adb.exe");
    }

    for (const auto& candidate : candidates) {
        DWORD attrs = GetFileAttributesA(candidate.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return candidate;
        }
    }

    return {};
}

} // namespace phonecam
