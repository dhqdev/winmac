#include "notify.h"
#include "gfx.h"
#include "art.h"
#include "config.h"

namespace notify {
namespace {
const wchar_t* kCls = L"WinMac.Notify";
enum { T_ANIM = 1, T_HIDE = 2 };
HWND g_wnd = nullptr;
Surface g_surf;
std::wstring g_title, g_body;
std::function<void()> g_click;
float g_k = 0, g_target = 0, g_s = 1;
int g_w = 0, g_h = 0, g_x = 0, g_y = 0;
DWORD g_last = 0;
bool g_hover = false;

void Render() {
    auto* c = g_surf.Begin(g_w, g_h);
    if (!c) return;
    float s = g_s;
    bool dark = gfx::DarkMode();
    float e = 1 - std::pow(1 - g_k, 3.f);
    float off = (1 - e) * 40 * s;
    c->SetTransform(D2D1::Matrix3x2F::Translation(off, 0));
    auto rr = gfx::RR(10 * s, 8 * s, g_w - 10 * s, g_h - 12 * s, 16 * s);
    gfx::Shadow(c, rr, 10 * s, 0.30f, 3 * s);
    gfx::Fill(c, rr, gfx::Panel(dark, 0.80f, 0.86f));
    gfx::GlassOverlay(c, rr, dark, 0.7f);
    float ix = rr.rect.left + 12 * s, iy = rr.rect.top + 12 * s, isz = 38 * s;
    art::AppIcon(c, D2D1::RectF(ix, iy, ix + isz, iy + isz));
    D2D1_COLOR_F fg = dark ? gfx::White(0.95f) : gfx::Black(0.86f), sub = dark ? gfx::White(0.7f) : gfx::Black(0.6f);
    float tx = ix + isz + 12 * s;
    gfx::Text(c, g_title, gfx::Font(13.5f * s, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(tx, rr.rect.top + 10 * s, rr.rect.right - 14 * s, rr.rect.top + 30 * s), fg);
    ComPtr<IDWriteTextLayout> l;
    auto* f = gfx::Font(12.5f * s);
    float bw = rr.rect.right - 14 * s - tx;
    if (SUCCEEDED(gfx::dw->CreateTextLayout(g_body.c_str(), (UINT32)g_body.size(), f, bw, 36 * s, &l))) {
        l->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        DWRITE_TRIMMING t{ DWRITE_TRIMMING_GRANULARITY_WORD, 0, 0 };
        ComPtr<IDWriteInlineObject> sign;
        if (SUCCEEDED(gfx::dw->CreateEllipsisTrimmingSign(l.Get(), &sign))) l->SetTrimming(&t, sign.Get());
        c->DrawTextLayout(D2D1::Point2F(tx, rr.rect.top + 30 * s), l.Get(), gfx::Brush(c, sub).Get());
    }
    c->SetTransform(D2D1::Matrix3x2F::Identity());
    g_surf.End(g_x, g_y, (BYTE)(255 * Clamp01(e * 1.2f)));
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TIMER:
        if (w == T_ANIM) {
            DWORD now = GetTickCount();
            float dt = std::min(0.05f, (now - g_last) / 1000.f);
            g_last = now;
            g_k = g_target > g_k ? std::min(g_target, g_k + dt / 0.28f) : std::max(g_target, g_k - dt / 0.22f);
            Render();
            if (g_k == g_target) {
                KillTimer(h, T_ANIM);
                if (g_k == 0) ShowWindow(h, SW_HIDE);
            }
        } else if (w == T_HIDE) {
            if (g_hover) return 0;
            KillTimer(h, T_HIDE);
            g_target = 0;
            g_last = GetTickCount();
            SetTimer(h, T_ANIM, 15, nullptr);
        }
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
        if (!g_hover) {
            g_hover = true;
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
        }
        return 0;
    case WM_MOUSELEAVE:
        g_hover = false;
        return 0;
    case WM_LBUTTONUP: {
        auto fn = g_click;
        g_click = nullptr;
        g_target = 0;
        g_last = GetTickCount();
        SetTimer(h, T_ANIM, 15, nullptr);
        if (fn) fn();
        return 0;
    }
    case WM_RBUTTONUP:
        g_click = nullptr;
        g_target = 0;
        g_last = GetTickCount();
        SetTimer(h, T_ANIM, 15, nullptr);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}  // namespace

void Show(const std::wstring& title, const std::wstring& body, std::function<void()> onClick, int ms) {
    if (!g_wnd) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = g_inst;
        wc.lpszClassName = kCls;
        wc.hCursor = LoadCursor(nullptr, IDC_HAND);
        RegisterClassW(&wc);
        g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kCls, L"WinMac Notificação", WS_POPUP, 0, 0, 1, 1,
                                nullptr, nullptr, g_inst, nullptr);
        g_surf.Attach(g_wnd);
    }
    g_title = title;
    g_body = body;
    g_click = std::move(onClick);
    g_s = gfx::Scale();
    g_w = (int)(380 * g_s);
    g_h = (int)(92 * g_s);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    g_x = mi.rcMonitor.right - g_w - (int)(6 * g_s);
    g_y = mi.rcMonitor.top + (int)(std::max(26, cfg::Int(L"MenuBar", L"Height", 28)) * g_s) + (int)(4 * g_s);
    g_k = 0;
    g_target = 1;
    g_last = GetTickCount();
    Render();
    SetWindowPos(g_wnd, HWND_TOPMOST, g_x, g_y, g_w, g_h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(g_wnd, T_ANIM, 15, nullptr);
    SetTimer(g_wnd, T_HIDE, ms, nullptr);
}
}  // namespace notify
