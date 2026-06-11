// dllmain.cpp - DLL entry point and COM registration
#include "pch.h"
#include "DeskBand.h"
#include "ClassFactory.h"

HINSTANCE g_hInst   = nullptr;
LONG      g_cDllRef = 0;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow()
{
    return (g_cDllRef == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (!IsEqualCLSID(rclsid, CLSID_DeskBand))
        return CLASS_E_CLASSNOTAVAILABLE;

    CClassFactory* pFactory = new (std::nothrow) CClassFactory();
    if (!pFactory) return E_OUTOFMEMORY;

    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
}

STDAPI DllRegisterServer()
{
    wchar_t szModule[MAX_PATH];
    GetModuleFileNameW(g_hInst, szModule, MAX_PATH);

    const wchar_t* szCLSID = L"{B1C2D3E4-F5A6-7B8C-9D0E-F1A2B3C4D5E6}";
    const wchar_t* szName  = APP_NAME;

    HKEY  hKey = nullptr;
    wchar_t szKey[256];

    // HKCR\CLSID\{...}
    wsprintfW(szKey, L"CLSID\\%s", szCLSID);
    RegCreateKeyExW(HKEY_CLASSES_ROOT, szKey, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExW(hKey, nullptr, 0, REG_SZ,
        (BYTE*)szName, (DWORD)(wcslen(szName) + 1) * 2);
    RegCloseKey(hKey);

    // HKCR\CLSID\{...}\InprocServer32
    wsprintfW(szKey, L"CLSID\\%s\\InprocServer32", szCLSID);
    RegCreateKeyExW(HKEY_CLASSES_ROOT, szKey, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExW(hKey, nullptr, 0, REG_SZ,
        (BYTE*)szModule, (DWORD)(wcslen(szModule) + 1) * 2);
    const wchar_t* szApt = L"Apartment";
    RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ,
        (BYTE*)szApt, (DWORD)(wcslen(szApt) + 1) * 2);
    RegCloseKey(hKey);

    // Register as DeskBand component category
    ICatRegister* pCR = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_StdComponentCategoriesMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_ICatRegister, (void**)&pCR)))
    {
        CATID catid = CATID_DeskBand;
        pCR->RegisterClassImplCategories(CLSID_DeskBand, 1, &catid);
        pCR->Release();
    }

    return S_OK;
}

STDAPI DllUnregisterServer()
{
    const wchar_t* szCLSID = L"{B1C2D3E4-F5A6-7B8C-9D0E-F1A2B3C4D5E6}";
    wchar_t szKey[256];

    ICatRegister* pCR = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_StdComponentCategoriesMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_ICatRegister, (void**)&pCR)))
    {
        CATID catid = CATID_DeskBand;
        pCR->UnRegisterClassImplCategories(CLSID_DeskBand, 1, &catid);
        pCR->Release();
    }

    wsprintfW(szKey, L"CLSID\\%s\\InprocServer32", szCLSID);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, szKey);

    wsprintfW(szKey, L"CLSID\\%s", szCLSID);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, szKey);

    return S_OK;
}
