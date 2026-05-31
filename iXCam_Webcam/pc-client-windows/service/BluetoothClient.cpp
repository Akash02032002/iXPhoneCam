#include "BluetoothClient.h"
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace phonecam {

// Must match the UUID in BluetoothTransport.kt on Android
// a1b2c3d4-e5f6-7890-abcd-ef1234567890
const GUID BluetoothClient::SERVICE_UUID = {
    0xa1b2c3d4, 0xe5f6, 0x7890,
    { 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90 }
};

BluetoothClient::BluetoothClient() {}
BluetoothClient::~BluetoothClient() {}

bool BluetoothClient::connect(const std::string& address, int /*port*/) {
    if (isConnected()) disconnect();

    BTH_ADDR bthAddr = parseBthAddress(address);
    if (bthAddr == 0) {
        printf("BluetoothClient: Invalid Bluetooth address: %s\n", address.c_str());
        return false;
    }

    // Create Bluetooth socket
    m_socket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (m_socket == INVALID_SOCK) {
        printf("BluetoothClient: Failed to create BT socket (error=%d)\n", WSAGetLastError());
        return false;
    }

    // Setup address structure
    SOCKADDR_BTH btAddr = {};
    btAddr.addressFamily = AF_BTH;
    btAddr.btAddr = bthAddr;
    btAddr.serviceClassId = SERVICE_UUID;
    btAddr.port = BT_PORT_ANY;

    printf("BluetoothClient: Connecting to %s...\n", address.c_str());

    int result = ::connect(m_socket, reinterpret_cast<sockaddr*>(&btAddr),
                           sizeof(btAddr));
    if (result == SOCKET_ERROR) {
        printf("BluetoothClient: Connection failed (error=%d)\n", WSAGetLastError());
        closesocket(m_socket);
        m_socket = INVALID_SOCK;
        return false;
    }

    m_connected = true;
    printf("BluetoothClient: Connected via Bluetooth to %s\n", address.c_str());
    return true;
}

std::vector<BluetoothClient::BluetoothDevice> BluetoothClient::getPairedDevices() {
    std::vector<BluetoothDevice> devices;

    BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams = {};
    searchParams.dwSize = sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS);
    searchParams.fReturnAuthenticated = TRUE;
    searchParams.fReturnRemembered = TRUE;
    searchParams.fReturnConnected = TRUE;
    searchParams.fReturnUnknown = FALSE;
    searchParams.fIssueInquiry = FALSE;
    searchParams.cTimeoutMultiplier = 0;

    BLUETOOTH_DEVICE_INFO deviceInfo = {};
    deviceInfo.dwSize = sizeof(BLUETOOTH_DEVICE_INFO);

    HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&searchParams, &deviceInfo);
    if (hFind == nullptr) {
        printf("BluetoothClient: No paired devices found\n");
        return devices;
    }

    do {
        BluetoothDevice dev;

        // Convert wide name to string
        char name[256];
        WideCharToMultiByte(CP_UTF8, 0, deviceInfo.szName, -1, name, 256, nullptr, nullptr);
        dev.name = name;

        // Format address as string
        BTH_ADDR addr = deviceInfo.Address.ullLong;
        std::ostringstream ss;
        ss << std::hex << std::uppercase << std::setfill('0')
           << std::setw(2) << ((addr >> 40) & 0xFF) << ":"
           << std::setw(2) << ((addr >> 32) & 0xFF) << ":"
           << std::setw(2) << ((addr >> 24) & 0xFF) << ":"
           << std::setw(2) << ((addr >> 16) & 0xFF) << ":"
           << std::setw(2) << ((addr >> 8) & 0xFF) << ":"
           << std::setw(2) << (addr & 0xFF);
        dev.address = ss.str();
        dev.bthAddr = addr;

        devices.push_back(dev);
        printf("BluetoothClient: Found paired device: %s (%s)\n",
               dev.name.c_str(), dev.address.c_str());

    } while (BluetoothFindNextDevice(hFind, &deviceInfo));

    BluetoothFindDeviceClose(hFind);
    return devices;
}

BTH_ADDR BluetoothClient::parseBthAddress(const std::string& address) {
    BTH_ADDR result = 0;
    unsigned int bytes[6];

    if (sscanf_s(address.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X",
                 &bytes[0], &bytes[1], &bytes[2],
                 &bytes[3], &bytes[4], &bytes[5]) == 6) {
        for (int i = 0; i < 6; i++) {
            result = (result << 8) | (bytes[i] & 0xFF);
        }
    }
    return result;
}

} // namespace phonecam
