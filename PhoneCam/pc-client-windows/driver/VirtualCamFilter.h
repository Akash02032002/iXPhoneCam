#pragma once
#ifndef PHONECAM_VIRTUAL_CAM_FILTER_H
#define PHONECAM_VIRTUAL_CAM_FILTER_H

/**
 * DirectShow Virtual Camera Source Filter.
 *
 * This filter registers as a video capture device in Windows.
 * Applications like Zoom, Teams, OBS, etc. will see "PhoneCam" as
 * an available camera/webcam.
 *
 * The filter reads decoded RGB frames from the shared memory FrameBuffer
 * (written by PhoneCamService) and delivers them to the DirectShow graph.
 *
 * Registration:
 *   regsvr32 PhoneCamDriver.dll       (register)
 *   regsvr32 /u PhoneCamDriver.dll    (unregister)
 */

#include <dshow.h>
#include <initguid.h>
#include <cstring>

// {E3F2C5A0-1234-4B8E-9A0F-ABCDEF123456}
DEFINE_GUID(CLSID_PhoneCamFilter,
    0xe3f2c5a0, 0x1234, 0x4b8e,
    0x9a, 0x0f, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56);

class VirtualCamPin;

// ==================== IEnumPins implementation ====================
class CEnumPins : public IEnumPins {
public:
    CEnumPins(IPin* pPin) : m_refCount(1), m_pPin(pPin), m_index(0) { if (m_pPin) m_pPin->AddRef(); }
    ~CEnumPins() { if (m_pPin) m_pPin->Release(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IEnumPins) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refCount); }
    STDMETHODIMP_(ULONG) Release() override { long r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    STDMETHODIMP Next(ULONG cPins, IPin** ppPins, ULONG* pcFetched) override {
        if (!ppPins) return E_POINTER;
        ULONG fetched = 0;
        if (m_index == 0 && cPins > 0 && m_pPin) {
            ppPins[0] = m_pPin; m_pPin->AddRef(); fetched = 1; m_index++;
        }
        if (pcFetched) *pcFetched = fetched;
        return (fetched == cPins) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG cPins) override { m_index += cPins; return (m_index <= 1) ? S_OK : S_FALSE; }
    STDMETHODIMP Reset() override { m_index = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumPins** ppEnum) override { auto* p = new CEnumPins(m_pPin); p->m_index = m_index; *ppEnum = p; return S_OK; }
private:
    long m_refCount; IPin* m_pPin; ULONG m_index;
};

// ==================== IEnumMediaTypes implementation ====================
class CEnumMediaTypes : public IEnumMediaTypes {
public:
    CEnumMediaTypes(const AM_MEDIA_TYPE& mt) : m_refCount(1), m_mt(mt), m_index(0) {
        // Deep copy the format block
        if (mt.pbFormat && mt.cbFormat > 0) {
            m_mt.pbFormat = (BYTE*)CoTaskMemAlloc(mt.cbFormat);
            memcpy(m_mt.pbFormat, mt.pbFormat, mt.cbFormat);
        }
    }
    ~CEnumMediaTypes() { if (m_mt.pbFormat) CoTaskMemFree(m_mt.pbFormat); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IEnumMediaTypes) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refCount); }
    STDMETHODIMP_(ULONG) Release() override { long r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    STDMETHODIMP Next(ULONG cMT, AM_MEDIA_TYPE** ppMT, ULONG* pcFetched) override {
        if (!ppMT) return E_POINTER;
        ULONG fetched = 0;
        if (m_index == 0 && cMT > 0) {
            ppMT[0] = (AM_MEDIA_TYPE*)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
            *ppMT[0] = m_mt;
            if (m_mt.pbFormat && m_mt.cbFormat > 0) {
                ppMT[0]->pbFormat = (BYTE*)CoTaskMemAlloc(m_mt.cbFormat);
                memcpy(ppMT[0]->pbFormat, m_mt.pbFormat, m_mt.cbFormat);
            }
            fetched = 1; m_index++;
        }
        if (pcFetched) *pcFetched = fetched;
        return (fetched == cMT) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG c) override { m_index += c; return (m_index <= 1) ? S_OK : S_FALSE; }
    STDMETHODIMP Reset() override { m_index = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumMediaTypes** ppEnum) override { auto* p = new CEnumMediaTypes(m_mt); p->m_index = m_index; *ppEnum = p; return S_OK; }
private:
    long m_refCount; AM_MEDIA_TYPE m_mt; ULONG m_index;
};

class VirtualCamFilter : public IBaseFilter, public IReferenceClock {
public:
    VirtualCamFilter(IUnknown* pUnk, HRESULT* phr);
    virtual ~VirtualCamFilter();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IPersist
    STDMETHODIMP GetClassID(CLSID* pClsID) override;

    // IMediaFilter
    STDMETHODIMP Stop() override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Run(REFERENCE_TIME tStart) override;
    STDMETHODIMP GetState(DWORD dwMilliSecsTimeout, FILTER_STATE* State) override;
    STDMETHODIMP SetSyncSource(IReferenceClock* pClock) override;
    STDMETHODIMP GetSyncSource(IReferenceClock** pClock) override;

    // IBaseFilter
    STDMETHODIMP EnumPins(IEnumPins** ppEnum) override;
    STDMETHODIMP FindPin(LPCWSTR Id, IPin** ppPin) override;
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* pInfo) override;
    STDMETHODIMP JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) override;
    STDMETHODIMP QueryVendorInfo(LPWSTR* pVendorInfo) override;

    // IReferenceClock
    STDMETHODIMP GetTime(REFERENCE_TIME* pTime) override;
    STDMETHODIMP AdviseTime(REFERENCE_TIME baseTime, REFERENCE_TIME streamTime,
                            HEVENT hEvent, DWORD_PTR* pdwAdviseCookie) override;
    STDMETHODIMP AdvisePeriodic(REFERENCE_TIME startTime, REFERENCE_TIME periodTime,
                                HSEMAPHORE hSemaphore, DWORD_PTR* pdwAdviseCookie) override;
    STDMETHODIMP Unadvise(DWORD_PTR dwAdviseCookie) override;

    // Helpers
    FILTER_STATE GetFilterState() const { return m_state; }
    IFilterGraph* GetFilterGraph() const { return m_pGraph; }

private:
    long m_refCount;
    FILTER_STATE m_state;
    IFilterGraph* m_pGraph;
    IReferenceClock* m_pClock;
    LPWSTR m_pName;
    VirtualCamPin* m_pPin;
    REFERENCE_TIME m_startTime;
};

// Class factory
class VirtualCamFilterFactory : public IClassFactory {
public:
    VirtualCamFilterFactory();
    virtual ~VirtualCamFilterFactory();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    STDMETHODIMP LockServer(BOOL fLock) override;

private:
    long m_refCount;
};

#endif // PHONECAM_VIRTUAL_CAM_FILTER_H
