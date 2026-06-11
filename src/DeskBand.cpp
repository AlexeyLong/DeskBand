// DeskBand.cpp - Cryptocurrency Price Ticker implementation
#include "pch.h"
#include "DeskBand.h"

// Flash colors per spec
static const COLORREF CLR_UP   = RGB(50,  220, 50);
static const COLORREF CLR_DOWN = RGB(255, 61,  48);

// ═════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═════════════════════════════════════════════════════════════
CDeskBand::CDeskBand()
    : m_cRef(1), m_pSite(nullptr), m_hwnd(nullptr)
    , m_hwndParent(nullptr), m_dwBandID(0)
    , m_bCompositionEnabled(FALSE)
    , m_pairIndex(0)                        // BTCUSD default
    , m_intervalMs(DEFAULT_INTERVAL_MS)     // 30s default
    , m_style(UpdateStyle::Background)      // highlight bg default
    , m_provider(Provider::CoinGecko)       // CoinGecko default
    , m_price(0.0), m_prevPrice(0.0)
    , m_priceValid(false), m_connecting(false)
    , m_flashState(FlashState::None)
    , m_flashTimerID(0), m_updateTimerID(0)
    , m_fetching(false), m_hFont(nullptr)
{
    InterlockedIncrement(&g_cDllRef);
    LoadSettings();
    RebuildFont();
}

CDeskBand::~CDeskBand()
{
    StopTimers();
    if (m_thread.joinable()) m_thread.join();
    DestroyBandWindow();
    if (m_pSite)  { m_pSite->Release(); m_pSite = nullptr; }
    if (m_hFont)  { DeleteObject(m_hFont); m_hFont = nullptr; }
    InterlockedDecrement(&g_cDllRef);
}

// ═════════════════════════════════════════════════════════════
// IUnknown
// ═════════════════════════════════════════════════════════════
ULONG CDeskBand::AddRef()  { return InterlockedIncrement(&m_cRef); }
ULONG CDeskBand::Release()
{
    ULONG c = InterlockedDecrement(&m_cRef);
    if (c == 0) delete this;
    return c;
}
HRESULT CDeskBand::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown)      ||
        IsEqualIID(riid, IID_IOleWindow)     ||
        IsEqualIID(riid, IID_IDockingWindow) ||
        IsEqualIID(riid, IID_IDeskBand)      ||
        IsEqualIID(riid, IID_IDeskBand2))
        *ppv = static_cast<IDeskBand2*>(this);
    else if (IsEqualIID(riid, IID_IObjectWithSite))
        *ppv = static_cast<IObjectWithSite*>(this);
    else if (IsEqualIID(riid, IID_IPersist) ||
             IsEqualIID(riid, IID_IPersistStream))
        *ppv = static_cast<IPersistStream*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

// ═════════════════════════════════════════════════════════════
// IOleWindow / IDockingWindow
// ═════════════════════════════════════════════════════════════
HRESULT CDeskBand::GetWindow(HWND* phwnd)
{
    if (!phwnd) return E_POINTER;
    *phwnd = m_hwnd;
    return m_hwnd ? S_OK : E_FAIL;
}
HRESULT CDeskBand::ShowDW(BOOL bShow)
{
    if (m_hwnd) ShowWindow(m_hwnd, bShow ? SW_SHOW : SW_HIDE);
    return S_OK;
}
HRESULT CDeskBand::CloseDW(DWORD)
{
    StopTimers();
    ShowDW(FALSE);
    DestroyBandWindow();
    return S_OK;
}

// ═════════════════════════════════════════════════════════════
// IDeskBand - report band size dynamically
// ═════════════════════════════════════════════════════════════
HRESULT CDeskBand::GetBandInfo(DWORD dwBandID, DWORD, DESKBANDINFO* pdbi)
{
    if (!pdbi) return E_INVALIDARG;
    m_dwBandID = dwBandID;

    int w = CalcIdealWidth();

    if (pdbi->dwMask & DBIM_MINSIZE) { pdbi->ptMinSize.x = w; pdbi->ptMinSize.y = BAND_HEIGHT; }
    if (pdbi->dwMask & DBIM_MAXSIZE) { pdbi->ptMaxSize.x = w; pdbi->ptMaxSize.y = BAND_HEIGHT; }
    if (pdbi->dwMask & DBIM_ACTUAL)  { pdbi->ptActual.x  = w; pdbi->ptActual.y  = BAND_HEIGHT; }
    if (pdbi->dwMask & DBIM_TITLE)   { pdbi->wszTitle[0] = L'\0'; }
    if (pdbi->dwMask & DBIM_MODEFLAGS) pdbi->dwModeFlags = DBIMF_NORMAL | DBIMF_VARIABLEHEIGHT;
    if (pdbi->dwMask & DBIM_BKCOLOR)   pdbi->dwMask &= ~DBIM_BKCOLOR;
    return S_OK;
}

