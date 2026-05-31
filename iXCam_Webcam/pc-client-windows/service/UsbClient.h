#pragma once
#ifndef PHONECAM_USB_CLIENT_H
#define PHONECAM_USB_CLIENT_H

#include "TransportClient.h"

namespace phonecam {

/**
 * TCP client for connecting to the phone over USB (via ADB port forward)
 * or directly over WiFi.
 *
 * USB workflow:
 *   1. User runs: adb forward tcp:4747 tcp:4747
 *   2. This client connects to localhost:4747
 *   3. ADB tunnels the TCP connection over USB to the phone
 *
 * WiFi workflow:
 *   1. User enters phone's WiFi IP address
 *   2. This client connects to <phone-ip>:4747
 */
class UsbClient : public TransportClient {
public:
    UsbClient();
    ~UsbClient() override;

    /**
     * Connect to the phone.
     * @param address IP address ("127.0.0.1" for USB, phone IP for WiFi)
     * @param port    Port number (default 4747)
     */
    bool connect(const std::string& address, int port) override;

    /**
     * Attempt ADB port forwarding automatically.
     * Runs: adb forward tcp:<port> tcp:<port>
     * @return true if adb command succeeded
     */
    static bool setupAdbForward(int port = 4747);

    /**
     * Check if an Android device is connected via USB.
     * Runs: adb devices
     */
    static bool isDeviceConnected();

    /**
     * Locate adb.exe from PATH or the standard Android SDK install location.
     */
    static std::string findAdbExecutable();
};

} // namespace phonecam

#endif // PHONECAM_USB_CLIENT_H
