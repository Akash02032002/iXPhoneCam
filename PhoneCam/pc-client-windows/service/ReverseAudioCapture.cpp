#include "ReverseAudioCapture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mmreg.h>
#include <ksmedia.h>

namespace phonecam {

namespace {
    constexpr int TARGET_SAMPLE_RATE = 44100;
    constexpr int TARGET_BYTES_PER_SAMPLE = 2;
    constexpr int TARGET_CHANNELS = 1;
    constexpr int TARGET_CHUNK_MS = 20;
    constexpr int TARGET_CHUNK_BYTES =
        TARGET_SAMPLE_RATE * TARGET_BYTES_PER_SAMPLE * TARGET_CHANNELS * TARGET_CHUNK_MS / 1000;

    template <typename T>
    void safeRelease(T*& ptr) {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }

    int64_t nowMicros() {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    }
}

ReverseAudioCapture::ReverseAudioCapture() = default;

ReverseAudioCapture::~ReverseAudioCapture() {
    stop();
}

bool ReverseAudioCapture::start(AudioCallback callback) {
    if (m_running.load()) return true;
    if (!callback) return false;

    m_running = true;
    m_thread = std::thread([this, callback]() {
        captureLoop(callback);
    });
    return true;
}

void ReverseAudioCapture::stop() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void ReverseAudioCapture::captureLoop(AudioCallback callback) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool coInitialized = SUCCEEDED(hr);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;
    WAVEFORMATEX* mixFormat = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        printf("ReverseAudioCapture: failed to create device enumerator (hr=0x%08X)\n", hr);
        goto cleanup;
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        printf("ReverseAudioCapture: failed to get default render endpoint (hr=0x%08X)\n", hr);
        goto cleanup;
    }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&audioClient));
    if (FAILED(hr)) {
        printf("ReverseAudioCapture: failed to activate audio client (hr=0x%08X)\n", hr);
        goto cleanup;
    }

    hr = audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr)) {
        printf("ReverseAudioCapture: failed to read mix format (hr=0x%08X)\n", hr);
        goto cleanup;
    }

    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK,
                                 10000000, 0, mixFormat, nullptr);
    if (FAILED(hr)) {
        printf("ReverseAudioCapture: failed to initialize loopback (hr=0x%08X)\n", hr);
        goto cleanup;
    }

    hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
                                 reinterpret_cast<void**>(&captureClient));
    if (FAILED(hr)) {
        printf("ReverseAudioCapture: failed to get capture client (hr=0x%08X)\n", hr);
        goto cleanup;
    }

    hr = audioClient->Start();
    if (FAILED(hr)) {
        printf("ReverseAudioCapture: failed to start loopback (hr=0x%08X)\n", hr);
        goto cleanup;
    }

    printf("ReverseAudioCapture: started default PC speaker loopback (%u Hz, %u channels, %u bits)\n",
           mixFormat->nSamplesPerSec, mixFormat->nChannels, mixFormat->wBitsPerSample);

    {
        std::vector<int16_t> mono;
        std::vector<int16_t> resampled;
        std::vector<uint8_t> pending;

        while (m_running.load()) {
            UINT32 packetFrames = 0;
            hr = captureClient->GetNextPacketSize(&packetFrames);
            if (FAILED(hr)) break;

            if (packetFrames == 0) {
                Sleep(5);
                continue;
            }

            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;

            mono.clear();
            resampled.clear();
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
                mono.assign(frames, 0);
            } else {
                convertToMono16(data, frames, mixFormat, mono);
            }

            resampleTo44100(mono, static_cast<int>(mixFormat->nSamplesPerSec), resampled);
            const auto* bytes = reinterpret_cast<const uint8_t*>(resampled.data());
            pending.insert(pending.end(), bytes, bytes + resampled.size() * sizeof(int16_t));

            while (pending.size() >= TARGET_CHUNK_BYTES) {
                callback(pending.data(), TARGET_CHUNK_BYTES, nowMicros());
                pending.erase(pending.begin(), pending.begin() + TARGET_CHUNK_BYTES);
            }

            captureClient->ReleaseBuffer(frames);
        }
    }

    if (audioClient) {
        audioClient->Stop();
    }

