// ClassFactory.h - COM IClassFactory for CDeskBand
#pragma once
#include "pch.h"

class CDeskBand;
extern LONG g_cDllRef;
extern CDeskBand* CreateDeskBand();

class CClassFactory : public IClassFactory
{
public:
    CClassFactory()  : m_cRef(1) { InterlockedIncrement(&g_cDllRef); }
    ~CClassFactory()             { InterlockedDecrement(&g_cDllRef); }

    STDMETHOD_(ULONG, AddRef)() override
    {
        return InterlockedIncrement(&m_cRef);
    }
    STDMETHOD_(ULONG, Release)() override
    {
        ULONG c = InterlockedDecrement(&m_cRef);
        if (c == 0) delete this;
        return c;
    }
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IClassFactory))
        {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD(CreateInstance)(IUnknown* pOuter, REFIID riid, void** ppv) override
    {
        if (!ppv)    return E_POINTER;
        if (pOuter)  return CLASS_E_NOAGGREGATION;
        CDeskBand* p = CreateDeskBand();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    STDMETHOD(LockServer)(BOOL bLock) override
    {
        if (bLock) InterlockedIncrement(&g_cDllRef);
        else       InterlockedDecrement(&g_cDllRef);
        return S_OK;
    }

private:
    LONG m_cRef;
};