// ═════════════════════════════════════════════════════════════
// IDeskBand2
// ═════════════════════════════════════════════════════════════
HRESULT CDeskBand::CanRenderComposited(BOOL* p) { if (p) *p = TRUE; return S_OK; }
HRESULT CDeskBand::SetCompositionState(BOOL b)
{
    m_bCompositionEnabled = b;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
    return S_OK;
}
HRESULT CDeskBand::GetCompositionState(BOOL* p) { if (p) *p = m_bCompositionEnabled; return S_OK; }

// ═════════════════════════════════════════════════════════════
// IObjectWithSite
// ═════════════════════════════════════════════════════════════
HRESULT CDeskBand::SetSite(IUnknown* pUnkSite)
{
    if (m_pSite) { m_pSite->Release(); m_pSite = nullptr; }
    DestroyBandWindow();
    StopTimers();
    if (!pUnkSite) return S_OK;

    IOleWindow* pOW = nullptr;
    if (FAILED(pUnkSite->QueryInterface(IID_IOleWindow, (void**)&pOW)))
        return E_FAIL;
    pOW->GetWindow(&m_hwndParent);
    pOW->Release();
    if (!m_hwndParent) return E_FAIL;

    m_pSite = pUnkSite;
    m_pSite->AddRef();

    // Register window class (ignore already-registered error)
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = CDeskBand::WndProc;
    wc.hInstance     = g_hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);

    CreateBandWindow(m_hwndParent);
    StartTimer();
    TriggerFetch();
    return S_OK;
}
HRESULT CDeskBand::GetSite(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (!m_pSite) { *ppv = nullptr; return E_FAIL; }
    return m_pSite->QueryInterface(riid, ppv);
}
HRESULT CDeskBand::GetClassID(CLSID* p)
{
    if (!p) return E_POINTER;
    *p = CLSID_DeskBand;
    return S_OK;
}

// ═════════════════════════════════════════════════════════════
// Window helpers
// ═════════════════════════════════════════════════════════════
BOOL CDeskBand::CreateBandWindow(HWND hwndParent)
{
    m_hwnd = CreateWindowExW(
        WS_EX_TRANSPARENT,
        WND_CLASS, nullptr,
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, BAND_MIN_WIDTH, BAND_HEIGHT,
        hwndParent, nullptr, g_hInst, this);
    return m_hwnd != nullptr;
}
void CDeskBand::DestroyBandWindow()
{
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

void CDeskBand::RebuildFont()
{
    if (m_hFont) { DeleteObject(m_hFont); m_hFont = nullptr; }
    LOGFONTW lf = {};
    HDC hdc = GetDC(nullptr);
    lf.lfHeight  = -MulDiv(DEFAULT_FONT_SIZE, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, hdc);
    lf.lfWeight  = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    m_hFont = CreateFontIndirectW(&lf);
}

int CDeskBand::MeasureText(const std::wstring& text)
{
    if (text.empty() || !m_hFont) return BAND_MIN_WIDTH;
    HDC   hdc  = GetDC(nullptr);
    HFONT hOld = (HFONT)SelectObject(hdc, m_hFont);
    SIZE  sz   = {};
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz);
    SelectObject(hdc, hOld);
    ReleaseDC(nullptr, hdc);
    return sz.cx;
}

int CDeskBand::CalcIdealWidth()
{
    std::wstring text;
    if (m_connecting || !m_priceValid)
        text = L"Refreshing...";
    else
    {
        // Format price with thousands separator
        wchar_t szNum[64];
        if      (m_price >= 10000.0) swprintf_s(szNum, L"%.0f", m_price);
        else if (m_price >= 1.0)     swprintf_s(szNum, L"%.2f", m_price);
        else                         swprintf_s(szNum, L"%.4f", m_price);

        std::wstring num(szNum);
        size_t dot    = num.find(L'.');
        size_t intEnd = (dot == std::wstring::npos) ? num.size() : dot;
        int ins = (int)intEnd - 3;
        while (ins > 0) { num.insert(ins, 1, L','); ins -= 3; }

        text = std::wstring(g_pairs[m_pairIndex].binSymbol).substr(0, 3) + L" $" + num;
    }
    int w = MeasureText(text) + 20;    // 10px padding each side
    if (w < 60)  w = 60;
    if (w > BAND_MAX_WIDTH) w = BAND_MAX_WIDTH;
    return w;
}

