#include "VirtualCamPin.h"
#include "VirtualCamFilter.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
struct SupportedResolution {
    int width;
    int height;
};

constexpr SupportedResolution kSupportedResolutions[] = {
    {640, 480},
    {1280, 720},
    {1920, 1080},
};
constexpr int kSupportedResolutionCount =
    sizeof(kSupportedResolutions) / sizeof(kSupportedResolutions[0]);

bool IsSupportedResolution(int width, int height) {
    for (const auto& resolution : kSupportedResolutions) {
        if (resolution.width == width && resolution.height == height) {
            return true;
        }
    }
    return false;
}

void ScaleBgr24Nearest(
    const uint8_t* source,
    int sourceWidth,
    int sourceHeight,
    uint8_t* target,
    int targetWidth,
    int targetHeight
) {
    if (!source || !target || sourceWidth <= 0 || sourceHeight <= 0 ||
        targetWidth <= 0 || targetHeight <= 0) {
        return;
    }

    if (sourceWidth == targetWidth && sourceHeight == targetHeight) {
        memcpy(target, source, targetWidth * targetHeight * 3);
        return;
    }

    for (int y = 0; y < targetHeight; ++y) {
        const int sourceY = y * sourceHeight / targetHeight;
        const uint8_t* sourceRow = source + sourceY * sourceWidth * 3;
        uint8_t* targetRow = target + y * targetWidth * 3;

        for (int x = 0; x < targetWidth; ++x) {
            const int sourceX = x * sourceWidth / targetWidth;
            const uint8_t* sourcePixel = sourceRow + sourceX * 3;
            uint8_t* targetPixel = targetRow + x * 3;
            targetPixel[0] = sourcePixel[0];
            targetPixel[1] = sourcePixel[1];
            targetPixel[2] = sourcePixel[2];
        }
    }
}
}

// Debug logging helper - writes to file for easy viewing
static void DbgLog(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);

    char logPath[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("ProgramData", logPath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        strcpy_s(logPath, ".");
    }
    strcat_s(logPath, "\\PhoneCam");
    CreateDirectoryA(logPath, nullptr);
    strcat_s(logPath, "\\phonecam_driver.log");

    FILE* f = fopen(logPath, "a");
    if (f) {
        fprintf(f, "%s", buf);
        fclose(f);
    }
}

VirtualCamPin::VirtualCamPin(VirtualCamFilter* pFilter, HRESULT* phr)
    : m_refCount(1)
    , m_pFilter(pFilter)
    , m_pConnectedPin(nullptr)
    , m_pAllocator(nullptr)
    , m_pInputPin(nullptr)
    , m_hThread(nullptr)
    , m_bStreaming(false)
    , m_width(640)
    , m_height(480)
    , m_fps(10)
{
    if (phr) *phr = S_OK;
}

VirtualCamPin::~VirtualCamPin() {
    StopStreaming();
    if (m_pConnectedPin) m_pConnectedPin->Release();
    if (m_pAllocator) m_pAllocator->Release();
    if (m_pInputPin) m_pInputPin->Release();
}

STDMETHODIMP VirtualCamPin::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;

    if (riid == IID_IUnknown) {
        DbgLog("PhoneCam Pin: QI IUnknown\n");
        *ppv = static_cast<IPin*>(this);
    } else if (riid == IID_IPin) {
        DbgLog("PhoneCam Pin: QI IPin\n");
        *ppv = static_cast<IPin*>(this);
    } else if (riid == IID_IQualityControl) {
        DbgLog("PhoneCam Pin: QI IQualityControl\n");
        *ppv = static_cast<IQualityControl*>(this);
    } else if (riid == IID_IAMStreamConfig) {
        DbgLog("PhoneCam Pin: QI IAMStreamConfig\n");
        *ppv = static_cast<IAMStreamConfig*>(this);
    } else if (riid == IID_IKsPropertySet) {
        DbgLog("PhoneCam Pin: QI IKsPropertySet\n");
        *ppv = static_cast<IKsPropertySet*>(this);
    } else {
        OLECHAR guidStr[64];
        StringFromGUID2(riid, guidStr, 64);
        char guidAnsi[128];
        WideCharToMultiByte(CP_ACP, 0, guidStr, -1, guidAnsi, 128, nullptr, nullptr);
        DbgLog("PhoneCam Pin: QI UNKNOWN iid=%s\n", guidAnsi);
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) VirtualCamPin::AddRef() { return InterlockedIncrement(&m_refCount); }
STDMETHODIMP_(ULONG) VirtualCamPin::Release() {
    long ref = InterlockedDecrement(&m_refCount);
    if (ref == 0) delete this;
    return ref;
}

