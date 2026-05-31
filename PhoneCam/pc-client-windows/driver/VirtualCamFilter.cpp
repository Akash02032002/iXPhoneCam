#include "VirtualCamFilter.h"
#include "VirtualCamPin.h"
#include <cstdio>
#include <cstring>

// ==================== VirtualCamFilter ====================

VirtualCamFilter::VirtualCamFilter(IUnknown* pUnk, HRESULT* phr)
    : m_refCount(1)
    , m_state(State_Stopped)
    , m_pGraph(nullptr)
    , m_pClock(nullptr)
    , m_pName(nullptr)
    , m_pPin(nullptr)
    , m_startTime(0)
{
    m_pPin = new VirtualCamPin(this, phr);
    if (phr) *phr = S_OK;
}

VirtualCamFilter::~VirtualCamFilter() {
    delete m_pPin;
    if (m_pName) CoTaskMemFree(m_pName);
    if (m_pClock) m_pClock->Release();
}

// IUnknown
STDMETHODIMP VirtualCamFilter::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;

    if (riid == IID_IUnknown)
        *ppv = static_cast<IBaseFilter*>(this);
    else if (riid == IID_IPersist)
        *ppv = static_cast<IPersist*>(this);
    else if (riid == IID_IMediaFilter)
        *ppv = static_cast<IMediaFilter*>(this);
    else if (riid == IID_IBaseFilter)
        *ppv = static_cast<IBaseFilter*>(this);
    else if (riid == IID_IReferenceClock)
        *ppv = static_cast<IReferenceClock*>(this);
    else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) VirtualCamFilter::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) VirtualCamFilter::Release() {
    long ref = InterlockedDecrement(&m_refCount);
    if (ref == 0) delete this;
    return ref;
}

// IPersist
STDMETHODIMP VirtualCamFilter::GetClassID(CLSID* pClsID) {
    if (!pClsID) return E_POINTER;
    *pClsID = CLSID_PhoneCamFilter;
    return S_OK;
}

// IMediaFilter
STDMETHODIMP VirtualCamFilter::Stop() {
    if (m_pPin) m_pPin->StopStreaming();
    m_state = State_Stopped;
    return S_OK;
}

STDMETHODIMP VirtualCamFilter::Pause() {
    // DirectShow graph transitions Stop→Pause→Run
    // Some apps (like Zoom) may query frames during Pause
    // Start streaming on Pause so frames are ready when Run is called
    if (m_state == State_Stopped && m_pPin) {
        m_pPin->StartStreaming();
    }
    m_state = State_Paused;
    return S_OK;
}

STDMETHODIMP VirtualCamFilter::Run(REFERENCE_TIME tStart) {
    m_startTime = tStart;
    if (m_pPin) m_pPin->StartStreaming();
    m_state = State_Running;
    return S_OK;
}

STDMETHODIMP VirtualCamFilter::GetState(DWORD dwMilliSecsTimeout, FILTER_STATE* State) {
    if (!State) return E_POINTER;
    *State = m_state;
    return S_OK;
}

STDMETHODIMP VirtualCamFilter::SetSyncSource(IReferenceClock* pClock) {
    if (m_pClock) m_pClock->Release();
    m_pClock = pClock;
    if (m_pClock) m_pClock->AddRef();
    return S_OK;
}

STDMETHODIMP VirtualCamFilter::GetSyncSource(IReferenceClock** pClock) {
    if (!pClock) return E_POINTER;
    *pClock = m_pClock;
    if (m_pClock) m_pClock->AddRef();
    return S_OK;
}

// IBaseFilter
STDMETHODIMP VirtualCamFilter::EnumPins(IEnumPins** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new CEnumPins(static_cast<IPin*>(m_pPin));
    OutputDebugStringA("PhoneCam Filter: EnumPins() called, returning 1 pin\n");

    char logPath[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("ProgramData", logPath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        strcpy_s(logPath, ".");
    }
    strcat_s(logPath, "\\PhoneCam");
    CreateDirectoryA(logPath, nullptr);
    strcat_s(logPath, "\\phonecam_driver.log");

    FILE* f = fopen(logPath, "a");
    if (f) { fprintf(f, "PhoneCam Filter: EnumPins() called, returning 1 pin\n"); fclose(f); }
    return S_OK;
}

STDMETHODIMP VirtualCamFilter::FindPin(LPCWSTR Id, IPin** ppPin) {
    if (!ppPin) return E_POINTER;
    if (wcscmp(Id, L"Video") == 0 && m_pPin) {
        *ppPin = static_cast<IPin*>(m_pPin);
        (*ppPin)->AddRef();
        return S_OK;
    }
    return VFW_E_NOT_FOUND;
}

STDMETHODIMP VirtualCamFilter::QueryFilterInfo(FILTER_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    wcscpy_s(pInfo->achName, L"PhoneCam Virtual Camera");
    pInfo->pGraph = m_pGraph;
    if (m_pGraph) m_pGraph->AddRef();
    return S_OK;
}

STDMETHODIMP VirtualCamFilter::JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) {
    m_pGraph = pGraph; // weak reference, don't AddRef
    if (m_pName) {
        CoTaskMemFree(m_pName);
        m_pName = nullptr;
    }
    if (pName) {
        size_t len = wcslen(pName) + 1;
        m_pName = static_cast<LPWSTR>(CoTaskMemAlloc(len * sizeof(WCHAR)));
        if (m_pName) wcscpy_s(m_pName, len, pName);
    }
    return S_OK;
}

STDMETHODIMP VirtualCamFilter::QueryVendorInfo(LPWSTR* pVendorInfo) {
    return E_NOTIMPL;
}

// IReferenceClock
STDMETHODIMP VirtualCamFilter::GetTime(REFERENCE_TIME* pTime) {
    if (!pTime) return E_POINTER;
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    *pTime = (count.QuadPart * 10000000LL) / freq.QuadPart;
    return S_OK;
}

STDMETHODIMP VirtualCamFilter::AdviseTime(REFERENCE_TIME, REFERENCE_TIME,
                                           HEVENT, DWORD_PTR*) {
    return E_NOTIMPL;
}

STDMETHODIMP VirtualCamFilter::AdvisePeriodic(REFERENCE_TIME, REFERENCE_TIME,
                                               HSEMAPHORE, DWORD_PTR*) {
    return E_NOTIMPL;
}

STDMETHODIMP VirtualCamFilter::Unadvise(DWORD_PTR) {
    return E_NOTIMPL;
}

// ==================== Factory ====================

VirtualCamFilterFactory::VirtualCamFilterFactory() : m_refCount(1) {}
VirtualCamFilterFactory::~VirtualCamFilterFactory() {}

STDMETHODIMP VirtualCamFilterFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) VirtualCamFilterFactory::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) VirtualCamFilterFactory::Release() {
    long ref = InterlockedDecrement(&m_refCount);
    if (ref == 0) delete this;
    return ref;
}

STDMETHODIMP VirtualCamFilterFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    HRESULT hr;
    auto* filter = new VirtualCamFilter(nullptr, &hr);
    if (FAILED(hr)) {
        delete filter;
        return hr;
    }
    hr = filter->QueryInterface(riid, ppv);
    filter->Release();
    return hr;
}

STDMETHODIMP VirtualCamFilterFactory::LockServer(BOOL fLock) {
    return S_OK;
}