// ═════════════════════════════════════════════════════════════
// WndProc
// ═════════════════════════════════════════════════════════════
LRESULT CALLBACK CDeskBand::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    CDeskBand* p = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        p = reinterpret_cast<CDeskBand*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)p);
    }
    else
        p = reinterpret_cast<CDeskBand*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    return p ? p->OnMessage(hwnd, msg, wp, lp)
             : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CDeskBand::OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    // ── Paint ──
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:      OnPaint(hwnd); return 0;

    // ── Timers ──
    case WM_TIMER:
        if ((UINT_PTR)wp == m_updateTimerID)
            TriggerFetch();
        else if ((UINT_PTR)wp == m_flashTimerID)
        {
            KillTimer(hwnd, m_flashTimerID);
            m_flashTimerID = 0;
            m_flashState   = FlashState::None;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    // ── Price arrived from fetch thread ──
    case WMU_PRICE_READY:
    {
        // Resize window to fit new text
        int w = CalcIdealWidth();
        SetWindowPos(hwnd, nullptr, 0, 0, w, BAND_HEIGHT,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(GetParent(hwnd), WM_SIZE, 0, 0);

        // Start flash timer if needed
        if (m_flashState != FlashState::None &&
            m_style != UpdateStyle::None)
        {
            if (m_flashTimerID) KillTimer(hwnd, m_flashTimerID);
            m_flashTimerID = SetTimer(hwnd, 2, FLASH_DURATION_MS, nullptr);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    // ── No connection ──
    case WMU_FETCH_FAILED:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    // ── Context menu ──
    case WM_RBUTTONUP:
        ShowContextMenu(hwnd);
        return 0;

    // ── Double-click = refresh now ──
    case WM_LBUTTONDBLCLK:
        TriggerFetch();
        return 0;

    // ── Menu commands ──
    case WM_COMMAND:
        OnCommand(LOWORD(wp));
        return 0;

    case WM_DESTROY:
        StopTimers();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void CDeskBand::OnCommand(WORD cmd)
{
    switch (cmd)
    {
    // Coin pairs
    case IDM_PAIR_BTCUSD: m_pairIndex = 0; m_priceValid = false; TriggerFetch(); SaveSettings(); break;
    case IDM_PAIR_ETHUSD: m_pairIndex = 1; m_priceValid = false; TriggerFetch(); SaveSettings(); break;
    case IDM_PAIR_BNBUSD: m_pairIndex = 2; m_priceValid = false; TriggerFetch(); SaveSettings(); break;
    case IDM_PAIR_LTCUSD: m_pairIndex = 3; m_priceValid = false; TriggerFetch(); SaveSettings(); break;

    // Frequency
    case IDM_FREQ_30:  m_intervalMs = 30000;  StopTimers(); StartTimer(); SaveSettings(); break;
    case IDM_FREQ_60:  m_intervalMs = 60000;  StopTimers(); StartTimer(); SaveSettings(); break;
    case IDM_FREQ_300: m_intervalMs = 300000; StopTimers(); StartTimer(); SaveSettings(); break;

    // Style
    case IDM_STYLE_NONE: m_style = UpdateStyle::None;       SaveSettings(); break;
    case IDM_STYLE_TEXT: m_style = UpdateStyle::TextOnly;   SaveSettings(); break;
    case IDM_STYLE_BG:   m_style = UpdateStyle::Background; SaveSettings(); break;

    // Provider - switch immediately, re-fetch
    case IDM_PROV_COINGECKO: m_provider = Provider::CoinGecko;       m_priceValid = false; TriggerFetch(); SaveSettings(); break;
    case IDM_PROV_BINANCE:   m_provider = Provider::BinanceFutures;  m_priceValid = false; TriggerFetch(); SaveSettings(); break;
    case IDM_PROV_OKX:       m_provider = Provider::OKX;             m_priceValid = false; TriggerFetch(); SaveSettings(); break;
    }
}

// ═════════════════════════════════════════════════════════════
// Context menu
// ═════════════════════════════════════════════════════════════
void CDeskBand::ShowContextMenu(HWND hwnd)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // Version label (grayed, non-clickable)
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED | MF_DISABLED, 0, APP_VERSION);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Update Style ──
    HMENU hStyle = CreatePopupMenu();
    AppendMenuW(hStyle, MF_STRING | (m_style==UpdateStyle::None       ? MF_CHECKED:0), IDM_STYLE_NONE, L"No highlighting");
    AppendMenuW(hStyle, MF_STRING | (m_style==UpdateStyle::TextOnly   ? MF_CHECKED:0), IDM_STYLE_TEXT, L"Highlight text only");
    AppendMenuW(hStyle, MF_STRING | (m_style==UpdateStyle::Background ? MF_CHECKED:0), IDM_STYLE_BG,   L"Highlight background");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hStyle, L"Update Style");

    // ── Update Frequency ──
    HMENU hFreq = CreatePopupMenu();
    AppendMenuW(hFreq, MF_STRING | (m_intervalMs==30000  ? MF_CHECKED:0), IDM_FREQ_30,  L"30 seconds");
    AppendMenuW(hFreq, MF_STRING | (m_intervalMs==60000  ? MF_CHECKED:0), IDM_FREQ_60,  L"1 minute");
    AppendMenuW(hFreq, MF_STRING | (m_intervalMs==300000 ? MF_CHECKED:0), IDM_FREQ_300, L"5 minutes");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFreq, L"Update Frequency");

    // ── Cryptocurrencies ──
    HMENU hPairs = CreatePopupMenu();
    AppendMenuW(hPairs, MF_STRING | (m_pairIndex==0 ? MF_CHECKED:0), IDM_PAIR_BTCUSD, L"BTCUSD");
    AppendMenuW(hPairs, MF_STRING | (m_pairIndex==1 ? MF_CHECKED:0), IDM_PAIR_ETHUSD, L"ETHUSD");
    AppendMenuW(hPairs, MF_STRING | (m_pairIndex==2 ? MF_CHECKED:0), IDM_PAIR_BNBUSD, L"BNBUSD");
    AppendMenuW(hPairs, MF_STRING | (m_pairIndex==3 ? MF_CHECKED:0), IDM_PAIR_LTCUSD, L"LTCUSD");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPairs, L"Cryptocurrencies");

    // ── Market Data Provider ──
    HMENU hProv = CreatePopupMenu();
    AppendMenuW(hProv, MF_STRING | (m_provider==Provider::CoinGecko      ? MF_CHECKED:0), IDM_PROV_COINGECKO, L"CoinGecko");
    AppendMenuW(hProv, MF_STRING | (m_provider==Provider::BinanceFutures ? MF_CHECKED:0), IDM_PROV_BINANCE,   L"Binance Futures");
    AppendMenuW(hProv, MF_STRING | (m_provider==Provider::OKX            ? MF_CHECKED:0), IDM_PROV_OKX,       L"OKX");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hProv, L"Market Data Provider");

    // Show
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hwnd, nullptr);

    DestroyMenu(hPairs);
    DestroyMenu(hFreq);
    DestroyMenu(hStyle);
    DestroyMenu(hProv);
    DestroyMenu(hMenu);
}