// IPin
STDMETHODIMP VirtualCamPin::Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) {
    if (!pReceivePin) return E_POINTER;
    DbgLog("PhoneCam: Connect() called\n");

    if (pmt) {
        HRESULT accept = QueryAccept(pmt);
        if (accept != S_OK) return VFW_E_INVALIDMEDIATYPE;
        if (pmt->formattype == FORMAT_VideoInfo && pmt->cbFormat >= sizeof(VIDEOINFOHEADER)) {
            auto* pvi = reinterpret_cast<const VIDEOINFOHEADER*>(pmt->pbFormat);
            m_width = pvi->bmiHeader.biWidth;
            m_height = abs(pvi->bmiHeader.biHeight);
            DbgLog("PhoneCam: Connect using requested format %dx%d\n", m_width, m_height);
        }
    } else {
        RefreshPreferredFormatFromFrameBuffer();
    }

    AM_MEDIA_TYPE mt = GetMediaType();
    HRESULT hr = pReceivePin->ReceiveConnection(this, &mt);
    if (FAILED(hr)) {
        DbgLog("PhoneCam: ReceiveConnection failed hr=0x%08X\n", hr);
        if (mt.pbFormat) CoTaskMemFree(mt.pbFormat);
        return hr;
    }
    // Free the format block allocated by GetMediaType
    if (mt.pbFormat) CoTaskMemFree(mt.pbFormat);

    if (m_pConnectedPin) m_pConnectedPin->Release();
    m_pConnectedPin = pReceivePin;
    m_pConnectedPin->AddRef();

    // Get IMemInputPin
    hr = pReceivePin->QueryInterface(IID_IMemInputPin, (void**)&m_pInputPin);
    if (FAILED(hr)) {
        DbgLog("PhoneCam: QueryInterface IMemInputPin failed hr=0x%08X\n", hr);
        return hr;
    }

    // Setup allocator
    ALLOCATOR_PROPERTIES props = {};
    props.cBuffers = 4;
    props.cbBuffer = m_width * m_height * 3; // RGB24
    props.cbAlign = 1;
    props.cbPrefix = 0;

    hr = m_pInputPin->GetAllocator(&m_pAllocator);
    if (FAILED(hr)) {
        hr = CoCreateInstance(CLSID_MemoryAllocator, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IMemAllocator, (void**)&m_pAllocator);
        if (FAILED(hr)) {
            DbgLog("PhoneCam: Failed to create allocator hr=0x%08X\n", hr);
            return hr;
        }
    }

    ALLOCATOR_PROPERTIES actual;
    hr = m_pAllocator->SetProperties(&props, &actual);
    if (FAILED(hr)) {
        DbgLog("PhoneCam: SetProperties failed hr=0x%08X\n", hr);
        return hr;
    }

    hr = m_pInputPin->NotifyAllocator(m_pAllocator, FALSE);
    DbgLog("PhoneCam: Connect() hr=0x%08X bufs=%d bufSz=%d req=%d fmt=%dx%d\n",
           hr, actual.cBuffers, actual.cbBuffer, props.cbBuffer, m_width, m_height);
    return hr;
}

STDMETHODIMP VirtualCamPin::ReceiveConnection(IPin*, const AM_MEDIA_TYPE*) { return E_UNEXPECTED; }

