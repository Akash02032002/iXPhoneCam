#pragma once
#ifndef PHONECAM_AUDIO_PLAYER_H
#define PHONECAM_AUDIO_PLAYER_H

#include <Windows.h>
#include <mmsystem.h>
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>
#include <atomic>
#include <string>

#pragma comment(lib, "winmm.lib")

namespace phonecam {

/**
 * Plays raw PCM audio received from the phone through the PC speakers.
 * Uses Windows waveOut API for low-latency audio output.
 *
 * Audio format: 44100 Hz, 16-bit, Mono (matches Android AudioCapture)
 */
class AudioPlayer {
public:
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int CHANNELS = 1;
    static constexpr int BITS_PER_SAMPLE = 16;
    static constexpr int NUM_BUFFERS = 4;
    static constexpr int BUFFER_SIZE = 8820;  // ~100ms of audio at 44100Hz mono 16-bit

    AudioPlayer();
    ~AudioPlayer();

    bool initialize(const std::string& preferredDeviceName = "");
    void play(const uint8_t* pcmData, int size);
    void stop();
    bool isPlaying() const { return m_playing; }
    void setVolume(float volume);  // 0.0 - 1.0

private:
    static void CALLBACK waveOutCallback(HWAVEOUT hwo, UINT uMsg,
                                          DWORD_PTR dwInstance,
                                          DWORD_PTR dwParam1,
                                          DWORD_PTR dwParam2);
    void onBufferDone(WAVEHDR* header);
    bool submitBuffer(const uint8_t* data, int size);

    HWAVEOUT m_hWaveOut = nullptr;
    WAVEFORMATEX m_waveFormat = {};
    std::atomic<bool> m_playing{false};
    bool m_initialized = false;

    // Buffer pool
    struct AudioBuffer {
        WAVEHDR header;
        std::vector<uint8_t> data;
        bool inUse;
    };
    AudioBuffer m_buffers[NUM_BUFFERS];
    std::mutex m_bufferMutex;

    // Overflow queue for when all buffers are in use
    std::queue<std::vector<uint8_t>> m_pendingQueue;
    std::mutex m_queueMutex;
};

} // namespace phonecam

#endif // PHONECAM_AUDIO_PLAYER_H