cleanup:
    if (mixFormat) CoTaskMemFree(mixFormat);
    safeRelease(captureClient);
    safeRelease(audioClient);
    safeRelease(device);
    safeRelease(enumerator);
    if (coInitialized) CoUninitialize();
    m_running = false;
    printf("ReverseAudioCapture: stopped\n");
}

void ReverseAudioCapture::convertToMono16(const BYTE* source, UINT32 frames,
                                          const WAVEFORMATEX* format,
                                          std::vector<int16_t>& out) {
    if (!source || !format) return;

    const int channels = std::max<int>(1, format->nChannels);
    out.resize(frames);

    bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        isFloat = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }

    if (isFloat && format->wBitsPerSample == 32) {
        const float* samples = reinterpret_cast<const float*>(source);
        for (UINT32 frame = 0; frame < frames; ++frame) {
            float sum = 0.0f;
            for (int ch = 0; ch < channels; ++ch) {
                sum += samples[frame * channels + ch];
            }
            float mono = std::clamp(sum / channels, -1.0f, 1.0f);
            out[frame] = static_cast<int16_t>(mono * 32767.0f);
        }
        return;
    }

    if (format->wBitsPerSample == 16) {
        const int16_t* samples = reinterpret_cast<const int16_t*>(source);
        for (UINT32 frame = 0; frame < frames; ++frame) {
            int sum = 0;
            for (int ch = 0; ch < channels; ++ch) {
                sum += samples[frame * channels + ch];
            }
            out[frame] = static_cast<int16_t>(sum / channels);
        }
        return;
    }

    if (format->wBitsPerSample == 24) {
        const uint8_t* samples = reinterpret_cast<const uint8_t*>(source);
        const int bytesPerFrame = channels * 3;
        for (UINT32 frame = 0; frame < frames; ++frame) {
            int sum = 0;
            const uint8_t* framePtr = samples + frame * bytesPerFrame;
            for (int ch = 0; ch < channels; ++ch) {
                const uint8_t* s = framePtr + ch * 3;
                int32_t value = (static_cast<int32_t>(s[0]) |
                                 (static_cast<int32_t>(s[1]) << 8) |
                                 (static_cast<int32_t>(s[2]) << 16));
                if (value & 0x00800000) value |= 0xFF000000;
                sum += static_cast<int>(value >> 8);
            }
            out[frame] = static_cast<int16_t>(sum / channels);
        }
        return;
    }

    if (format->wBitsPerSample == 32) {
        const int32_t* samples = reinterpret_cast<const int32_t*>(source);
        for (UINT32 frame = 0; frame < frames; ++frame) {
            int64_t sum = 0;
            for (int ch = 0; ch < channels; ++ch) {
                sum += samples[frame * channels + ch] >> 16;
            }
            out[frame] = static_cast<int16_t>(sum / channels);
        }
        return;
    }

    std::fill(out.begin(), out.end(), 0);
}

void ReverseAudioCapture::resampleTo44100(const std::vector<int16_t>& input,
                                          int sourceRate,
                                          std::vector<int16_t>& output) {
    if (input.empty()) return;
    if (sourceRate <= 0 || sourceRate == TARGET_SAMPLE_RATE) {
        output = input;
        return;
    }

    size_t outCount = static_cast<size_t>(
        std::max<double>(1.0, std::floor(input.size() * (double)TARGET_SAMPLE_RATE / sourceRate)));
    output.resize(outCount);

    double step = static_cast<double>(sourceRate) / TARGET_SAMPLE_RATE;
    for (size_t i = 0; i < outCount; ++i) {
        size_t src = static_cast<size_t>(i * step);
        if (src >= input.size()) src = input.size() - 1;
        output[i] = input[src];
    }
}

} // namespace phonecam