STDMETHODIMP VirtualCamPin::Disconnect() {
    StopStreaming();
    if (m_pConnectedPin) { m_pConnectedPin->Release(); m_pConnectedPin = nullptr; }
    if (m_pAllocator) { m_pAllocator->Release(); m_pAllocator = nullptr; }
    if (m_pInputPin) { m_pInputPin->Release(); m_pInputPin = nullptr; }
    return S_OK;
}

STDMETHODIMP VirtualCamPin::ConnectedTo(IPin** pPin) {
    if (!pPin) return E_POINTER;
    if (!m_pConnectedPin) return VFW_E_NOT_CONNECTED;
    *pPin = m_pConnectedPin;
    (*pPin)->AddRef();
    return S_OK;
}

STDMETHODIMP VirtualCamPin::ConnectionMediaType(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (!m_pConnectedPin) return VFW_E_NOT_CONNECTED;
    *pmt = GetMediaType();
    return S_OK;
}

STDMETHODIMP VirtualCamPin::QueryPinInfo(PIN_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    pInfo->pFilter = static_cast<IBaseFilter*>(m_pFilter);
    if (m_pFilter) m_pFilter->AddRef();
    pInfo->dir = PINDIR_OUTPUT;
    wcscpy_s(pInfo->achName, L"Video");
    return S_OK;
}

STDMETHODIMP VirtualCamPin::QueryDirection(PIN_DIRECTION* pPinDir) {
    if (!pPinDir) return E_POINTER;
    *pPinDir = PINDIR_OUTPUT;
    DbgLog("PhoneCam Pin: QueryDirection -> PINDIR_OUTPUT\n");
    return S_OK;
}

STDMETHODIMP VirtualCamPin::QueryId(LPWSTR* Id) {
    if (!Id) return E_POINTER;
    *Id = static_cast<LPWSTR>(CoTaskMemAlloc(12));
    if (!*Id) return E_OUTOFMEMORY;
    wcscpy_s(*Id, 6, L"Video");
    return S_OK;
}

STDMETHODIMP VirtualCamPin::QueryAccept(const AM_MEDIA_TYPE* pmt) {
    if (pmt->majortype != MEDIATYPE_Video || pmt->subtype != MEDIASUBTYPE_RGB24)
        return S_FALSE;
    // Verify dimensions if format block is present
    if (pmt->formattype == FORMAT_VideoInfo && pmt->cbFormat >= sizeof(VIDEOINFOHEADER)) {
        auto* pvi = reinterpret_cast<const VIDEOINFOHEADER*>(pmt->pbFormat);
        int w = pvi->bmiHeader.biWidth;
        int h = abs(pvi->bmiHeader.biHeight);
        if (!IsSupportedResolution(w, h)) {
            DbgLog("PhoneCam: QueryAccept rejected %dx%d\n", w, h);
            return S_FALSE;
        }
    }
    return S_OK;
}

STDMETHODIMP VirtualCamPin::EnumMediaTypes(IEnumMediaTypes** ppEnum) {
    if (!ppEnum) return E_POINTER;
    AM_MEDIA_TYPE mt = GetMediaType();
    *ppEnum = new CEnumMediaTypes(mt);
    // Free the format block from GetMediaType since CEnumMediaTypes made its own copy
    if (mt.pbFormat) CoTaskMemFree(mt.pbFormat);
    return S_OK;
}
STDMETHODIMP VirtualCamPin::QueryInternalConnections(IPin**, ULONG*) { return E_NOTIMPL; }
STDMETHODIMP VirtualCamPin::EndOfStream() { return S_OK; }
STDMETHODIMP VirtualCamPin::BeginFlush() { return S_OK; }
STDMETHODIMP VirtualCamPin::EndFlush() { return S_OK; }
STDMETHODIMP VirtualCamPin::NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) { return S_OK; }