// ═════════════════════════════════════════════════════════════
// Paint
// ═════════════════════════════════════════════════════════════
bool CDeskBand::IsSystemDark()
{
    HKEY hKey = nullptr;
    DWORD val = 1, sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueExW(hKey, L"SystemUsesLightTheme",
                         nullptr, nullptr, (BYTE*)&val, &sz);
        RegCloseKey(hKey);
    }
    return val == 0;
}

void CDeskBand::OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    // Off-screen buffer (no flicker)
    HDC     hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hBmp   = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP hOld   = (HBITMAP)SelectObject(hdcMem, hBmp);

    // ── Background color ──
    COLORREF bkColor = RGB(0, 0, 0);    // default: black per spec

    if (m_flashState != FlashState::None)
    {
        if (m_style == UpdateStyle::Background)
            bkColor = (m_flashState == FlashState::Up) ? CLR_UP : CLR_DOWN;
    }

    HBRUSH hbr = CreateSolidBrush(bkColor);
    FillRect(hdcMem, &rc, hbr);
    DeleteObject(hbr);

    // ── Text ──
    SelectObject(hdcMem, m_hFont);
    SetBkMode(hdcMem, TRANSPARENT);

    // Text color
    COLORREF textColor = RGB(255, 255, 255);   // white on black default

    if (m_flashState != FlashState::None && m_style == UpdateStyle::TextOnly)
        textColor = (m_flashState == FlashState::Up) ? CLR_UP : CLR_DOWN;
    else if (m_flashState != FlashState::None && m_style == UpdateStyle::Background)
        textColor = RGB(255, 255, 255);  // always white on colored bg

    SetTextColor(hdcMem, textColor);

    // Build display string
    std::wstring text;
    if (m_connecting || !m_priceValid)
    {
        text = L"Refreshing...";
    }
    else
    {
        wchar_t szNum[64];
        if      (m_price >= 10000.0) swprintf_s(szNum, L"%.0f", m_price);
        else if (m_price >= 1.0)     swprintf_s(szNum, L"%.2f", m_price);
        else                         swprintf_s(szNum, L"%.4f", m_price);

        std::wstring num(szNum);
        size_t dot    = num.find(L'.');
        size_t intEnd = (dot == std::wstring::npos) ? num.size() : dot;
        int ins = (int)intEnd - 3;
        while (ins > 0) { num.insert(ins, 1, L','); ins -= 3; }

        text = std::wstring(g_pairs[m_pairIndex].binSymbol).substr(0, 3) + L" $" + num;
    }

    DrawTextW(hdcMem, text.c_str(), -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);

    EndPaint(hwnd, &ps);
}

