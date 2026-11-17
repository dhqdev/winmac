#include "traffic.h"
#include "gfx.h"
#include "apps.h"
#include "config.h"

namespace traffic {
namespace {
const wchar_t* kCls = L"WinMac.Traffic";
const wchar_t* kTimerCls = L"WinMac.TrafficTimer";
enum { T_SYNC = 1, T_PERIODIC = 2 };

struct Ov {
    HWND target = nullptr, wnd = nullptr;
    Surface surf;
    RECT btn{};
    COLORREF bg = RGB(255, 255, 255);
    float s = 1;
    bool active = false, hover = false, tracking = false, shown = false;
    int zone = -1, press = -1;
};
std::map<HWND, std::unique_ptr<Ov>> g_ovs;
HWND g_timer = nullptr;
HWINEVENTHOOK g_hooks[6] = {};
bool g_on = false;
std::map<DWORD, bool> g_elev;
std::vector<std::wstring> g_exclude;

const D2D1_COLOR_F kFill[3] = { gfx::Rgba(255, 95, 87), gfx::Rgba(254, 188, 46), gfx::Rgba(40, 200, 64) };
const D2D1_COLOR_F kEdge[3] = { gfx::Rgba(224, 68, 62), gfx::Rgba(222, 161, 35), gfx::Rgba(26, 171, 41) };
const D2D1_COLOR_F kInk[3] = { gfx::Rgba(77, 0, 0, 0.75f), gfx::Rgba(153, 87, 0, 0.85f), gfx::Rgba(0, 101, 0, 0.8f) };

bool Elevated(DWORD pid) {
    auto it = g_elev.find(pid);
    if (it != g_elev.end()) return it->second;
    bool elev = true;   // sem acesso ao token = processo de administrador
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        HANDLE tok = nullptr;
        if (OpenProcessToken(p, TOKEN_QUERY, &tok)) {
            TOKEN_ELEVATION te{};
            DWORD sz = 0;
            if (GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &sz)) elev = te.TokenIsElevated != 0;
            CloseHandle(tok);
        }
        CloseHandle(p);
    }
    if (g_elev.size() > 400) g_elev.clear();
    g_elev[pid] = elev;
    return elev;
}

bool ButtonRect(HWND h, RECT* out, float* scale) {
    RECT wr, b;
    if (!GetWindowRect(h, &wr)) return false;
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_CAPTION_BUTTON_BOUNDS, &b, sizeof(b)))) return false;
    float s = GetDpiForWindow(h) / 96.f;
    if (s <= 0) s = 1;
    int bw = b.right - b.left, bh = b.bottom - b.top;
    if (bw < 60 * s || bh < 14 * s || bh > 70 * s) return false;
    *out = RECT{ wr.left + b.left, wr.top + b.top, wr.left + b.right, wr.top + b.bottom };
    *scale = s;
    return true;
}

bool Eligible(HWND h) {
    if (!apps::IsTaskWindow(h) || IsIconic(h)) return false;
    LONG st = GetWindowLongW(h, GWL_STYLE);
    if ((st & WS_CAPTION) != WS_CAPTION || !(st & WS_SYSMENU)) return false;
    RECT r;
    GetWindowRect(h, &r);
    if (r.right - r.left < 260 || r.bottom - r.top < 120) return false;
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    if (r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom) return false;
    const auto& w = apps::Describe(h);
    if (Elevated(w.pid)) return false;
    std::wstring exe = PathFindFileNameW(w.exe.c_str());
    for (auto& e : g_exclude)
        if (!e.empty() && _wcsicmp(e.c_str(), exe.c_str()) == 0) return false;
    return true;
}

COLORREF Sample(int x, int y) {
    HDC dc = GetDC(nullptr);
    COLORREF c = GetPixel(dc, x, y);
    ReleaseDC(nullptr, dc);
    return c == CLR_INVALID ? RGB(255, 255, 255) : c;
}