// IQualityControl
STDMETHODIMP VirtualCamPin::Notify(IBaseFilter*, Quality) { return S_OK; }
STDMETHODIMP VirtualCamPin::SetSink(IQualityControl*) { return S_OK; }

// IAMStreamConfig
STDMETHODIMP VirtualCamPin::SetFormat(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (m_bStreaming) return VFW_E_NOT_STOPPED;

    if (pmt->majortype != MEDIATYPE_Video || pmt->subtype != MEDIASUBTYPE_RGB24) {
        DbgLog("PhoneCam: SetFormat rejected (not RGB24 video)\n");
        return VFW_E_INVALIDMEDIATYPE;
    }
    if (pmt->formattype == FORMAT_VideoInfo && pmt->cbFormat >= sizeof(VIDEOINFOHEADER)) {
        auto* pvi = reinterpret_cast<VIDEOINFOHEADER*>(pmt->pbFormat);
        int reqW = pvi->bmiHeader.biWidth;
        int reqH = abs(pvi->bmiHeader.biHeight);
        DbgLog("PhoneCam: SetFormat requested %dx%d\n", reqW, reqH);
        if (!IsSupportedResolution(reqW, reqH)) {
            return VFW_E_INVALIDMEDIATYPE;
        }
        m_width = reqW;
        m_height = reqH;
    }
    return S_OK;
}

STDMETHODIMP VirtualCamPin::GetFormat(AM_MEDIA_TYPE** ppmt) {
    if (!ppmt) return E_POINTER;
    RefreshPreferredFormatFromFrameBuffer();
    *ppmt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (!*ppmt) return E_OUTOFMEMORY;
    **ppmt = GetMediaType();
    return S_OK;
}

STDMETHODIMP VirtualCamPin::GetNumberOfCapabilities(int* piCount, int* piSize) {
    if (!piCount || !piSize) return E_POINTER;
    *piCount = kSupportedResolutionCount;
    *piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

STDMETHODIMP VirtualCamPin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) {
    if (iIndex < 0 || iIndex >= kSupportedResolutionCount) return S_FALSE;
    if (!ppmt || !pSCC) return E_POINTER;

    const auto resolution = kSupportedResolutions[iIndex];
    *ppmt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (!*ppmt) return E_OUTOFMEMORY;
    **ppmt = GetMediaType(resolution.width, resolution.height);

    auto* caps = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(pSCC);
    memset(caps, 0, sizeof(*caps));
    caps->guid = FORMAT_VideoInfo;
    caps->InputSize.cx = resolution.width;
    caps->InputSize.cy = resolution.height;
    caps->MinOutputSize.cx = 320;
    caps->MinOutputSize.cy = 240;
    caps->MaxOutputSize.cx = 1920;
    caps->MaxOutputSize.cy = 1080;
    caps->MinCroppingSize = caps->MinOutputSize;
    caps->MaxCroppingSize = caps->MaxOutputSize;
    caps->MinFrameInterval = 166667;  // 60fps
    caps->MaxFrameInterval = 333333;  // 30fps

    return S_OK;
}

// IKsPropertySet
STDMETHODIMP VirtualCamPin::Set(REFGUID, DWORD, LPVOID, DWORD, LPVOID, DWORD) { return E_NOTIMPL; }

