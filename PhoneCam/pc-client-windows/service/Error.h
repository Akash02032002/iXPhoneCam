#pragma once
#ifndef PHONECAM_ERROR_H
#define PHONECAM_ERROR_H

/**
 * Error codes and error handling utilities for PhoneCam.
 * Comprehensive error reporting with context and recovery suggestions.
 */

#include <string>
#include <map>

namespace phonecam {

// Error codes
enum class ErrorCode {
    // No error
    SUCCESS = 0,

    // Connection errors (1000-1099)
    ERR_CONNECTION_TIMEOUT = 1001,
    ERR_CONNECTION_REFUSED = 1002,
    ERR_CONNECTION_RESET = 1003,
    ERR_SOCKET_CREATION_FAILED = 1004,
    ERR_ADB_NOT_FOUND = 1005,
    ERR_ADB_NO_DEVICE = 1006,
    ERR_BLUETOOTH_PAIRING_FAILED = 1007,

    // Packet/Protocol errors (2000-2099)
    ERR_PACKET_MAGIC_INVALID = 2001,
    ERR_PACKET_TYPE_INVALID = 2002,
    ERR_PACKET_SIZE_INVALID = 2003,
    ERR_PACKET_CHECKSUM = 2004,
    ERR_PACKET_TIMEOUT = 2005,

    // Decoder errors (3000-3099)
    ERR_DECODER_INIT_FAILED = 3001,
    ERR_DECODER_CODEC_NOT_FOUND = 3002,
    ERR_DECODER_INVALID_STREAM = 3003,
    ERR_DECODER_FRAME_DROP = 3004,
    ERR_DECODER_PIXEL_FORMAT = 3005,

    // Shared memory errors (4000-4099)
    ERR_FRAMEBUFFER_CREATE_FAILED = 4001,
    ERR_FRAMEBUFFER_MAP_FAILED = 4002,
    ERR_FRAMEBUFFER_PERMISSION = 4003,

    // Audio errors (5000-5099)
    ERR_AUDIO_INIT_FAILED = 5001,
    ERR_AUDIO_DEVICE_NOT_FOUND = 5002,

    // System errors (6000-6099)
    ERR_OUT_OF_MEMORY = 6001,
    ERR_THREAD_CREATION_FAILED = 6002,
    ERR_INVALID_ARGUMENT = 6003,

    // Unknown/generic error
    ERR_UNKNOWN = 9999
};

struct ErrorContext {
    ErrorCode code = ErrorCode::SUCCESS;
    std::string message;
    std::string component;  // Which module reported error
    std::map<std::string, std::string> details;
    int systemError = 0;  // GetLastError() or errno
    
    std::string toString() const {
        std::string result;
        result += "[" + component + "] ";
        result += message;
        
        if (!details.empty()) {
            result += " (";
            for (const auto& [k, v] : details) {
                result += k + "=" + v + ", ";
            }
            result += ")";
        }
        
        if (systemError != 0) {
            result += " [system error: " + std::to_string(systemError) + "]";
        }
        
        return result;
    }
};

// Error helpers
inline std::string errorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS:
            return "SUCCESS";
        case ErrorCode::ERR_CONNECTION_TIMEOUT:
            return "Connection timeout";
        case ErrorCode::ERR_CONNECTION_REFUSED:
            return "Connection refused (is app running on phone?)";
        case ErrorCode::ERR_CONNECTION_RESET:
            return "Connection reset by peer";
        case ErrorCode::ERR_SOCKET_CREATION_FAILED:
            return "Failed to create socket";
        case ErrorCode::ERR_ADB_NOT_FOUND:
            return "ADB not found (Android SDK not installed?)";
        case ErrorCode::ERR_ADB_NO_DEVICE:
            return "No Android device found via ADB";
        case ErrorCode::ERR_BLUETOOTH_PAIRING_FAILED:
            return "Bluetooth pairing failed";
        case ErrorCode::ERR_PACKET_MAGIC_INVALID:
            return "Invalid packet magic bytes (stream corruption?)";
        case ErrorCode::ERR_PACKET_TYPE_INVALID:
            return "Unknown packet type";
        case ErrorCode::ERR_PACKET_SIZE_INVALID:
            return "Packet size out of bounds";
        case ErrorCode::ERR_DECODER_CODEC_NOT_FOUND:
            return "H.264 codec not found (FFmpeg installation issue?)";
        case ErrorCode::ERR_DECODER_INVALID_STREAM:
            return "Invalid H.264 stream (corrupted data?)";
        case ErrorCode::ERR_FRAMEBUFFER_CREATE_FAILED:
            return "Failed to create shared memory frame buffer";
        case ErrorCode::ERR_AUDIO_INIT_FAILED:
            return "Audio subsystem initialization failed";
        case ErrorCode::ERR_OUT_OF_MEMORY:
            return "Out of memory";
        default:
            return "Unknown error (" + std::to_string(static_cast<int>(code)) + ")";
    }
}

inline std::string getRecoverySuggestion(ErrorCode code) {
    switch (code) {
        case ErrorCode::ERR_CONNECTION_TIMEOUT:
            return "Check phone WiFi connection, try USB mode instead, or increase timeout";
        case ErrorCode::ERR_CONNECTION_REFUSED:
            return "Make sure PhoneCam app is running on the phone and permissions are granted";
        case ErrorCode::ERR_ADB_NOT_FOUND:
            return "Install Android SDK Platform Tools from https://developer.android.com/studio/releases/platform-tools";
        case ErrorCode::ERR_ADB_NO_DEVICE:
            return "Connect Android device via USB, enable USB Debugging, and run 'adb devices' to verify";
        case ErrorCode::ERR_DECODER_CODEC_NOT_FOUND:
            return "Ensure FFmpeg libraries are installed at C:\\ffmpeg with proper include/lib folders";
        case ErrorCode::ERR_FRAMEBUFFER_CREATE_FAILED:
            return "Check Windows permissions, try restarting PhoneCamService, or reinstall DirectShow driver";
        default:
            return "Check logs for more details, restart the service, and verify system requirements";
    }
}

// Error factory functions
inline ErrorContext makeError(ErrorCode code, const std::string& component,
                             const std::string& message = "") {
    ErrorContext ctx;
    ctx.code = code;
    ctx.component = component;
    ctx.message = message.empty() ? errorCodeToString(code) : message;
    return ctx;
}

inline ErrorContext makeSystemError(ErrorCode code, const std::string& component,
                                    int sysError) {
    ErrorContext ctx = makeError(code, component);
    ctx.systemError = sysError;
    return ctx;
}

} // namespace phonecam

#endif // PHONECAM_ERROR_H