// ═════════════════════════════════════════════════════════════
// Timer
// ═════════════════════════════════════════════════════════════
void CDeskBand::StartTimer()
{
    if (m_hwnd)
        m_updateTimerID = SetTimer(m_hwnd, 1, m_intervalMs, nullptr);
}
void CDeskBand::StopTimers()
{
    if (m_hwnd)
    {
        if (m_updateTimerID) { KillTimer(m_hwnd, m_updateTimerID); m_updateTimerID = 0; }
        if (m_flashTimerID)  { KillTimer(m_hwnd, m_flashTimerID);  m_flashTimerID  = 0; }
    }
}

void CDeskBand::TriggerFetch()
{
    if (m_fetching) return;
    if (m_thread.joinable()) m_thread.join();

    m_fetching   = true;
    m_connecting = false;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);

    int      pairIdx  = m_pairIndex;
    Provider provider = m_provider;

    m_thread = std::thread([this, pairIdx, provider]()
    {
        FetchThread(pairIdx, provider);
    });
}

// ═════════════════════════════════════════════════════════════
// Fetch thread - dispatches to provider
// ═════════════════════════════════════════════════════════════
void CDeskBand::FetchThread(int pairIdx, Provider provider)
{
    double newPrice = 0.0;
    bool   ok       = false;

    switch (provider)
    {
    case Provider::CoinGecko:       ok = FetchCoinGecko(pairIdx, newPrice); break;
    case Provider::BinanceFutures:  ok = FetchBinance  (pairIdx, newPrice); break;
    case Provider::OKX:             ok = FetchOKX      (pairIdx, newPrice); break;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connecting = false;

        if (ok && newPrice > 0.0)
        {
            double oldPrice = m_price;
            m_prevPrice  = oldPrice;
            m_price      = newPrice;
            m_priceValid = true;

            // Flash direction
            if (oldPrice > 0.0)
            {
                if      (newPrice > oldPrice) m_flashState = FlashState::Up;
                else if (newPrice < oldPrice) m_flashState = FlashState::Down;
                else                          m_flashState = FlashState::None;
            }
        }
    }

    m_fetching = false;

    if (m_hwnd)
        PostMessageW(m_hwnd, ok ? WMU_PRICE_READY : WMU_FETCH_FAILED, 0, 0);
}

// ═════════════════════════════════════════════════════════════
// CoinGecko  -  GET /api/v3/simple/price?ids=X&vs_currencies=usd
// ═════════════════════════════════════════════════════════════
bool CDeskBand::FetchCoinGecko(int pairIdx, double& outPrice)
{
    std::wstring path = std::wstring(L"/api/v3/simple/price?ids=") +
                        g_pairs[pairIdx].geckoId +
                        L"&vs_currencies=usd";
    std::string body;
    if (!HttpsGet(L"api.coingecko.com", path, body)) return false;
    // {"bitcoin":{"usd":65000.12}}
    return ExtractDouble(body, "\"usd\"", outPrice);
}

