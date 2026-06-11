// DeskBand.h - Cryptocurrency Price Ticker
// COM DeskBand object declaration
#pragma once
#include "pch.h"

// App info
#define APP_NAME        L"Cryptocurrency Price Ticker"
#define APP_VERSION     L"v1.0.3"

// Band dimensions
#define BAND_HEIGHT     30
#define BAND_MIN_WIDTH  80
#define BAND_MAX_WIDTH  400

// Default settings
#define DEFAULT_INTERVAL_MS  30000
#define DEFAULT_FONT_SIZE    9
#define FLASH_DURATION_MS    2000

// Registry key
#define REG_KEY         L"Software\\CryptoPriceTicker"

// Window class
#define WND_CLASS       L"CryptoPriceTickerWnd"

// Internal window messages
#define WMU_PRICE_READY   (WM_USER + 1)   // fetch thread -> UI: new price arrived
#define WMU_FETCH_FAILED  (WM_USER + 2)   // fetch thread -> UI: no connection

// ── Menu command IDs ──────────────────────────────────────────
// Cryptocurrencies
#define IDM_PAIR_BTCUSD   2001
#define IDM_PAIR_ETHUSD   2002
#define IDM_PAIR_BNBUSD   2003
#define IDM_PAIR_LTCUSD   2004

// Update Frequency
#define IDM_FREQ_30       2011
#define IDM_FREQ_60       2012
#define IDM_FREQ_300      2013

// Update Style
#define IDM_STYLE_NONE    2021
#define IDM_STYLE_TEXT    2022
#define IDM_STYLE_BG      2023

// Market Data Provider
#define IDM_PROV_COINGECKO  2031
#define IDM_PROV_BINANCE    2032
#define IDM_PROV_OKX        2033

// ── Enumerations ──────────────────────────────────────────────
enum class UpdateStyle { None, TextOnly, Background };
enum class Provider    { CoinGecko, BinanceFutures, OKX };
enum class FlashState  { None, Up, Down };

// ── Coin descriptor ───────────────────────────────────────────
struct CoinPair
{
    const wchar_t* label;       // "BTCUSD"
    const wchar_t* geckoId;     // "bitcoin"
    const wchar_t* binSymbol;   // "BTCUSDT"
    const wchar_t* okxInstId;   // "BTC-USDT"
};

static const CoinPair g_pairs[] =
{
    { L"BTCUSD", L"bitcoin",     L"BTCUSDT", L"BTC-USDT" },
    { L"ETHUSD", L"ethereum",    L"ETHUSDT", L"ETH-USDT" },
    { L"BNBUSD", L"binancecoin", L"BNBUSDT", L"BNB-USDT" },
    { L"LTCUSD", L"litecoin",    L"LTCUSDT", L"LTC-USDT" },
};
static const int g_pairCount = 4;

extern HINSTANCE g_hInst;
extern LONG      g_cDllRef;

// ── COM CLSID ────────────────────────────────────────────────
// {B1C2D3E4-F5A6-7B8C-9D0E-F1A2B3C4D5E6}
static const CLSID CLSID_DeskBand =
{ 0xb1c2d3e4, 0xf5a6, 0x7b8c,
  { 0x9d, 0x0e, 0xf1, 0xa2, 0xb3, 0xc4, 0xd5, 0xe6 } };

// ─────────────────────────────────────────────────────────────
// CDeskBand  -  IDeskBand2 + IObjectWithSite + IPersistStream
// ─────────────────────────────────────────────────────────────
class CDeskBand : public IDeskBand2,
                  public IObjectWithSite,
                  public IPersistStream
{
public:
    CDeskBand();
    virtual ~CDeskBand();

    // IUnknown
    STDMETHOD_(ULONG, AddRef)()  override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override;

    // IOleWindow
    STDMETHOD(GetWindow)(HWND* phwnd) override;
    STDMETHOD(ContextSensitiveHelp)(BOOL) override { return E_NOTIMPL; }

    // IDockingWindow
    STDMETHOD(ShowDW)(BOOL bShow) override;
    STDMETHOD(CloseDW)(DWORD) override;
    STDMETHOD(ResizeBorderDW)(const RECT*, IUnknown*, BOOL) override { return E_NOTIMPL; }

    // IDeskBand
    STDMETHOD(GetBandInfo)(DWORD dwBandID, DWORD dwViewMode, DESKBANDINFO* pdbi) override;

    // IDeskBand2
    STDMETHOD(CanRenderComposited)(BOOL* pfCan) override;
    STDMETHOD(SetCompositionState)(BOOL bEnabled) override;
    STDMETHOD(GetCompositionState)(BOOL* pb) override;

    // IObjectWithSite
    STDMETHOD(SetSite)(IUnknown* pUnkSite) override;
    STDMETHOD(GetSite)(REFIID riid, void** ppvSite) override;

    // IPersist
    STDMETHOD(GetClassID)(CLSID* pClassID) override;

    // IPersistStream
    STDMETHOD(IsDirty)()                    override { return S_FALSE; }
    STDMETHOD(Load)(IStream*)               override { return S_OK; }
    STDMETHOD(Save)(IStream*, BOOL)         override { return S_OK; }
    STDMETHOD(GetSizeMax)(ULARGE_INTEGER*)  override { return E_NOTIMPL; }

private:
    // Window
    BOOL CreateBandWindow(HWND hwndParent);
    void DestroyBandWindow();
    void RebuildFont();
    int  MeasureText(const std::wstring& text);
    int  CalcIdealWidth();

    // Message loop
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void    OnCommand(WORD cmd);

    // Paint
    void OnPaint(HWND hwnd);

    // Menu
    void ShowContextMenu(HWND hwnd);

    // Data fetching
    void StartTimer();
    void StopTimers();
    void TriggerFetch();
    void FetchThread(int pairIdx, Provider provider);

    // Per-provider fetch
    bool FetchCoinGecko (int pairIdx, double& outPrice);
    bool FetchBinance   (int pairIdx, double& outPrice);
    bool FetchOKX       (int pairIdx, double& outPrice);

    // HTTP helper
    bool HttpsGet(const std::wstring& host,
                  const std::wstring& path,
                  std::string& outBody);

    // JSON helper
    static bool ExtractDouble(const std::string& json,
                               const std::string& key,
                               double& outVal);

    // Settings
    void LoadSettings();
    void SaveSettings();

    // Helpers
    static bool IsSystemDark();

private:
    LONG      m_cRef;
    IUnknown* m_pSite;
    HWND      m_hwnd;
    HWND      m_hwndParent;
    DWORD     m_dwBandID;
    BOOL      m_bCompositionEnabled;

    // Settings
    int          m_pairIndex;       // index into g_pairs[]
    DWORD        m_intervalMs;
    UpdateStyle  m_style;
    Provider     m_provider;

    // Price state
    std::mutex   m_mutex;
    double       m_price;
    double       m_prevPrice;
    bool         m_priceValid;
    bool         m_connecting;      // show "Refreshing..."

    // Flash
    FlashState   m_flashState;
    UINT_PTR     m_flashTimerID;
    UINT_PTR     m_updateTimerID;

    // Fetch thread
    std::thread       m_thread;
    std::atomic<bool> m_fetching;

    // GDI
    HFONT m_hFont;
};
