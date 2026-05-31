#pragma once
#ifndef PHONECAM_FRAME_BUFFER_H
#define PHONECAM_FRAME_BUFFER_H

#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <string>
#include <Windows.h>

namespace phonecam {

/**
 * Shared memory ring buffer for passing decoded video frames
 * from the PhoneCam Service to the Virtual Camera DirectShow filter.
 *
 * Uses a Windows named shared memory section so that the service process
 * and the DirectShow filter (loaded in-process by apps like Zoom/Teams)
 * can exchange frames without IPC overhead.
 *
 * Layout of shared memory:
 *   [0-3]     writeIndex (uint32) - current write position
 *   [4-7]     width (uint32)
 *   [8-11]    height (uint32)
 *   [12-15]   frameSize (uint32) - bytes per frame
 *   [16-19]   frameReady (uint32) - flag: 1 = new frame available
 *   [20+]     frame data (RGB24, width * height * 3 bytes)
 */
class FrameBuffer {
public:
    static constexpr const char* SHARED_MEM_NAME = "PhoneCamFrameBuffer";
    static constexpr const char* MUTEX_NAME = "PhoneCamFrameMutex";
    static constexpr const char* EVENT_NAME = "PhoneCamFrameEvent";
    static constexpr int         HEADER_OFFSET = 32; // bytes reserved for header
    // Max shared memory size — supports up to 1920x1080 RGB24
    static constexpr int         MAX_MAP_SIZE = HEADER_OFFSET + 1920 * 1080 * 3;

    FrameBuffer();
    ~FrameBuffer();

    // Writer side (used by PhoneCam Service)
    bool createWriter(int width, int height);
    void writeFrame(const uint8_t* rgbData, int size);

    // Reader side (used by DirectShow filter)
    bool openReader();
    bool readFrame(uint8_t* outputBuffer, int bufferSize, int& width, int& height);
    bool waitForFrame(int timeoutMs = 100);

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    int getFrameSize() const { return m_frameSize; }

    void close();

private:
    HANDLE m_hMapFile = nullptr;
    HANDLE m_hMutex = nullptr;
    HANDLE m_hEvent = nullptr;
    uint8_t* m_pBuffer = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_frameSize = 0;
    int m_totalSize = 0;
    bool m_isWriter = false;
};

} // namespace phonecam

#endif // PHONECAM_FRAME_BUFFER_H
