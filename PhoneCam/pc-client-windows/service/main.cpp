/**
 * PhoneCam Service - Main Entry Point
 *
 * This is the background process that:
 * 1. Connects to the phone via USB (ADB) or Bluetooth
 * 2. Receives H.264 encoded video frames
 * 3. Decodes them with FFmpeg
 * 4. Writes RGB frames to shared memory (FrameBuffer)
 * 5. The DirectShow virtual camera driver reads from shared memory
 *
 * Usage:
 *   PhoneCamService.exe --usb                   (connect via USB/ADB)
 *   PhoneCamService.exe --wifi 192.168.1.100     (connect via WiFi)
 *   PhoneCamService.exe --bluetooth AA:BB:CC:DD:EE:FF
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <csignal>
#include <memory>
#include <sstream>
#include <algorithm>
#include <cmath>

#include "Decoder.h"
#include "PacketReader.h"
#include "UsbClient.h"
#include "BluetoothClient.h"
#include "FrameBuffer.h"
#include "AudioPlayer.h"
#include "ReverseAudioCapture.h"
#include "Protocol.h"
#include "Logger.h"
#include "Error.h"

using namespace phonecam;

static std::atomic<bool> g_running(true);
static Logger& g_logger = Logger::getInstance();

void signalHandler(int sig) {
    g_logger.warn("Received shutdown signal");
    g_running = false;
}

void printUsage(const char* progName) {
    printf("PhoneCam Service v1.0\n");
    printf("Usage:\n");
    printf("  %s --usb                           Connect via USB (requires ADB)\n", progName);
    printf("  %s --wifi <ip-address>              Connect via WiFi\n", progName);
    printf("  %s --bluetooth <mac-address>        Connect via Bluetooth\n", progName);
    printf("  %s --list-bt                        List paired Bluetooth devices\n", progName);
    printf("\nOptions:\n");
    printf("  --width <N>     Resolution width (default: 1280)\n");
    printf("  --height <N>    Resolution height (default: 720)\n");
    printf("  --fps <N>       Virtual camera frame rate (default: 30)\n");
    printf("  --port <N>      TCP port (default: 4747)\n");
    printf("  --no-audio      Disable audio playback\n");
    printf("  --phone-speaker Send PC speaker audio to the phone speaker\n");
    printf("  --phone-mic-output <name> Route phone mic audio to a named Windows output device\n");
}

int main(int argc, char* argv[]) {
    // Disable stdout buffering so logs appear immediately even when redirected
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Keep logs beside the portable app package. ProgramData/LocalAppData can
    // be blocked in sandboxed or locked-down Windows accounts.
    std::string logDir = "logs";
    
    if (!g_logger.initialize(logDir, LogLevel::INFO)) {
        std::string fallbackLogDir = ".";
        if (!g_logger.initialize(fallbackLogDir, LogLevel::INFO)) {
            fprintf(stderr, "WARNING: Failed to initialize file logging, using console only\n");
        }
    }
    
    g_logger.info("PhoneCam Service starting");
    g_logger.info("Version: 1.0.0");

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Parse arguments
    std::string mode;
    std::string address = "127.0.0.1";
    int port = DEFAULT_PORT;
    int width = 1280;
    int height = 720;
    int fps = 30;
    bool initialAudioEnabled = true;
    bool enablePhoneSpeaker = false;
    std::string phoneMicOutputName;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--usb") == 0) {
            mode = "usb";
            address = "127.0.0.1";
        } else if (strcmp(argv[i], "--wifi") == 0 && i + 1 < argc) {
            mode = "wifi";
            address = argv[++i];
        } else if (strcmp(argv[i], "--bluetooth") == 0 && i + 1 < argc) {
            mode = "bluetooth";
            address = argv[++i];
        } else if (strcmp(argv[i], "--list-bt") == 0) {
            auto devices = BluetoothClient::getPairedDevices();
            if (devices.empty()) {
                g_logger.info("No paired Bluetooth devices found");
            } else {
                g_logger.info("Paired Bluetooth devices:");
                for (size_t j = 0; j < devices.size(); j++) {
                    g_logger.info("  [" + std::to_string(j + 1) + "] " + 
                                 devices[j].name + " (" + devices[j].address + ")");
                }
            }
            return 0;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            int parsedPort = atoi(argv[++i]);
            if (parsedPort <= 0 || parsedPort > 65535) {
                g_logger.error("Invalid port number: " + std::to_string(parsedPort));
                return 1;
            }
            port = parsedPort;
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = atoi(argv[++i]);
            if (width <= 0 || width > 7680) {
                g_logger.error("Invalid width: " + std::to_string(width));
                width = 1280;
            }
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            height = atoi(argv[++i]);
            if (height <= 0 || height > 4320) {
                g_logger.error("Invalid height: " + std::to_string(height));
                height = 720;
            }
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps = atoi(argv[++i]);
            if (fps <= 0 || fps > 60) {
                g_logger.error("Invalid fps: " + std::to_string(fps));
                fps = 30;
            }
        } else if (strcmp(argv[i], "--no-audio") == 0) {
            initialAudioEnabled = false;
        } else if (strcmp(argv[i], "--phone-speaker") == 0) {
            enablePhoneSpeaker = true;
        } else if (strcmp(argv[i], "--phone-mic-output") == 0 && i + 1 < argc) {
            phoneMicOutputName = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (mode.empty()) {
        printUsage(argv[0]);
        return 1;
    }

    g_logger.info("Configuration", {
        {"mode", mode},
        {"address", address},
        {"port", std::to_string(port)},
        {"resolution", std::to_string(width) + "x" + std::to_string(height)},
        {"fps", std::to_string(fps)},
        {"audio", initialAudioEnabled ? "enabled" : "disabled"},
        {"phone_speaker", enablePhoneSpeaker ? "enabled" : "disabled"},
        {"phone_mic_output", phoneMicOutputName.empty() ? "default" : phoneMicOutputName}
    });

    // ---- Step 1: Initialize shared memory frame buffer ----
    auto frameBuffer = std::make_unique<FrameBuffer>();
    if (!frameBuffer->createWriter(width, height, fps)) {
        auto err = makeError(ErrorCode::ERR_FRAMEBUFFER_CREATE_FAILED, "Main");
        g_logger.error(err.toString());
        g_logger.error("Recovery: " + getRecoverySuggestion(err.code));
        return 1;
    }
    g_logger.info("Shared memory frame buffer created", 
                 {{"size", std::to_string(frameBuffer->getFrameSize())}});

    // ---- Step 2: Initialize H.264 decoder ----
    auto decoder = std::make_unique<Decoder>();
    if (!decoder->initialize(width, height)) {
        auto err = makeError(ErrorCode::ERR_DECODER_INIT_FAILED, "Main");
        g_logger.error(err.toString());
        g_logger.error("Recovery: " + getRecoverySuggestion(err.code));
        return 1;
    }
    g_logger.info("H.264 decoder initialized");

    // ---- Step 3: Connect to phone ----
    std::unique_ptr<TransportClient> transport;

    if (mode == "usb") {
        g_logger.info("Setting up USB/ADB connection...");
        if (!UsbClient::isDeviceConnected()) {
            auto err = makeError(ErrorCode::ERR_ADB_NO_DEVICE, "UsbClient");
            g_logger.warn(err.toString());
        }
        if (!UsbClient::setupAdbForward(port)) {
            auto err = makeError(ErrorCode::ERR_ADB_NOT_FOUND, "UsbClient");
            g_logger.warn(err.toString());
        }

        auto usb = std::make_unique<UsbClient>();
        if (!usb->connect(address, port)) {
            auto err = makeError(ErrorCode::ERR_CONNECTION_REFUSED, "UsbClient");
            g_logger.error(err.toString());
            g_logger.error("Recovery: " + getRecoverySuggestion(err.code));
            return 1;
        }
        transport = std::move(usb);

    } else if (mode == "wifi") {
        g_logger.info("Setting up WiFi connection to " + address);
        auto wifi = std::make_unique<UsbClient>();
        if (!wifi->connect(address, port)) {
            auto err = makeError(ErrorCode::ERR_CONNECTION_TIMEOUT, "WifiClient");
            g_logger.error(err.toString());
            return 1;
        }
        transport = std::move(wifi);

    } else if (mode == "bluetooth") {
        g_logger.info("Setting up Bluetooth connection to " + address);
        auto bt = std::make_unique<BluetoothClient>();
        if (!bt->connect(address, 0)) {
            auto err = makeError(ErrorCode::ERR_CONNECTION_REFUSED, "BluetoothClient");
            g_logger.error(err.toString());
            return 1;
        }
        transport = std::move(bt);
    }

    g_logger.info("Connected to phone!");

    // ---- Step 3.5: Initialize audio player ----
    auto audioPlayer = std::make_unique<AudioPlayer>();
    std::atomic<uint64_t> audioPacketsReceived(0);
    std::atomic<int> audioPeakPcm(0);
    std::atomic<int> audioRmsPcm(0);
    std::atomic<bool> enableAudio(initialAudioEnabled);

    if (enableAudio.load()) {
        if (audioPlayer->initialize(phoneMicOutputName)) {
            g_logger.info("Audio playback initialized");
        } else {
            auto err = makeError(ErrorCode::ERR_AUDIO_INIT_FAILED, "AudioPlayer");
            g_logger.warn(err.toString());
            g_logger.warn("Continuing without audio");
            enableAudio.store(false);
        }
    }

    // ---- Step 4: Setup packet reader ----
    PacketReader reader;
    std::atomic<uint64_t> framesDecoded(0);
    std::atomic<uint64_t> decodingErrors(0);
    std::atomic<uint64_t> bytesReceived(0);
    std::atomic<uint64_t> reverseAudioPacketsSent(0);

    auto reverseAudio = std::make_unique<ReverseAudioCapture>();
    auto startReverseAudio = [&]() {
        if (!transport || reverseAudio->isRunning()) return;
        reverseAudio->start([&](const uint8_t* data, int size, int64_t timestampUs) {
            if (!transport || !transport->isConnected() || !data || size <= 0) return;
            auto packet = buildReverseAudioPacket(data, static_cast<uint32_t>(size), timestampUs);
            if (transport->sendData(packet.data(), static_cast<int>(packet.size()))) {
                reverseAudioPacketsSent++;
            }
        });
        g_logger.info("Reverse audio enabled: PC speaker audio is being sent to phone speaker");
    };

    auto stopReverseAudio = [&]() {
        if (reverseAudio && reverseAudio->isRunning()) {
            reverseAudio->stop();
            g_logger.info("Reverse audio disabled");
        }
    };

    if (enablePhoneSpeaker) {
        startReverseAudio();
    }

    // Validate frame buffer allocation
    int frameSize = width * height * 3;
    if (frameSize <= 0 || frameSize > 100 * 1024 * 1024) {
        auto err = makeError(ErrorCode::ERR_INVALID_ARGUMENT, "Main", 
                           "Frame size calculation overflow");
        g_logger.error(err.toString());
        return 1;
    }

    auto rgbFrame = std::make_unique<uint8_t[]>(frameSize);
    if (!rgbFrame) {
        auto err = makeError(ErrorCode::ERR_OUT_OF_MEMORY, "Main");
        g_logger.error(err.toString());
        return 1;
    }

    int decoderErrors = 0;

    reader.onVideoFrame([&](const uint8_t* data, int size,
                             int64_t timestampUs, bool isKeyFrame) {
        if (data == nullptr || size <= 0 || size > 10*1024*1024) {
            g_logger.warn("Invalid video frame received");
            return;
        }

        bytesReceived += size;

        if (!decoder->decodeFrame(data, size, timestampUs)) {
            decodingErrors++;
            if (decodingErrors % 100 == 0) {
                g_logger.warn("Decoder error #" + std::to_string(decodingErrors) + 
                             " (requesting keyframe)");
                if (transport) {
                    transport->sendCommand(CMD_REQUEST_KEYFRAME);
                }
            }
            return;
        }

        int decoded = decoder->getDecodedFrame(rgbFrame.get(), frameSize);
        if (decoded > 0) {
            frameBuffer->writeFrame(rgbFrame.get(), decoded);
            framesDecoded++;
        }
    });

    reader.onAudioData([&](const uint8_t* data, int size, int64_t timestampUs) {
        if (enableAudio.load() && data && size > 0) {
            int peak = 0;
            double sumSquares = 0.0;
            int sampleCount = size / 2;
            const int16_t* samples = reinterpret_cast<const int16_t*>(data);
            for (int i = 0; i < sampleCount; ++i) {
                int value = std::abs(static_cast<int>(samples[i]));
                peak = (std::max)(peak, value);
                sumSquares += static_cast<double>(samples[i]) * samples[i];
            }
            audioPeakPcm.store(peak);
            audioRmsPcm.store(sampleCount > 0
                ? static_cast<int>(std::sqrt(sumSquares / sampleCount))
                : 0);

            audioPlayer->play(data, size);
            audioPacketsReceived++;
        }
    });

    reader.onHeartbeat([]() {
        // Connection is alive - could log periodic heartbeats if needed
    });

    // ---- Step 5: Stats thread ----
    std::thread statsThread([&]() {
        uint64_t lastFrames = 0;
        uint64_t lastBytes = 0;
        int staleFrameCount = 0;

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            uint64_t currentFrames = framesDecoded.load();
            uint64_t currentBytes = bytesReceived.load();

            uint64_t fps = (currentFrames - lastFrames) / 2;
            uint64_t kbps = ((currentBytes - lastBytes) * 8) / (2 * 1000);
            uint64_t audioTotal = audioPacketsReceived.load();
            uint64_t reverseAudioTotal = reverseAudioPacketsSent.load();
            uint64_t errors = decodingErrors.load();
            int audioPeakPct = (audioPeakPcm.load() * 100) / 32767;
            int audioRmsPct = (audioRmsPcm.load() * 100) / 32767;

            g_logger.info("Stats", {
                {"fps", std::to_string(fps)},
                {"kbps", std::to_string(kbps)},
                {"frames", std::to_string(currentFrames)},
                {"audio_packets", std::to_string(audioTotal)},
                {"audio_peak_pct", std::to_string(audioPeakPct)},
                {"audio_rms_pct", std::to_string(audioRmsPct)},
                {"reverse_audio_packets", std::to_string(reverseAudioTotal)},
                {"decoder_errors", std::to_string(errors)}
            });

            // Alert on stalled decoder output. If bytes are still arriving, the
            // transport is alive but the decoder likely needs a fresh IDR frame.
            if (currentFrames == lastFrames) {
                staleFrameCount++;
                if (staleFrameCount >= 2 && transport && transport->isConnected()) {
                    g_logger.warn("No decoded frames; requesting keyframe");
                    transport->sendCommand(CMD_REQUEST_KEYFRAME);
                }
                if (staleFrameCount >= 8) {  // 16 seconds of no decoded frames
                    g_logger.warn("No decoded frames for 16 seconds");
                    if (currentBytes == lastBytes && currentFrames > 0 && transport && transport->isConnected()) {
                        g_logger.warn("Forcing reconnect because transport is stalled");
                        transport->disconnect();
                    }
                }
            } else {
                staleFrameCount = 0;
            }

            lastFrames = currentFrames;
            lastBytes = currentBytes;
        }
    });
    statsThread.detach();

    // ---- Step 5.5: Stdin command reader (for WPF UI control) ----
    // Reads single-line commands from stdin and sends them to the phone
    std::thread stdinThread([&]() {
        char line[256];
        while (g_running && fgets(line, sizeof(line), stdin)) {
            std::string cmd(line);
            // Remove trailing newline
            while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
                cmd.pop_back();

            if (cmd == "flip") {
                transport->sendCommand(CMD_SWITCH_CAMERA);
                printf("Command sent: flip camera\n");
            } else if (cmd == "rotate") {
                transport->sendCommand(CMD_ROTATE_CAMERA);
                printf("Command sent: rotate camera\n");
            } else if (cmd == "flash") {
                transport->sendCommand(CMD_TOGGLE_FLASH);
                printf("Command sent: toggle flash\n");
            } else if (cmd == "focus-on" || cmd == "focus-off") {
                uint16_t enabled = (cmd == "focus-on") ? 1 : 0;
                auto pkt = buildControlPacket2(CMD_SET_AUTOFOCUS_MODE, enabled);
                transport->sendData(pkt.data(), static_cast<int>(pkt.size()));
                printf("Command sent: autofocus %s\n", enabled ? "on" : "off");
            } else if (cmd == "focus") {
                transport->sendCommand(CMD_AUTOFOCUS);
                printf("Command sent: autofocus\n");
            } else if (cmd == "keyframe") {
                transport->sendCommand(CMD_REQUEST_KEYFRAME);
                printf("Command sent: request keyframe\n");
            } else if (cmd.rfind("zoom ", 0) == 0) {
                // zoom <zoomX100>  e.g. "zoom 250" = 2.5x
                int zoomX100 = atoi(cmd.c_str() + 5);
                if (zoomX100 >= 100 && zoomX100 <= 10000) {
                    auto pkt = buildControlPacket2(CMD_SET_ZOOM, static_cast<uint16_t>(zoomX100));
                    transport->sendData(pkt.data(), static_cast<int>(pkt.size()));
                    printf("Command sent: zoom %d (%.1fx)\n", zoomX100, zoomX100 / 100.0);
                }
            } else if (cmd.rfind("bitrate ", 0) == 0) {
                // bitrate <bps>  e.g. "bitrate 5000000"
                int bitrate = atoi(cmd.c_str() + 8);
                if (bitrate > 0) {
                    auto pkt = buildControlPacket4(CMD_SET_BITRATE, static_cast<uint32_t>(bitrate));
                    transport->sendData(pkt.data(), static_cast<int>(pkt.size()));
                    printf("Command sent: bitrate %d bps\n", bitrate);
                }
            } else if (cmd.rfind("fps ", 0) == 0) {
                int requestedFps = atoi(cmd.c_str() + 4);
                if (requestedFps > 0 && requestedFps <= 60) {
                    fps = requestedFps;
                    frameBuffer->setFps(fps);
                    printf("Command sent: fps %d\n", fps);
                }
            } else if (cmd.rfind("resolution ", 0) == 0) {
                // resolution <w> <h>  e.g. "resolution 1280 720"
                int w2 = 0, h2 = 0;
                if (sscanf(cmd.c_str() + 11, "%d %d", &w2, &h2) == 2 && w2 > 0 && h2 > 0) {
                    auto pkt = buildControlPacketRes(CMD_SET_RESOLUTION,
                                                     static_cast<uint16_t>(w2),
                                                     static_cast<uint16_t>(h2));
                    transport->sendData(pkt.data(), static_cast<int>(pkt.size()));
                    printf("Command sent: resolution %dx%d\n", w2, h2);
                }
            } else if (cmd == "audio-on") {
                enableAudio.store(true);
                if (transport && transport->isConnected()) {
                    transport->sendCommand(CMD_AUDIO_ON);
                }
                if (!audioPlayer->isPlaying() && !audioPlayer->initialize(phoneMicOutputName)) {
                    enableAudio.store(false);
                    printf("Audio playback could not be enabled\n");
                } else {
                    printf("Phone microphone streaming and audio playback enabled\n");
                }
            } else if (cmd == "audio-off") {
                printf("audio-off ignored: phone microphone audio stays always on while streaming\n");
            } else if (cmd == "phone-speaker-on") {
                enablePhoneSpeaker = true;
                startReverseAudio();
                printf("Phone speaker reverse audio enabled\n");
            } else if (cmd == "phone-speaker-off") {
                enablePhoneSpeaker = false;
                stopReverseAudio();
                printf("Phone speaker reverse audio disabled\n");
            } else if (cmd == "quit" || cmd == "exit") {
                g_running = false;
                break;
            }
        }
    });
    stdinThread.detach();

    // ---- Step 6: Main receive loop with auto-reconnection ----
    g_logger.info("Starting packet receive loop... (Ctrl+C to stop)");
    
    // Exponential backoff for reconnection
    const int MAX_RECONNECT_RETRIES = 10;
    int reconnect_ms = 500;
    const int MAX_BACKOFF_MS = 30000;
    int reconnect_attempts = 0;

    while (g_running) {
        reader.readLoop(transport->getSocket());

        if (!g_running) break;

        g_logger.warn("Connection lost");
        transport->disconnect();

        if (reconnect_attempts >= MAX_RECONNECT_RETRIES) {
            g_logger.error("Max reconnection attempts exhausted");
            break;
        }

        reconnect_attempts++;
        g_logger.info("Reconnecting", {
            {"attempt", std::to_string(reconnect_attempts)},
            {"delay_ms", std::to_string(reconnect_ms)}
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_ms));

        if (!g_running) break;

        // Re-setup ADB forward for USB mode
        if (mode == "usb") {
            UsbClient::setupAdbForward(port);
        }

        if (!transport->connect(address, port)) {
            g_logger.warn("Reconnection attempt failed, will retry");
            // Exponential backoff: 500ms → 1000ms → 2000ms ... up to 30s
            reconnect_ms = (std::min)(reconnect_ms * 2, MAX_BACKOFF_MS);
            continue;
        }

        g_logger.info("Reconnected successfully");
        reconnect_attempts = 0;  // Reset on successful reconnect
        reconnect_ms = 500;  // Reset backoff
        if (enablePhoneSpeaker) {
            startReverseAudio();
        }
    }

    // ---- Cleanup ----
    g_logger.info("Shutting down service");
    g_running = false;

    // Wait for threads to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Cleanup in reverse order
    decoder->shutdown();
    frameBuffer->close();
    if (audioPlayer) {
        audioPlayer->stop();
    }
    if (reverseAudio) {
        reverseAudio->stop();
    }

    g_logger.info("PhoneCam Service stopped successfully");
    return 0;
}
