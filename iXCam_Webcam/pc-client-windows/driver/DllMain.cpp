/**
 * DLL entry point and COM registration for the PhoneCam virtual camera.
 *
 * This DLL acts as a DirectShow source filter that registers itself
 * as a video capture device (webcam) in Windows.
 *
 * Registration:   regsvr32 PhoneCamDriver.dll
 * Unregistration: regsvr32 /u PhoneCamDriver.dll
 */

#include "VirtualCamFilter.h"
#include <dshow.h>
#include <cstdio>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

// Need CLSID_VideoInputDeviceCategory
#include <uuids.h>

static long g_dllRefCount = 0;
static HMODULE g_hModule = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (rclsid != CLSID_PhoneCamFilter)
        return CLASS_E_CLASSNOTAVAILABLE;

    auto* factory = new VirtualCamFilterFactory();
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return (g_dllRefCount == 0) ? S_OK : S_FALSE;
}

/**
 * Register the DirectShow filter as a video capture device.
 * Creates registry entries so Windows sees PhoneCam as a webcam.
 */
STDAPI DllRegisterServer() {
    char dllPath[MAX_PATH];
    GetModuleFileNameA(g_hModule, dllPath, MAX_PATH);

    // Register CLSID
    OLECHAR clsidStr[64];
    StringFromGUID2(CLSID_PhoneCamFilter, clsidStr, 64);

    char clsidAnsi[128];
    WideCharToMultiByte(CP_ACP, 0, clsidStr, -1, clsidAnsi, 128, nullptr, nullptr);

    char keyPath[256];

    // HKCR\CLSID\{...}
    sprintf_s(keyPath, "CLSID\\%s", clsidAnsi);
    HKEY hKey;
    RegCreateKeyExA(HKEY_CLASSES_ROOT, keyPath, 0, nullptr,
                    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                   (BYTE*)"PhoneCam Virtual Camera", 24);
    RegCloseKey(hKey);

    // HKCR\CLSID\{...}\InprocServer32
    sprintf_s(keyPath, "CLSID\\%s\\InprocServer32", clsidAnsi);
    RegCreateKeyExA(HKEY_CLASSES_ROOT, keyPath, 0, nullptr,
                    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                   (BYTE*)dllPath, (DWORD)strlen(dllPath) + 1);
    RegSetValueExA(hKey, "ThreadingModel", 0, REG_SZ,
                   (BYTE*)"Both", 5);
    RegCloseKey(hKey);

    // Register as Video Capture Source
    // Must declare the output pin and media types so DirectShow recognizes this as a video device
    IFilterMapper2* pMapper = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IFilterMapper2, (void**)&pMapper);
    if (SUCCEEDED(hr)) {
        // Define output pin media types
        const REGPINTYPES rgPinTypes[] = {
            { &MEDIATYPE_Video, &MEDIASUBTYPE_RGB24 }
        };

        // Define the output pin
        const REGFILTERPINS2 rgPins2[] = {
            {
                REG_PINFLAG_B_OUTPUT,       // dwFlags - output pin
                1,                          // cInstances
                1,                          // nMediaTypes
                rgPinTypes,                 // lpMediaType
                0,                          // nMediums
                nullptr,                    // lpMedium
                &PIN_CATEGORY_CAPTURE       // clsPinCategory - mark as CAPTURE pin
            }
        };

        REGFILTER2 rf2 = {};
        rf2.dwVersion = 2;                 // Version 2 supports pin categories
        rf2.dwMerit = MERIT_DO_NOT_USE;
        rf2.cPins2 = 1;
        rf2.rgPins2 = rgPins2;

        hr = pMapper->RegisterFilter(
            CLSID_PhoneCamFilter,
            L"PhoneCam Virtual Camera",
            nullptr,
            &CLSID_VideoInputDeviceCategory,
            L"PhoneCam Virtual Camera",
            &rf2
        );
        pMapper->Release();
        if (FAILED(hr)) {
            printf("PhoneCam: RegisterFilter failed hr=0x%08X\n", hr);
        }
    }

    printf("PhoneCam Virtual Camera registered successfully.\n");
    return S_OK;
}

STDAPI DllUnregisterServer() {
    // Unregister from FilterMapper
    IFilterMapper2* pMapper = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IFilterMapper2, (void**)&pMapper);
    if (SUCCEEDED(hr)) {
        pMapper->UnregisterFilter(&CLSID_VideoInputDeviceCategory,
                                   L"PhoneCam Virtual Camera",
                                   CLSID_PhoneCamFilter);
        pMapper->Release();
    }

    // Remove CLSID keys
    OLECHAR clsidStr[64];
    StringFromGUID2(CLSID_PhoneCamFilter, clsidStr, 64);
    char clsidAnsi[128];
    WideCharToMultiByte(CP_ACP, 0, clsidStr, -1, clsidAnsi, 128, nullptr, nullptr);

    char keyPath[256];
    sprintf_s(keyPath, "CLSID\\%s\\InprocServer32", clsidAnsi);
    RegDeleteKeyA(HKEY_CLASSES_ROOT, keyPath);
    sprintf_s(keyPath, "CLSID\\%s", clsidAnsi);
    RegDeleteKeyA(HKEY_CLASSES_ROOT, keyPath);

    printf("PhoneCam Virtual Camera unregistered.\n");
    return S_OK;
}
