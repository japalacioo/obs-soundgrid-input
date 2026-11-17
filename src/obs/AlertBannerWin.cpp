// Barra de aviso nativa (Win32) sobre la ventana principal de OBS. Es una ventana emergente
// "propiedad" de la ventana de OBS: queda siempre encima de ella, se minimiza con ella y no le
// quita el foco. Todo ocurre en el hilo de interfaz de OBS (el de Qt), cuyo bucle de mensajes
// también despacha los mensajes de esta ventana.
#include "AlertBanner.h"

#include "ObsSoundGridSource.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace sg {

namespace {

constexpr wchar_t kClassName[] = L"SoundGridAudioLossBanner";
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerMs = 250;
constexpr int kMenuBarDip = 22; // alto aproximado del menú de OBS (Qt) a 96 ppp
constexpr int kBarDip = 34;

HWND g_owner = nullptr;
HWND g_wnd = nullptr;
HWINEVENTHOOK g_hook = nullptr;
HFONT g_bold = nullptr;
HFONT g_regular = nullptr;
UINT g_fontDpi = 0;
AudioLossBannerModel g_model;
BannerState g_state;

HINSTANCE moduleInstance()
{
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&moduleInstance), &h);
    return h;
}

std::wstring widen(const std::string &s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

void ensureFonts(UINT dpi)
{
    if (g_bold && g_fontDpi == dpi)
        return;
    if (g_bold)
        DeleteObject(g_bold);
    if (g_regular)
        DeleteObject(g_regular);
    const int h = -MulDiv(10, static_cast<int>(dpi), 72);
    g_bold = CreateFontW(h, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_regular = CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontDpi = dpi;
}

COLORREF background(BannerLevel l)
{
    switch (l) {
    case BannerLevel::Lost: return RGB(0xC6, 0x28, 0x28);       // rojo
    case BannerLevel::Recovering: return RGB(0xE6, 0x51, 0x00); // ámbar
    case BannerLevel::Recovered: return RGB(0x2E, 0x7D, 0x32);  // verde
    default: return RGB(0, 0, 0);
    }
}

// Coloca la barra bajo el menú de OBS, a todo el ancho del área cliente, o la oculta.
void layout()
{
    if (!g_wnd)
        return;
    const bool show = g_state.level != BannerLevel::Hidden && g_owner && IsWindow(g_owner) &&
                      IsWindowVisible(g_owner) && !IsIconic(g_owner);
    if (!show) {
        if (IsWindowVisible(g_wnd))
            ShowWindow(g_wnd, SW_HIDE);
        return;
    }
    RECT rc{};
    GetClientRect(g_owner, &rc);
    POINT origin{0, 0};
    ClientToScreen(g_owner, &origin);
    const UINT dpi = GetDpiForWindow(g_owner);
    const int top = origin.y + MulDiv(kMenuBarDip, static_cast<int>(dpi), 96);
    const int height = MulDiv(kBarDip, static_cast<int>(dpi), 96);
    SetWindowPos(g_wnd, nullptr, origin.x, top, rc.right - rc.left, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc{};
    GetClientRect(hwnd, &rc);
    // Doble búfer para que no parpadee al actualizar el texto.
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    HBRUSH br = CreateSolidBrush(background(g_state.level));
    FillRect(mem, &rc, br);
    DeleteObject(br);

    const UINT dpi = GetDpiForWindow(hwnd);
    ensureFonts(dpi);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 255, 255));
    RECT text = rc;
    text.left += MulDiv(14, static_cast<int>(dpi), 96);
    text.right -= MulDiv(14, static_cast<int>(dpi), 96);

    const std::wstring icon = g_state.level == BannerLevel::Recovered ? L"✔  " : L"⚠  ";
    const std::wstring head = icon + widen(g_state.headline);
    HGDIOBJ oldFont = SelectObject(mem, g_bold);
    RECT measure = text;
    DrawTextW(mem, head.c_str(), -1, &measure, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    DrawTextW(mem, head.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    text.left += (measure.right - measure.left) + MulDiv(12, static_cast<int>(dpi), 96);
    SelectObject(mem, g_regular);
    const std::wstring detail = L"—  " + widen(g_state.detail);
    DrawTextW(mem, detail.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(mem, oldFont);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void tick()
{
    const BannerState st = g_model.update(collectBannerInputs(), static_cast<int64_t>(os_gettime_ns()));
    if (st != g_state) {
        const bool becameLost = st.level == BannerLevel::Lost && g_state.level != BannerLevel::Lost;
        if (st.level != g_state.level)
            blog(st.level == BannerLevel::Lost ? LOG_WARNING : LOG_INFO, "[soundgrid] aviso en OBS: %s %s",
                 st.level == BannerLevel::Hidden ? "(oculto)" : st.headline.c_str(), st.detail.c_str());
        g_state = st;
        if (becameLost && g_owner) {
            // Si OBS no está en primer plano, la barra de tareas parpadea hasta que el operador lo mire.
            FLASHWINFO fi{sizeof(fi), g_owner, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0};
            FlashWindowEx(&fi);
        }
        if (g_wnd)
            InvalidateRect(g_wnd, nullptr, FALSE);
    }
    layout();
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (wp == kTimerId)
            tick();
        return 0;
    case WM_PAINT:
        paint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649) /* IDC_HAND */));
        return TRUE;
    case WM_LBUTTONUP:
        openSourcePropertiesByName(g_state.sourceToOpen);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// La ventana de OBS se movió o cambió de tamaño: recolocar al instante (sin esperar al temporizador).
void CALLBACK onLocationChange(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG, DWORD, DWORD)
{
    if (hwnd == g_owner && idObject == OBJID_WINDOW)
        layout();
}

} // namespace

bool alertBannerAvailable()
{
    return true;
}

void startAlertBanner()
{
    if (g_wnd)
        return;
    g_owner = static_cast<HWND>(obs_frontend_get_main_window_handle());
    if (!g_owner || !IsWindow(g_owner)) {
        blog(LOG_WARNING, "[soundgrid] no se encontró la ventana principal de OBS: el aviso rojo no estará disponible");
        g_owner = nullptr;
        return;
    }
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = moduleInstance();
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32649) /* IDC_HAND */);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc); // si ya existe (recarga), CreateWindow la usa igualmente
    g_wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"SoundGrid", WS_POPUP, 0, 0, 0, 0,
                            g_owner, nullptr, wc.hInstance, nullptr);
    if (!g_wnd) {
        blog(LOG_WARNING, "[soundgrid] no se pudo crear el aviso en OBS (error %lu)", GetLastError());
        return;
    }
    SetTimer(g_wnd, kTimerId, kTimerMs, nullptr);
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(g_owner, &pid);
    g_hook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, onLocationChange, pid,
                             tid, WINEVENT_OUTOFCONTEXT);
    blog(LOG_INFO, "[soundgrid] aviso de audio perdido activo en la ventana principal de OBS");
}

void stopAlertBanner()
{
    if (g_hook) {
        UnhookWinEvent(g_hook);
        g_hook = nullptr;
    }
    if (g_wnd) {
        if (IsWindow(g_wnd)) {
            KillTimer(g_wnd, kTimerId);
            DestroyWindow(g_wnd);
        }
        g_wnd = nullptr;
    }
    UnregisterClassW(kClassName, moduleInstance());
    if (g_bold)
        DeleteObject(g_bold);
    if (g_regular)
        DeleteObject(g_regular);
    g_bold = g_regular = nullptr;
    g_fontDpi = 0;
    g_owner = nullptr;
    g_state = BannerState{};
}

} // namespace sg
