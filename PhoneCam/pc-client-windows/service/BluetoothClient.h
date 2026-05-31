#pragma once
#ifndef PHONECAM_BLUETOOTH_CLIENT_H
#define PHONECAM_BLUETOOTH_CLIENT_H

#include "TransportClient.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <ws2bth.h>
#include <BluetoothAPIs.h>
#pragma comment(lib, "Bthprops.lib")
#endif

namespace phonecam {

/**
 * Bluetooth RFCOMM client for connecting to the phone.
 * Requires the phone and PC to be paired via Bluetooth settings first.
 */
class BluetoothClient : public TransportClient {
public:
    BluetoothClient();
    ~BluetoothClient() override;

    /**
     * Connect to a paired Bluetooth device.
     * @param address Bluetooth address string (e.g., "AA:BB:CC:DD:EE:FF")
     * @param port    RFCOMM channel (ignored, uses service UUID)
     */
    bool connect(const std::string& address, int port) override;

    /**
     * Scan for paired Bluetooth devices.
     * Returns a list of device names and addresses.
     */
    struct BluetoothDevice {
        std::string name;
        std::string address;
        BTH_ADDR    bthAddr;
    };

    static std::vector<BluetoothDevice> getPairedDevices();

    /**
     * Convert MAC string "AA:BB:CC:DD:EE:FF" to BTH_ADDR.
     */
    static BTH_ADDR parseBthAddress(const std::string& address);

private:
    // PhoneCam service UUID: a1b2c3d4-e5f6-7890-abcd-ef1234567890
    static const GUID SERVICE_UUID;
};

} // namespace phonecam

#endif // PHONECAM_BLUETOOTH_CLIENT_H