float Lum(COLORREF c) { return (0.299f * GetRValue(c) + 0.587f * GetGValue(c) + 0.114f * GetBValue(c)) / 255.f; }

void Geometry(float w, float h, float s, float* d, float* gap, float* x0, float* cy) {
    *d = std::min(12.5f * s, h * 0.5f);
    *gap = *d * 1.62f;
    *cy = h / 2;
    *x0 = w - 14 * s - *d / 2 - 2 * *gap;   // centro do botão vermelho
}

void Draw(ID2D1DeviceContext* c, float w, float h, float s, COLORREF bg, bool active, bool hover, int zone, int press) {
    c->FillRectangle(D2D1::RectF(0, 0, w, h), gfx::Brush(c, gfx::Rgba(GetRValue(bg), GetGValue(bg), GetBValue(bg))).Get());
    float d, gap, x0, cy;
    Geometry(w, h, s, &d, &gap, &x0, &cy);
    bool darkBg = Lum(bg) < 0.45f;
    for (int i = 0; i < 3; ++i) {
        float cx = x0 + i * gap;
        bool lit = active || hover;
        D2D1_COLOR_F f = lit ? kFill[i] : (darkBg ? gfx::Rgba(86, 86, 90) : gfx::Rgba(206, 206, 210));
        if (press == i && zone == i) { f.r *= 0.82f; f.g *= 0.82f; f.b *= 0.82f; }
        gfx::Circle(c, cx, cy, d / 2, f);
        c->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), d / 2 - 0.4f, d / 2 - 0.4f), gfx::Brush(c, lit ? kEdge[i] : gfx::Black(darkBg ? 0.3f : 0.12f)).Get(), 0.8f);
        if (hover) {
            float k = d * 0.22f;
            if (i == 0) {
                gfx::Line(c, cx - k, cy - k, cx + k, cy + k, kInk[0], 1.3f * s);
                gfx::Line(c, cx - k, cy + k, cx + k, cy - k, kInk[0], 1.3f * s);
            } else if (i == 1) {
                gfx::Line(c, cx - k * 1.2f, cy, cx + k * 1.2f, cy, kInk[1], 1.4f * s);
            } else {
                gfx::Line(c, cx - k * 1.2f, cy, cx + k * 1.2f, cy, kInk[2], 1.3f * s);
                gfx::Line(c, cx, cy - k * 1.2f, cx, cy + k * 1.2f, kInk[2], 1.3f * s);
            }
        }
    }
}

void Paint(Ov& o) {
    int w = o.btn.right - o.btn.left, h = o.btn.bottom - o.btn.top;
    if (auto* c = o.surf.Begin(w, h)) {
        Draw(c, (float)w, (float)h, o.s, o.bg, o.active, o.hover, o.zone, o.press);
        o.surf.End(o.btn.left, o.btn.top);
    }
}

int ZoneAt(const Ov& o, int x, int y) {
    float w = (float)(o.btn.right - o.btn.left), h = (float)(o.btn.bottom - o.btn.top);
    float d, gap, x0, cy;
    Geometry(w, h, o.s, &d, &gap, &x0, &cy);
    (void)cy;
    if (y < 0 || y >= h) return -1;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(x - (x0 + i * gap)) <= gap / 2) return i;
    return -1;
}