STDMETHODIMP VirtualCamPin::Get(REFGUID guidPropSet, DWORD dwPropID,
                                 LPVOID pInstanceData, DWORD cbInstanceData,
                                 LPVOID pPropData, DWORD cbPropData,
                                 DWORD* pcbReturned) {
    // Log the GUID being queried
    {
        OLECHAR guidStr[64];
        StringFromGUID2(guidPropSet, guidStr, 64);
        char guidAnsi[128];
        WideCharToMultiByte(CP_ACP, 0, guidStr, -1, guidAnsi, 128, nullptr, nullptr);
        DbgLog("PhoneCam Pin: IKsPropertySet::Get guid=%s propID=%lu cbPropData=%lu\n", guidAnsi, dwPropID, cbPropData);
    }

    // Report as a capture pin
    // Handle BOTH DirectShow (AMPROPSETID_Pin, propID=0) and KS (AM_KSPROPSETID_Pin, propID=2)
    bool isAmpropPin = (guidPropSet == AMPROPSETID_Pin && dwPropID == AMPROPERTY_PIN_CATEGORY);
    bool isKsPropPin = (guidPropSet == AM_KSPROPSETID_Pin && dwPropID == KSPROPERTY_PIN_CATEGORY);

    if (isAmpropPin || isKsPropPin) {
        if (cbPropData < sizeof(GUID)) return E_UNEXPECTED;
        *static_cast<GUID*>(pPropData) = PIN_CATEGORY_CAPTURE;
        if (pcbReturned) *pcbReturned = sizeof(GUID);
        DbgLog("PhoneCam Pin: Returning PIN_CATEGORY_CAPTURE\n");
        return S_OK;
    }
    DbgLog("PhoneCam Pin: IKsPropertySet::Get -> E_NOTIMPL\n");
    return E_NOTIMPL;
}

STDMETHODIMP VirtualCamPin::QuerySupported(REFGUID guidPropSet, DWORD dwPropID,
                                            DWORD* pTypeSupport) {
    bool isAmpropPin = (guidPropSet == AMPROPSETID_Pin && dwPropID == AMPROPERTY_PIN_CATEGORY);
    bool isKsPropPin = (guidPropSet == AM_KSPROPSETID_Pin && dwPropID == KSPROPERTY_PIN_CATEGORY);

    if (isAmpropPin || isKsPropPin) {
        if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
        return S_OK;
    }
    return E_NOTIMPL;
}

// Streaming
void VirtualCamPin::StartStreaming() {
    if (m_bStreaming) return;
    DbgLog("PhoneCam: StartStreaming()\n");
    m_bStreaming = true;

    // Open shared memory reader
    if (!m_frameBuffer.openReader()) {
        DbgLog("PhoneCam: Could not open frame buffer - will deliver black frames\n");
    } else {
        int fbW = m_frameBuffer.getWidth();
        int fbH = m_frameBuffer.getHeight();
        int fbFps = m_frameBuffer.getFps();
        if (fbFps > 0) m_fps = fbFps;
        DbgLog("PhoneCam: Frame buffer opened %dx%d @ %dfps (pin format %dx%d)\n",
               fbW, fbH, m_fps, m_width, m_height);
    }

    // Commit allocator
    if (m_pAllocator) {
        HRESULT hr = m_pAllocator->Commit();
        DbgLog("PhoneCam: Allocator Commit hr=0x%08X\n", hr);
    }

    m_hThread = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        static_cast<VirtualCamPin*>(p)->StreamThread();
        return 0;
    }, this, 0, nullptr);
}

void VirtualCamPin::StopStreaming() {
    DbgLog("PhoneCam: StopStreaming()\n");
    m_bStreaming = false;
    if (m_hThread) {
        WaitForSingleObject(m_hThread, 3000);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }
    if (m_pAllocator) m_pAllocator->Decommit();
    m_frameBuffer.close();
}

