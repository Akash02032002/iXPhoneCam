#include "AudioPlayer.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>

namespace phonecam {

AudioPlayer::AudioPlayer() {
    for (int i = 0; i < NUM_BUFFERS; i++) {
        m_buffers[i].inUse = false;
        memset(&m_buffers[i].header, 0, sizeof(WAVEHDR));
    }
}

AudioPlayer::~AudioPlayer() {
    stop();
}

bool AudioPlayer::initialize(const std::string& preferredDeviceName) {
    if (m_initialized) return true;

    // Setup wave format: 44100Hz, 16-bit, Mono (matches Android AudioCapture)
    m_waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    m_waveFormat.nChannels = CHANNELS;
    m_waveFormat.nSamplesPerSec = SAMPLE_RATE;
    m_waveFormat.wBitsPerSample = BITS_PER_SAMPLE;
    m_waveFormat.nBlockAlign = (CHANNELS * BITS_PER_SAMPLE) / 8;
    m_waveFormat.nAvgBytesPerSec = SAMPLE_RATE * m_waveFormat.nBlockAlign;
    m_waveFormat.cbSize = 0;

    UINT deviceId = WAVE_MAPPER;
    if (!preferredDeviceName.empty()) {
        UINT count = waveOutGetNumDevs();
        for (UINT i = 0; i < count; ++i) {
            WAVEOUTCAPSA caps = {};
            if (waveOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
                std::string name = caps.szPname;
                std::string haystack = name;
                std::string needle = preferredDeviceName;
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
                if (haystack.find(needle) != std::string::npos) {
                    deviceId = i;
                    printf("AudioPlayer: Routing phone mic audio to output device '%s'\n", caps.szPname);
                    break;
                }
            }
        }
        if (deviceId == WAVE_MAPPER) {
            printf("AudioPlayer: Preferred output '%s' not found, using default output\n",
                   preferredDeviceName.c_str());
        }
    }

    MMRESULT result = waveOutOpen(
        &m_hWaveOut,
        deviceId,
        &m_waveFormat,
        0,
        0,
        CALLBACK_NULL
    );

    if (result != MMSYSERR_NOERROR) {
        char errMsg[256];
        waveOutGetErrorTextA(result, errMsg, sizeof(errMsg));
        printf("AudioPlayer: Failed to open waveOut device: %s (error=%d)\n", errMsg, result);
        return false;
    }

    // Pre-allocate buffers
    for (int i = 0; i < NUM_BUFFERS; i++) {
        m_buffers[i].data.resize(BUFFER_SIZE);
        m_buffers[i].inUse = false;
        memset(&m_buffers[i].header, 0, sizeof(WAVEHDR));
    }

    m_initialized = true;
    m_playing = true;
    printf("AudioPlayer: Initialized (%d Hz, %d-bit, %s)\n",
           SAMPLE_RATE, BITS_PER_SAMPLE, CHANNELS == 1 ? "Mono" : "Stereo");
    return true;
}

void AudioPlayer::play(const uint8_t* pcmData, int size) {
    if (!m_initialized || !m_playing || size <= 0) return;
    submitBuffer(pcmData, size);
}

bool AudioPlayer::submitBuffer(const uint8_t* data, int size) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (!m_hWaveOut) return false;

    reclaimDoneBuffersLocked();

    // Find a free buffer
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (!m_buffers[i].inUse) {
            AudioBuffer& buf = m_buffers[i];

            // Resize if needed
            if (buf.data.size() < static_cast<size_t>(size)) {
                buf.data.resize(size);
            }

            memcpy(buf.data.data(), data, size);

            memset(&buf.header, 0, sizeof(WAVEHDR));
            buf.header.lpData = reinterpret_cast<LPSTR>(buf.data.data());
            buf.header.dwBufferLength = static_cast<DWORD>(size);
            buf.header.dwUser = static_cast<DWORD_PTR>(i);

            MMRESULT result = waveOutPrepareHeader(m_hWaveOut, &buf.header, sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) {
                printf("AudioPlayer: prepareHeader failed: %d\n", result);
                return false;
            }

            buf.inUse = true;
            result = waveOutWrite(m_hWaveOut, &buf.header, sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) {
                buf.inUse = false;
                waveOutUnprepareHeader(m_hWaveOut, &buf.header, sizeof(WAVEHDR));
                printf("AudioPlayer: waveOutWrite failed: %d\n", result);
                return false;
            }

            return true;
        }
    }

    return false;
}

void CALLBACK AudioPlayer::waveOutCallback(HWAVEOUT hwo, UINT uMsg,
                                             DWORD_PTR dwInstance,
                                             DWORD_PTR dwParam1,
                                             DWORD_PTR dwParam2) {
    if (uMsg == WOM_DONE) {
        auto* player = reinterpret_cast<AudioPlayer*>(dwInstance);
        auto* header = reinterpret_cast<WAVEHDR*>(dwParam1);
        player->onBufferDone(header);
    }
}

void AudioPlayer::onBufferDone(WAVEHDR* header) {
    if (!m_hWaveOut) return;

    int bufIdx = static_cast<int>(header->dwUser);

    waveOutUnprepareHeader(m_hWaveOut, header, sizeof(WAVEHDR));

    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        if (bufIdx >= 0 && bufIdx < NUM_BUFFERS) {
            m_buffers[bufIdx].inUse = false;
        }
    }

}

void AudioPlayer::reclaimDoneBuffersLocked() {
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        auto& buf = m_buffers[i];
        if (!buf.inUse) continue;

        if ((buf.header.dwFlags & WHDR_DONE) == 0) continue;

        if (waveOutUnprepareHeader(m_hWaveOut, &buf.header, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
            buf.inUse = false;
        }
    }
}

void AudioPlayer::setVolume(float volume) {
    if (!m_hWaveOut) return;
    volume = std::clamp(volume, 0.0f, 1.0f);
    DWORD vol = static_cast<DWORD>(volume * 0xFFFF);
    DWORD stereoVol = (vol << 16) | vol;
    waveOutSetVolume(m_hWaveOut, stereoVol);
}

void AudioPlayer::stop() {
    m_playing = false;

    if (m_hWaveOut) {
        waveOutReset(m_hWaveOut);

        // Unprepare all buffers
        for (int i = 0; i < NUM_BUFFERS; i++) {
            if (m_buffers[i].inUse) {
                waveOutUnprepareHeader(m_hWaveOut, &m_buffers[i].header, sizeof(WAVEHDR));
                m_buffers[i].inUse = false;
            }
        }

        waveOutClose(m_hWaveOut);
        m_hWaveOut = nullptr;
    }

    m_initialized = false;
    printf("AudioPlayer: Stopped\n");
}

} // namespace phonecam
