// Animações de abrir/fechar janelas: um "fantasma" de vidro sai do ícone do dock e se expande até a janela
// (ao abrir) ou encolhe e some (ao fechar). Só roda durante ~1/4 de segundo por evento.
#include "common.h"
#include "gfx.h"
#include "apps.h"
#include "dock.h"
#include "config.h"

namespace anim {
namespace {
struct Ghost {
    HWND hwnd = nullptr;
    Surface surf;
    RECT from{}, to{};
    ComPtr<IWICBitmapSource> icon;
    bool opening = true;
    DWORD start = 0;
    int dur = 260;
};
std::vector<std::unique_ptr<Ghost>> g_ghosts;
std::set<HWND> g_known;
HWND g_timer = nullptr;
DWORD g_startTick = 0;
bool g_enabled = true;
const wchar_t* kCls = L"WinMac.Ghost";

float EaseOut(float t) { return 1 - std::pow(1 - t, 3.f); }

bool Frame(Ghost& g) {
    float t = std::min(1.f, (GetTickCount() - g.start) / (float)g.dur);
    float e = EaseOut(t);
    RECT r;
    float alpha;
    if (g.opening) {
        r.left = (LONG)Lerp((float)g.from.left, (float)g.to.left, e);
        r.top = (LONG)Lerp((float)g.from.top, (float)g.to.top, e);
        r.right = (LONG)Lerp((float)g.from.right, (float)g.to.right, e);
        r.bottom = (LONG)Lerp((float)g.from.bottom, (float)g.to.bottom, e);
        alpha = t < 0.55f ? 1.f : 1.f - (t - 0.55f) / 0.45f;
    } else {
        float sc = 1.f - 0.14f * e;
        float cx = (g.from.left + g.from.right) / 2.f, cy = (g.from.top + g.from.bottom) / 2.f;
        float hw = (g.from.right - g.from.left) / 2.f * sc, hh = (g.from.bottom - g.from.top) / 2.f * sc;
        r = RECT{ (LONG)(cx - hw), (LONG)(cy - hh + 30 * e), (LONG)(cx + hw), (LONG)(cy + hh + 30 * e) };
        alpha = 1.f - e;
    }
    int w = std::max(2, (int)(r.right - r.left)), h = std::max(2, (int)(r.bottom - r.top));
    float s = gfx::Scale();
    float rad = std::min(14 * s, std::min(w, h) / 2.f);
    if (auto* c = g.surf.Begin(w, h)) {
        bool dark = gfx::DarkMode();
        auto rr = gfx::RR(0, 0, (float)w, (float)h, rad);
        gfx::Fill(c, rr, dark ? gfx::Rgba(36, 36, 42, 0.30f) : gfx::Rgba(255, 255, 255, 0.32f));
        gfx::GlassOverlay(c, rr, dark, 1.f);
        if (g.icon) {
            float isz = std::min(96 * s, std::min(w, h) * 0.6f);
            g.surf.DrawBitmap(g.icon.Get(), D2D1::RectF(w / 2.f - isz / 2, h / 2.f - isz / 2, w / 2.f + isz / 2, h / 2.f + isz / 2), 0.9f, false);
        }
        g.surf.End(r.left, r.top, (BYTE)(255 * Clamp01(alpha)));
        gfx::SetRoundRegion(g.hwnd, 0, 0, w, h, (int)rad);
    }
    return t < 1.f;
}

LRESULT CALLBACK TimerProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER) {
        for (auto it = g_ghosts.begin(); it != g_ghosts.end();) {
            if (!Frame(**it)) {
                (*it)->surf.Reset();
                DestroyWindow((*it)->hwnd);
                it = g_ghosts.erase(it);
            } else ++it;
        }
        if (g_ghosts.empty()) KillTimer(h, 1);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void Spawn(bool opening, RECT from, RECT to, ComPtr<IWICBitmapSource> icon) {
    if (g_ghosts.size() > 4) return;
    auto g = std::make_unique<Ghost>();
    g->opening = opening;
    g->from = from;
    g->to = to;
    g->icon = icon;
    g->dur = opening ? 280 : 200;
    g->start = GetTickCount();
    g->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kCls, L"", WS_POPUP,
                              from.left, from.top, 2, 2, nullptr, nullptr, g_inst, nullptr);
    if (!g->hwnd) return;
    g->surf.Attach(g->hwnd);
    gfx::SetBlur(g->hwnd, true);
    Frame(*g);
    ShowWindow(g->hwnd, SW_SHOWNOACTIVATE);
    g_ghosts.push_back(std::move(g));
    SetTimer(g_timer, 1, 15, nullptr);
}

bool Usable(HWND h, RECT* r) {
    if (!GetWindowRect(h, r)) return false;
    int w = r->right - r->left, hh = r->bottom - r->top;
    if (w < 120 || hh < 80) return false;
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    if (r->left <= mi.rcMonitor.left && r->top <= mi.rcMonitor.top && r->right >= mi.rcMonitor.right && r->bottom >= mi.rcMonitor.bottom) return false;  // tela cheia
    return true;
}

void CALLBACK OnEvent(HWINEVENTHOOK, DWORD ev, HWND h, LONG obj, LONG child, DWORD, DWORD) {
    if (!g_enabled || !h || obj != OBJID_WINDOW || child != CHILDID_SELF) return;
    if (GetAncestor(h, GA_PARENT) != GetDesktopWindow() || IsOwnWindow(h)) return;
    if (ev == EVENT_OBJECT_SHOW) {
        if (g_known.count(h)) return;
        // espera o app terminar de posicionar a janela
        SetTimer(g_timer, (UINT_PTR)h, 30, [](HWND tw, UINT, UINT_PTR id, DWORD) {
            KillTimer(tw, id);
            HWND w = (HWND)id;
            if (!IsWindow(w) || !apps::IsTaskWindow(w) || g_known.count(w)) return;
            g_known.insert(w);
            if (GetTickCount() - g_startTick < 4000 || IsIconic(w)) return;
            RECT r;
            if (!Usable(w, &r)) return;
            RECT from;
            ComPtr<IWICBitmapSource> icon;
            if (!g_dock.IconFor(w, &from, &icon)) {
                int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
                int hw = (r.right - r.left) * 2 / 5, hh = (r.bottom - r.top) * 2 / 5;
                from = RECT{ cx - hw, cy - hh, cx + hw, cy + hh };
            }
            Spawn(true, from, r, icon);
        });
    } else if (ev == EVENT_OBJECT_HIDE || ev == EVENT_OBJECT_DESTROY) {
        if (!g_known.count(h)) return;
        g_known.erase(h);
        if (ev == EVENT_OBJECT_DESTROY || IsIconic(h)) return;
        RECT r;
        if (!Usable(h, &r)) return;
        ComPtr<IWICBitmapSource> icon;
        g_dock.IconFor(h, nullptr, &icon);
        Spawn(false, r, r, icon);
    }
}
}  // namespace

void Reload() { g_enabled = cfg::Int(L"Effects", L"OpenClose", 1) != 0; }

void Init() {
    Reload();
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = g_inst;
    wc.lpszClassName = kCls;
    RegisterClassW(&wc);
    wc.lpfnWndProc = TimerProc;
    wc.lpszClassName = L"WinMac.GhostTimer";
    RegisterClassW(&wc);
    g_timer = CreateWindowExW(0, L"WinMac.GhostTimer", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_inst, nullptr);
    g_startTick = GetTickCount();
    for (HWND h : apps::TaskWindows()) g_known.insert(h);
    SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, OnEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}
}  // namespace anim