void VirtualCamPin::StreamThread() {
    DbgLog("PhoneCam: StreamThread started\n");
    const int outputWidth = m_width;
    const int outputHeight = m_height;
    const int outputFrameSize = outputWidth * outputHeight * 3;
    auto* outputBuffer = new uint8_t[outputFrameSize];
    memset(outputBuffer, 0, outputFrameSize); // start with black

    int sourceWidth = m_frameBuffer.getWidth();
    int sourceHeight = m_frameBuffer.getHeight();
    int sourceFrameSize = (sourceWidth > 0 && sourceHeight > 0)
        ? sourceWidth * sourceHeight * 3
        : outputFrameSize;
    auto* sourceBuffer = new uint8_t[sourceFrameSize];
    memset(sourceBuffer, 0, sourceFrameSize);

    int frameWidth, frameHeight;
    bool frameBufferOpen = m_frameBuffer.getWidth() > 0;

    REFERENCE_TIME streamTime = 0;
    DWORD frameCount = 0;

    while (m_bStreaming) {
        int currentFps = m_frameBuffer.getFps();
        if (currentFps <= 0 || currentFps > 60) currentFps = m_fps > 0 ? m_fps : 10;
        m_fps = currentFps;
        REFERENCE_TIME frameDuration100ns = 10000000LL / currentFps;
        DWORD frameDurationMs = std::max<DWORD>(1, 1000 / currentFps);

        // If frame buffer isn't open yet, try to open it periodically
        if (!frameBufferOpen) {
            if (m_frameBuffer.openReader()) {
                sourceWidth = m_frameBuffer.getWidth();
                sourceHeight = m_frameBuffer.getHeight();
                sourceFrameSize = sourceWidth * sourceHeight * 3;
                delete[] sourceBuffer;
                sourceBuffer = new uint8_t[sourceFrameSize];
                memset(sourceBuffer, 0, sourceFrameSize);
                frameBufferOpen = true;
                DbgLog("PhoneCam: Frame buffer opened in thread %dx%d -> output %dx%d\n",
                       sourceWidth, sourceHeight, outputWidth, outputHeight);
            }
        }

        // Wait for a new frame from shared memory
        if (frameBufferOpen) {
            // Wait for event (optimization to avoid busy-wait), but always try to read
            m_frameBuffer.waitForFrame(frameDurationMs);
            // Always attempt to read - the event is auto-reset and may have been
            // consumed by a previous reader. readFrame checks the frameReady flag.
            if (m_frameBuffer.readFrame(sourceBuffer, sourceFrameSize, frameWidth, frameHeight)) {
                // Check if frame buffer dimensions changed (service reconfigured)
                if (frameWidth > 0 && frameHeight > 0 &&
                    (frameWidth != sourceWidth || frameHeight != sourceHeight)) {
                    DbgLog("PhoneCam: Source resolution changed from %dx%d to %dx%d, re-opening\n",
                           sourceWidth, sourceHeight, frameWidth, frameHeight);
                    m_frameBuffer.close();
                    if (m_frameBuffer.openReader()) {
                        sourceWidth = m_frameBuffer.getWidth();
                        sourceHeight = m_frameBuffer.getHeight();
                        sourceFrameSize = sourceWidth * sourceHeight * 3;
                        delete[] sourceBuffer;
                        sourceBuffer = new uint8_t[sourceFrameSize];
                        memset(sourceBuffer, 0, sourceFrameSize);
                        DbgLog("PhoneCam: Re-opened source at %dx%d, frameSize=%d\n",
                               sourceWidth, sourceHeight, sourceFrameSize);
                    } else {
                        frameBufferOpen = false;
                    }
                }

                ScaleBgr24Nearest(
                    sourceBuffer,
                    sourceWidth,
                    sourceHeight,
                    outputBuffer,
                    outputWidth,
                    outputHeight
                );

                if (frameCount < 10) {
                    // Log first frames with more comprehensive diagnostics
                    int nzStart = 0, nzMid = 0, nzEnd = 0;
                    for (int i = 0; i < 1000 && i < outputFrameSize; i++)
                        if (outputBuffer[i] != 0) nzStart++;
                    int midOff = outputFrameSize / 2;
                    for (int i = midOff; i < midOff + 1000 && i < outputFrameSize; i++)
                        if (outputBuffer[i] != 0) nzMid++;
                    for (int i = outputFrameSize - 1000; i < outputFrameSize; i++)
                        if (outputBuffer[i] != 0) nzEnd++;
                    DbgLog("PhoneCam: Read frame %lu, source=%dx%d output=%dx%d outSize=%d, nz(start=%d,mid=%d,end=%d)\n",
                           frameCount, frameWidth, frameHeight, outputWidth, outputHeight, outputFrameSize,
                           nzStart, nzMid, nzEnd);
                }
            }
        } else {
            Sleep(frameDurationMs);
        }

        // Get a buffer from the allocator and deliver
        if (m_pAllocator && m_pInputPin) {
            IMediaSample* pSample = nullptr;
            HRESULT hr = m_pAllocator->GetBuffer(&pSample, nullptr, nullptr, 0);
            if (SUCCEEDED(hr) && pSample) {
                BYTE* pData = nullptr;
                pSample->GetPointer(&pData);
                int sampleBufSize = pSample->GetSize();
                int copySize = min(outputFrameSize, sampleBufSize);
                memcpy(pData, outputBuffer, copySize);
                pSample->SetActualDataLength(copySize);

                if (frameCount < 3) {
                    DbgLog("PhoneCam: Sample %lu: sampleBufSize=%d, outputFrameSize=%d, copySize=%d\n",
                           frameCount, sampleBufSize, outputFrameSize, copySize);
                }

                REFERENCE_TIME start = streamTime;
                REFERENCE_TIME end = streamTime + frameDuration100ns;
                pSample->SetTime(&start, &end);
                pSample->SetSyncPoint(TRUE);
                streamTime += frameDuration100ns;

                hr = m_pInputPin->Receive(pSample);
                pSample->Release();
                frameCount++;

                if (frameCount % 300 == 0) {
                    DbgLog("PhoneCam: Delivered %lu frames\n", frameCount);
                }
            } else {
                DbgLog("PhoneCam: GetBuffer failed hr=0x%08X\n", hr);
                Sleep(frameDurationMs);
            }
        } else {
            Sleep(frameDurationMs);
        }
    }

    delete[] sourceBuffer;
    delete[] outputBuffer;
    DbgLog("PhoneCam: StreamThread stopped after %lu frames\n", frameCount);
}