void Restack(Ov& o) {
    HWND prev = GetWindow(o.target, GW_HWNDPREV);
    if (prev == o.wnd) return;
    bool topT = (GetWindowLongW(o.target, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    HWND after;
    if (topT) after = HWND_TOPMOST;
    else if (!prev || (GetWindowLongW(prev, GWL_EXSTYLE) & WS_EX_TOPMOST)) after = HWND_TOP;
    else after = prev;
    SetWindowPos(o.wnd, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Hide(Ov& o) {
    if (o.shown) ShowWindow(o.wnd, SW_HIDE);
    o.shown = false;
}

// Atualiza posição/cor de uma camada. resample = ler a cor da barra de título de novo.
void Update(Ov& o, bool resample) {
    RECT r;
    float s;
    if (!Eligible(o.target) || !ButtonRect(o.target, &r, &s)) { Hide(o); return; }
    bool moved = memcmp(&r, &o.btn, sizeof(RECT)) != 0;
    o.btn = r;
    o.s = s;
    if (resample || moved || !o.shown) {
        COLORREF c = Sample(r.left - (int)(2 * s), r.top + (int)(4 * s));
        if (c != o.bg) { o.bg = c; moved = true; }
    }
    if (moved || !o.shown) Paint(o);
    if (!o.shown) {
        ShowWindow(o.wnd, SW_SHOWNOACTIVATE);
        o.shown = true;
    }
    Restack(o);
}

LRESULT CALLBACK OvProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Ov* o = (Ov*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!o) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        if (!o->tracking) {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            o->tracking = true;
        }
        int z = ZoneAt(*o, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (!o->hover || z != o->zone) { o->hover = true; o->zone = z; Paint(*o); }
        return 0;
    }
    case WM_MOUSELEAVE:
        o->tracking = false;
        o->hover = false;
        o->zone = -1;
        Paint(*o);
        return 0;
    case WM_LBUTTONDOWN:
        o->press = ZoneAt(*o, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        SetCapture(h);
        Paint(*o);
        return 0;
    case WM_LBUTTONUP: {
        if (GetCapture() == h) ReleaseCapture();
        int z = ZoneAt(*o, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        int p = o->press;
        o->press = -1;
        HWND t = o->target;
        if (z >= 0 && z == p) {
            WPARAM cmd = z == 0 ? SC_CLOSE : z == 1 ? SC_MINIMIZE : (IsZoomed(t) ? SC_RESTORE : SC_MAXIMIZE);
            PostMessageW(t, WM_SYSCOMMAND, cmd, 0);
        }
        if (IsWindow(h)) Paint(*o);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        if (ZoneAt(*o, GET_X_LPARAM(l), GET_Y_LPARAM(l)) < 0) PostMessageW(o->target, WM_SYSCOMMAND, IsZoomed(o->target) ? SC_RESTORE : SC_MAXIMIZE, 0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

Ov* Ensure(HWND target) {
    auto it = g_ovs.find(target);
    if (it != g_ovs.end()) return it->second.get();
    auto o = std::make_unique<Ov>();
    o->target = target;
    o->wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kCls, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_inst, nullptr);
    if (!o->wnd) return nullptr;
    SetWindowLongPtrW(o->wnd, GWLP_USERDATA, (LONG_PTR)o.get());
    o->surf.Attach(o->wnd);
    Ov* raw = o.get();
    g_ovs[target] = std::move(o);
    return raw;
}

void Remove(HWND target) {
    auto it = g_ovs.find(target);
    if (it == g_ovs.end()) return;
    it->second->surf.Reset();
    DestroyWindow(it->second->wnd);
    g_ovs.erase(it);
}

void Sync(bool resample) {
    if (!g_on) return;
    HWND fg = apps::ForegroundApp();
    std::set<HWND> keep;
    for (HWND h : apps::TaskWindows()) {
        if (!Eligible(h)) continue;
        RECT r;
        float s;
        if (!ButtonRect(h, &r, &s)) continue;
        Ov* o = Ensure(h);
        if (!o) continue;
        bool act = h == fg;
        if (act != o->active) { o->active = act; o->btn = RECT{}; }
        Update(*o, resample);
        keep.insert(h);
    }
    std::vector<HWND> gone;
    for (auto& kv : g_ovs)
        if (!keep.count(kv.first)) gone.push_back(kv.first);
    for (HWND h : gone) Remove(h);
}

void Schedule(int ms) {
    if (g_timer) SetTimer(g_timer, T_SYNC, ms, nullptr);
}

void CALLBACK OnEvent(HWINEVENTHOOK, DWORD ev, HWND h, LONG obj, LONG child, DWORD, DWORD) {
    if (obj != OBJID_WINDOW || child != CHILDID_SELF || !h) return;
    if (ev == EVENT_OBJECT_LOCATIONCHANGE) {
        auto it = g_ovs.find(h);
        if (it != g_ovs.end()) Update(*it->second, false);
        else if (g_on && apps::IsTaskWindow(h)) Schedule(120);   // ex.: saiu da tela cheia
        return;
    }
    if (IsOwnWindow(h)) return;
    Schedule(ev == EVENT_SYSTEM_FOREGROUND ? 70 : 60);
}

LRESULT CALLBACK TimerProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER) {
        if (w == T_SYNC) { KillTimer(h, T_SYNC); Sync(true); }
        else if (w == T_PERIODIC) Sync(true);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}  // namespace

void Init() {
    if (!g_timer) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = OvProc;
        wc.hInstance = g_inst;
        wc.lpszClassName = kCls;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.style = CS_DBLCLKS;
        RegisterClassW(&wc);
        WNDCLASSW tc{};
        tc.lpfnWndProc = TimerProc;
        tc.hInstance = g_inst;
        tc.lpszClassName = kTimerCls;
        RegisterClassW(&tc);
        g_timer = CreateWindowExW(0, kTimerCls, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_inst, nullptr);
    }
    Reload();
}

void Reload() {
    bool on = cfg::Int(L"Windows", L"TrafficLights", 1) != 0;
    g_exclude = cfg::List(L"Windows", L"TrafficExclude");
    if (on && !g_on) {
        g_on = true;
        DWORD fl = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        g_hooks[0] = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnEvent, 0, 0, fl);
        g_hooks[1] = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, OnEvent, 0, 0, fl);
        g_hooks[2] = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, OnEvent, 0, 0, fl);
        g_hooks[3] = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, OnEvent, 0, 0, fl);
        g_hooks[4] = SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, OnEvent, 0, 0, fl);
        g_hooks[5] = SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND, nullptr, OnEvent, 0, 0, fl);
        SetTimer(g_timer, T_PERIODIC, 1500, nullptr);
        Schedule(200);
    } else if (!on && g_on) {
        Shutdown();
    } else if (on) {
        Schedule(50);
    }
}