// ═════════════════════════════════════════════════════════════
// Binance Futures  -  GET /fapi/v1/ticker/price?symbol=BTCUSDT
// ═════════════════════════════════════════════════════════════
bool CDeskBand::FetchBinance(int pairIdx, double& outPrice)
{
    std::wstring path = std::wstring(L"/fapi/v1/ticker/price?symbol=") +
                        g_pairs[pairIdx].binSymbol;
    std::string body;
    if (!HttpsGet(L"fapi.binance.com", path, body)) return false;
    // {"symbol":"BTCUSDT","price":"95123.45"}
    return ExtractDouble(body, "\"price\"", outPrice);
}

// ═════════════════════════════════════════════════════════════
// OKX  -  GET /api/v5/market/ticker?instId=BTC-USDT
// ═════════════════════════════════════════════════════════════
bool CDeskBand::FetchOKX(int pairIdx, double& outPrice)
{
    std::wstring path = std::wstring(L"/api/v5/market/ticker?instId=") +
                        g_pairs[pairIdx].okxInstId;
    std::string body;
    if (!HttpsGet(L"www.okx.com", path, body)) return false;
    // {"code":"0","data":[{"instId":"BTC-USDT","last":"95000.1",...}]}
    return ExtractDouble(body, "\"last\"", outPrice);
}

// ═════════════════════════════════════════════════════════════
// HTTPS GET  (WinHTTP, no external dependencies)
// ═════════════════════════════════════════════════════════════
bool CDeskBand::HttpsGet(const std::wstring& host,
                          const std::wstring& path,
                          std::string& outBody)
{
    outBody.clear();

    HINTERNET hS = WinHttpOpen(L"CryptoPriceTicker/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return false;

    HINTERNET hC = WinHttpConnect(hS, host.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hC) { WinHttpCloseHandle(hS); return false; }

    HINTERNET hR = WinHttpOpenRequest(hC, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false; }

    DWORD timeout = 15000;
    WinHttpSetOption(hR, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    bool ok = false;
    if (WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hR, nullptr))
    {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(hR,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status, &sz, WINHTTP_NO_HEADER_INDEX);

        if (status == 200)
        {
            DWORD read = 0; char buf[4096];
            do {
                WinHttpReadData(hR, buf, sizeof(buf) - 1, &read);
                if (read) { buf[read] = '\0'; outBody += buf; }
            } while (read > 0);
            ok = !outBody.empty();
        }
    }

    WinHttpCloseHandle(hR);
    WinHttpCloseHandle(hC);
    WinHttpCloseHandle(hS);
    return ok;
}

// ═════════════════════════════════════════════════════════════
// JSON helper  -  finds "key": <number> and parses it
// ═════════════════════════════════════════════════════════════
bool CDeskBand::ExtractDouble(const std::string& json,
                               const std::string& key,
                               double& outVal)
{
    size_t pos = json.find(key);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    pos++;
    // skip whitespace and quotes (OKX returns string values)
    while (pos < json.size() &&
           (json[pos]==' ' || json[pos]=='\t' || json[pos]=='"'))
        pos++;
    try
    {
        outVal = std::stod(json.substr(pos));
        return outVal > 0.0;
    }
    catch (...) { return false; }
}

// ═════════════════════════════════════════════════════════════
// Registry settings
// ═════════════════════════════════════════════════════════════
void CDeskBand::LoadSettings()
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return;

    DWORD val, sz = sizeof(DWORD);
    #define RQ(name, dest) \
        sz = sizeof(DWORD); \
        if (RegQueryValueExW(hKey, name, nullptr, nullptr, (BYTE*)&val, &sz) == ERROR_SUCCESS) \
            dest = (decltype(dest))val;

    RQ(L"PairIndex",  m_pairIndex)
    RQ(L"IntervalMs", m_intervalMs)
    RQ(L"Style",      m_style)
    RQ(L"Provider",   m_provider)

    #undef RQ
    RegCloseKey(hKey);
}

void CDeskBand::SaveSettings()
{
    HKEY hKey = nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (!hKey) return;

    #define RS(name, val) { DWORD _v = (DWORD)(val); \
        RegSetValueExW(hKey, name, 0, REG_DWORD, (BYTE*)&_v, sizeof(_v)); }

    RS(L"PairIndex",  m_pairIndex)
    RS(L"IntervalMs", m_intervalMs)
    RS(L"Style",      m_style)
    RS(L"Provider",   m_provider)

    #undef RS
    RegCloseKey(hKey);
}
