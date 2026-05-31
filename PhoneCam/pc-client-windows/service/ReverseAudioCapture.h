#pragma once
#ifndef PHONECAM_REVERSE_AUDIO_CAPTURE_H
#define PHONECAM_REVERSE_AUDIO_CAPTURE_H

#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace phonecam {

class ReverseAudioCapture {
public:
    using AudioCallback = std::function<void(const uint8_t* data, int size, int64_t timestampUs)>;

    ReverseAudioCapture();
    ~ReverseAudioCapture();

    bool start(AudioCallback callback);
    void stop();
    bool isRunning() const { return m_running.load(); }

private:
    void captureLoop(AudioCallback callback);
    void convertToMono16(const BYTE* source, UINT32 frames, const WAVEFORMATEX* format,
                         std::vector<int16_t>& out);
    void resampleTo44100(const std::vector<int16_t>& input, int sourceRate,
                         std::vector<int16_t>& output);

    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace phonecam

#endif // PHONECAM_REVERSE_AUDIO_CAPTURE_H