void Shutdown() {
    for (auto& hk : g_hooks)
        if (hk) { UnhookWinEvent(hk); hk = nullptr; }
    if (g_timer) KillTimer(g_timer, T_PERIODIC);
    std::vector<HWND> all;
    for (auto& kv : g_ovs) all.push_back(kv.first);
    for (HWND h : all) Remove(h);
    g_on = false;
}

void RenderPreview(const std::wstring& png) {
    float s = 2.f;
    int w = (int)(520 * s), h = (int)(130 * s);
    auto b = gfx::Offscreen(w, h, [&](ID2D1DeviceContext* c) {
        auto bar = [&](float y, COLORREF bg, D2D1_COLOR_F text, bool active, bool hover) {
            c->FillRectangle(D2D1::RectF(0, y, (float)w, y + 31 * s), gfx::Brush(c, gfx::Rgba(GetRValue(bg), GetGValue(bg), GetBValue(bg))).Get());
            gfx::Text(c, L"Sem título - Bloco de Notas", gfx::Font(12 * s), D2D1::RectF(14 * s, y, 400 * s, y + 31 * s), text);
            D2D1_MATRIX_3X2_F old;
            c->GetTransform(&old);
            c->SetTransform(D2D1::Matrix3x2F::Translation(w - 138 * s, y) * old);
            Draw(c, 138 * s, 31 * s, s, bg, active, hover, -1, -1);
            c->SetTransform(old);
        };
        c->Clear(gfx::Rgba(240, 240, 244));
        bar(8 * s, RGB(255, 255, 255), gfx::Black(0.85f), true, false);
        bar(48 * s, RGB(32, 32, 36), gfx::White(0.9f), true, true);
        bar(88 * s, RGB(243, 243, 243), gfx::Black(0.45f), false, false);
    });
    gfx::SaveImage(b.Get(), png);
}
}  // namespace traffic
