#pragma once
#ifndef PHONECAM_VIRTUAL_CAM_PIN_H
#define PHONECAM_VIRTUAL_CAM_PIN_H

#include <dshow.h>
#include "FrameBuffer.h"

// KS property set GUIDs for DirectShow capture pin identification
#include <initguid.h>
// {EE904F0C-D09B-11D0-ABE9-00A0C9223196}
DEFINE_GUID(AM_KSPROPSETID_Pin,
    0xEE904F0C, 0xD09B, 0x11D0, 0xAB, 0xE9, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96);
#ifndef KSPROPERTY_PIN_CATEGORY
#define KSPROPERTY_PIN_CATEGORY 2
#endif

class VirtualCamFilter;

/**
 * DirectShow output pin for the virtual camera.
 * Delivers RGB24 frames read from shared memory FrameBuffer.
 */
class VirtualCamPin : public IPin, public IQualityControl, public IAMStreamConfig,
                       public IKsPropertySet {
public:
    VirtualCamPin(VirtualCamFilter* pFilter, HRESULT* phr);
    virtual ~VirtualCamPin();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IPin
    STDMETHODIMP Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP Disconnect() override;
    STDMETHODIMP ConnectedTo(IPin** pPin) override;
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP QueryPinInfo(PIN_INFO* pInfo) override;
    STDMETHODIMP QueryDirection(PIN_DIRECTION* pPinDir) override;
    STDMETHODIMP QueryId(LPWSTR* Id) override;
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes** ppEnum) override;
    STDMETHODIMP QueryInternalConnections(IPin** apPin, ULONG* nPin) override;
    STDMETHODIMP EndOfStream() override;
    STDMETHODIMP BeginFlush() override;
    STDMETHODIMP EndFlush() override;
    STDMETHODIMP NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop,
                            double dRate) override;

    // IQualityControl
    STDMETHODIMP Notify(IBaseFilter* pSelf, Quality q) override;
    STDMETHODIMP SetSink(IQualityControl* piqc) override;

    // IAMStreamConfig
    STDMETHODIMP SetFormat(AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP GetFormat(AM_MEDIA_TYPE** ppmt) override;
    STDMETHODIMP GetNumberOfCapabilities(int* piCount, int* piSize) override;
    STDMETHODIMP GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) override;

    // IKsPropertySet
    STDMETHODIMP Set(REFGUID guidPropSet, DWORD dwPropID,
                     LPVOID pInstanceData, DWORD cbInstanceData,
                     LPVOID pPropData, DWORD cbPropData) override;
    STDMETHODIMP Get(REFGUID guidPropSet, DWORD dwPropID,
                     LPVOID pInstanceData, DWORD cbInstanceData,
                     LPVOID pPropData, DWORD cbPropData,
                     DWORD* pcbReturned) override;
    STDMETHODIMP QuerySupported(REFGUID guidPropSet, DWORD dwPropID,
                                DWORD* pTypeSupport) override;

    // Streaming control
    void StartStreaming();
    void StopStreaming();

private:
    void StreamThread();
    void RefreshPreferredFormatFromFrameBuffer();
    AM_MEDIA_TYPE GetMediaType(int width = 0, int height = 0);

    long m_refCount;
    VirtualCamFilter* m_pFilter;
    IPin* m_pConnectedPin;
    IMemAllocator* m_pAllocator;
    IMemInputPin* m_pInputPin;
    phonecam::FrameBuffer m_frameBuffer;
    HANDLE m_hThread;
    bool m_bStreaming;

    int m_width;
    int m_height;
    int m_fps;
};

#endif // PHONECAM_VIRTUAL_CAM_PIN_H