void VirtualCamPin::RefreshPreferredFormatFromFrameBuffer() {
    if (m_bStreaming || m_pConnectedPin) return;

    phonecam::FrameBuffer probe;
    if (!probe.openReader()) return;

    int fbW = probe.getWidth();
    int fbH = probe.getHeight();
    int fbFps = probe.getFps();
    if (IsSupportedResolution(fbW, fbH) && (fbW != m_width || fbH != m_height)) {
        DbgLog("PhoneCam: Preferred format from frame buffer %dx%d @ %dfps\n", fbW, fbH, fbFps);
        m_width = fbW;
        m_height = fbH;
    }
    if (fbFps > 0) m_fps = fbFps;
}

AM_MEDIA_TYPE VirtualCamPin::GetMediaType(int width, int height) {
    if (width <= 0) width = m_width;
    if (height <= 0) height = m_height;

    AM_MEDIA_TYPE mt = {};
    mt.majortype = MEDIATYPE_Video;
    mt.subtype = MEDIASUBTYPE_RGB24;
    mt.bFixedSizeSamples = TRUE;
    mt.bTemporalCompression = FALSE;
    mt.lSampleSize = width * height * 3;
    mt.formattype = FORMAT_VideoInfo;

    auto* pvi = static_cast<VIDEOINFOHEADER*>(CoTaskMemAlloc(sizeof(VIDEOINFOHEADER)));
    memset(pvi, 0, sizeof(VIDEOINFOHEADER));
    pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    pvi->bmiHeader.biWidth = width;
    pvi->bmiHeader.biHeight = height; // positive = bottom-up (standard DirectShow RGB24)
    pvi->bmiHeader.biPlanes = 1;
    pvi->bmiHeader.biBitCount = 24;
    pvi->bmiHeader.biCompression = BI_RGB;
    pvi->bmiHeader.biSizeImage = width * height * 3;
    pvi->AvgTimePerFrame = 10000000LL / m_fps; // 100ns units

    mt.pbFormat = reinterpret_cast<BYTE*>(pvi);
    mt.cbFormat = sizeof(VIDEOINFOHEADER);

    return mt;
}
