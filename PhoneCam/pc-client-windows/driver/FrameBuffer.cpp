#include "FrameBuffer.h"
#include <cstring>
#include <cstdio>

namespace phonecam {

FrameBuffer::FrameBuffer() {}

FrameBuffer::~FrameBuffer() {
    close();
}

bool FrameBuffer::createWriter(int width, int height, int fps) {
    m_isWriter = true;
    m_width = width;
    m_height = height;
    m_fps = fps > 0 ? fps : 10;
    m_frameSize = width * height * 3; // RGB24
    m_totalSize = MAX_MAP_SIZE;       // Always allocate max to avoid resize issues

    // Security attributes that allow any process to access the shared resources
    // (needed because the DirectShow filter runs inside Zoom/Teams process)
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = FALSE;

    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE); // NULL DACL = full access
    sa.lpSecurityDescriptor = &sd;

    // Create named mutex
    m_hMutex = CreateMutexA(&sa, FALSE, MUTEX_NAME);
    if (!m_hMutex) {
        printf("FrameBuffer: Failed to create mutex, error=%lu\n", GetLastError());
        return false;
    }

    // Create event for signaling new frames
    m_hEvent = CreateEventA(&sa, FALSE, FALSE, EVENT_NAME);
    if (!m_hEvent) {
        printf("FrameBuffer: Failed to create event, error=%lu\n", GetLastError());
        return false;
    }

    // Create shared memory
    m_hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE,
        0, m_totalSize, SHARED_MEM_NAME
    );

    if (!m_hMapFile) {
        printf("FrameBuffer: Failed to create file mapping, error=%lu\n", GetLastError());
        return false;
    }

    m_pBuffer = static_cast<uint8_t*>(
        MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, m_totalSize)
    );

    if (!m_pBuffer) {
        printf("FrameBuffer: Failed to map view, error=%lu\n", GetLastError());
        return false;
    }

    // Write header
    memset(m_pBuffer, 0, HEADER_OFFSET);
    auto* header = reinterpret_cast<uint32_t*>(m_pBuffer);
    header[1] = static_cast<uint32_t>(m_width);
    header[2] = static_cast<uint32_t>(m_height);
    header[3] = static_cast<uint32_t>(m_frameSize);
    header[4] = 0; // frameReady = false
    header[5] = static_cast<uint32_t>(m_fps);

    printf("FrameBuffer: Writer created %dx%d @ %dfps, shared mem size=%d\n",
           m_width, m_height, m_fps, m_totalSize);
    return true;
}

void FrameBuffer::writeFrame(const uint8_t* rgbData, int size) {
    if (!m_pBuffer || !m_hMutex) return;

    if (WaitForSingleObject(m_hMutex, 50) != WAIT_OBJECT_0) {
        return;
    }

    int copySize = (size < m_frameSize) ? size : m_frameSize;
    memcpy(m_pBuffer + HEADER_OFFSET, rgbData, copySize);

    // Update header
    auto* header = reinterpret_cast<uint32_t*>(m_pBuffer);
    header[0]++; // increment writeIndex
    header[4] = 1; // frameReady = true

    ReleaseMutex(m_hMutex);

    // Signal the reader
    if (m_hEvent) {
        SetEvent(m_hEvent);
    }
}

void FrameBuffer::setFps(int fps) {
    if (fps <= 0) return;
    m_fps = fps;

    if (!m_pBuffer || !m_hMutex) return;

    if (WaitForSingleObject(m_hMutex, 50) != WAIT_OBJECT_0) {
        return;
    }

    auto* header = reinterpret_cast<uint32_t*>(m_pBuffer);
    header[5] = static_cast<uint32_t>(m_fps);

    ReleaseMutex(m_hMutex);
}

bool FrameBuffer::openReader() {
    m_isWriter = false;

    // Open mutex (need full access for WaitForSingleObject + ReleaseMutex)
    m_hMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, MUTEX_NAME);
    if (!m_hMutex) return false;

    // Open event
    m_hEvent = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, EVENT_NAME);

    // Open shared memory - need ALL_ACCESS because readFrame writes back frameReady=0
    m_hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEM_NAME);
    if (!m_hMapFile) {
        CloseHandle(m_hMutex);
        m_hMutex = nullptr;
        return false;
    }

    // Map a small header first to get dimensions
    auto* headerView = static_cast<uint8_t*>(
        MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, HEADER_OFFSET)
    );
    if (!headerView) return false;

    auto* header = reinterpret_cast<const uint32_t*>(headerView);
    m_width = static_cast<int>(header[1]);
    m_height = static_cast<int>(header[2]);
    m_frameSize = static_cast<int>(header[3]);
    m_fps = header[5] > 0 ? static_cast<int>(header[5]) : 10;
    m_totalSize = MAX_MAP_SIZE;
    UnmapViewOfFile(headerView);

    // Re-map full size with write access
    m_pBuffer = static_cast<uint8_t*>(
        MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, m_totalSize)
    );

    if (!m_pBuffer) return false;

    printf("FrameBuffer: Reader opened %dx%d @ %dfps\n", m_width, m_height, m_fps);
    return true;
}

bool FrameBuffer::readFrame(uint8_t* outputBuffer, int bufferSize, int& width, int& height) {
    if (!m_pBuffer || !m_hMutex) return false;

    if (WaitForSingleObject(m_hMutex, 50) != WAIT_OBJECT_0) {
        return false;
    }

    auto* header = reinterpret_cast<uint32_t*>(m_pBuffer);
    if (header[4] == 0) {
        // No new frame
        ReleaseMutex(m_hMutex);
        return false;
    }

    width = static_cast<int>(header[1]);
    height = static_cast<int>(header[2]);
    int frameSize = static_cast<int>(header[3]);
    m_fps = header[5] > 0 ? static_cast<int>(header[5]) : m_fps;

    int copySize = (bufferSize < frameSize) ? bufferSize : frameSize;
    memcpy(outputBuffer, m_pBuffer + HEADER_OFFSET, copySize);

    header[4] = 0; // frameReady = false (consumed)

    ReleaseMutex(m_hMutex);
    return true;
}

bool FrameBuffer::waitForFrame(int timeoutMs) {
    if (!m_hEvent) return false;
    return WaitForSingleObject(m_hEvent, timeoutMs) == WAIT_OBJECT_0;
}

void FrameBuffer::close() {
    if (m_pBuffer) {
        UnmapViewOfFile(m_pBuffer);
        m_pBuffer = nullptr;
    }
    if (m_hMapFile) {
        CloseHandle(m_hMapFile);
        m_hMapFile = nullptr;
    }
    if (m_hMutex) {
        CloseHandle(m_hMutex);
        m_hMutex = nullptr;
    }
    if (m_hEvent) {
        CloseHandle(m_hEvent);
        m_hEvent = nullptr;
    }
}

} // namespace phonecam
