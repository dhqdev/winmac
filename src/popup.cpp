#include "popup.h"
#include "gfx.h"

namespace {
struct MenuState {
    HWND hwnd = nullptr;
    std::vector<PMItem> items;
    std::vector<D2D1_RECT_F> rects;
    int hover = -1;
    int result = 0;
    bool done = false;
    float s = 1;
    int x = 0, y = 0, w = 0, h = 0;
    float scW = 0;
    int fade = 0;
    DWORD start = 0;
    float ax = 0, ay = 0;   // ponto de onde o menu "nasce"
    Surface surf;
    std::function<bool(POINT)> outside;
};
MenuState* g_menu = nullptr;
const wchar_t* kCls = L"WinMac.Popup";
const int kFadeSteps = 6;

float PopT(MenuState& m) { return Clamp01((GetTickCount() - m.start) / 160.f); }

void Render(MenuState& m) {
    auto* c = m.surf.Begin(m.w, m.h);
    if (!c) return;
    bool dark = gfx::DarkMode();
    float s = m.s;
    float t = PopT(m), u = t - 1.f;
    float e = 1.f + 2.2f * u * u * u + 1.2f * u * u;   // mola leve
    float sc = 0.93f + 0.07f * e;
    c->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, D2D1::Point2F(m.ax, m.ay)));
    auto rr = gfx::RR(0, 0, (float)m.w, (float)m.h, 11 * s);
    gfx::Fill(c, rr, gfx::Panel(dark, 0.72f, 0.80f));
    gfx::GlassOverlay(c, rr, dark, 0.55f);
    auto* f = gfx::Font(13.5f * s);
    auto* fb = gfx::Font(13.5f * s, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    D2D1_COLOR_F fg = dark ? gfx::White(0.93f) : gfx::Black(0.86f);
    for (size_t i = 0; i < m.items.size(); ++i) {
        const auto& it = m.items[i];
        auto r = m.rects[i];
        if (it.sep) {
            float yy = std::floor((r.top + r.bottom) / 2) + 0.5f;
            gfx::Line(c, r.left + 12 * s, yy, r.right - 12 * s, yy, dark ? gfx::White(0.13f) : gfx::Black(0.11f), 1.f, false);
            continue;
        }
        bool hl = (int)i == m.hover && it.enabled;
        if (hl) gfx::Fill(c, gfx::RR(r.left + 5 * s, r.top, r.right - 5 * s, r.bottom, 6 * s), gfx::Accent());
        D2D1_COLOR_F col = hl ? gfx::White(1) : fg;
        if (!it.enabled) col.a *= 0.38f;
        float tx = r.left + 24 * s;
        if (it.checked) gfx::Text(c, L"✓", f, D2D1::RectF(r.left + 10 * s, r.top, tx, r.bottom), col);
        gfx::Text(c, it.text, it.bold ? fb : f, D2D1::RectF(tx, r.top, r.right - 16 * s - m.scW, r.bottom), col);
        if (!it.shortcut.empty()) {
            D2D1_COLOR_F sc = hl ? gfx::White(0.85f) : (dark ? gfx::White(0.42f) : gfx::Black(0.40f));
            gfx::Text(c, it.shortcut, f, D2D1::RectF(tx, r.top, r.right - 16 * s, r.bottom), sc, DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    }
    c->SetTransform(D2D1::Matrix3x2F::Identity());
    int a = (int)(255 * Clamp01(PopT(m) * 2.6f));
    m.surf.End(m.x, m.y, (BYTE)a);
}

int HitTest(MenuState& m, POINT p) {
    for (size_t i = 0; i < m.rects.size(); ++i) {
        auto& r = m.rects[i];
        if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) return (m.items[i].sep || !m.items[i].enabled) ? -1 : (int)i;
    }
    return -1;
}

void MoveHover(MenuState& m, int dir) {
    int n = (int)m.items.size();
    int i = m.hover < 0 ? (dir > 0 ? -1 : n) : m.hover;   // sem seleção: começa do primeiro (ou do último)
    for (int k = 0; k < n; ++k) {
        i = (i + dir + n) % n;
        if (!m.items[i].sep && m.items[i].enabled) { m.hover = i; break; }
    }
    Render(m);
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    MenuState* m = g_menu;
    if (!m || m->hwnd != h) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hit = HitTest(*m, p);
        bool inside = p.x >= 0 && p.y >= 0 && p.x < m->w && p.y < m->h;
        if (hit != m->hover && (inside || hit >= 0)) { m->hover = hit; Render(*m); }
        if (!inside && m->hover >= 0) { m->hover = -1; Render(*m); }
        if (!inside && m->outside) {
            POINT sp = p;
            ClientToScreen(h, &sp);
            if (m->outside(sp)) { m->result = kMenuSwitch; m->done = true; }
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (p.x < 0 || p.y < 0 || p.x >= m->w || p.y >= m->h) { m->result = 0; m->done = true; }
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hit = HitTest(*m, p);
        if (hit >= 0) { m->result = m->items[hit].id; m->done = true; }
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case VK_ESCAPE: m->result = 0; m->done = true; break;
        case VK_UP: MoveHover(*m, -1); break;
        case VK_DOWN: MoveHover(*m, +1); break;
        case VK_LEFT: m->result = kMenuLeft; m->done = true; break;
        case VK_RIGHT: m->result = kMenuRight; m->done = true; break;
        case VK_RETURN:
        case VK_SPACE:
            if (m->hover >= 0) { m->result = m->items[m->hover].id; m->done = true; }
            break;
        }
        return 0;
    case WM_CAPTURECHANGED:
        if ((HWND)lp != h) m->done = true;
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) m->done = true;
        return 0;
    case WM_TIMER:
        Render(*m);
        if (PopT(*m) >= 1.f) KillTimer(h, 1);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
}  // namespace

bool PopupMenuOpen() { return g_menu != nullptr; }

int PopupMenu(const std::vector<PMItem>& items, int x, int y, std::function<bool(POINT)> onOutsideMove, bool anchorBottom, int minWidthPx) {
    if (g_menu || items.empty()) return 0;
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = g_inst;
        wc.lpszClassName = kCls;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        reg = true;
    }
    MenuState m;
    m.items = items;
    m.outside = onOutsideMove;
    m.s = gfx::Scale();
    float s = m.s;
    auto* f = gfx::Font(13.5f * s);
    auto* fb = gfx::Font(13.5f * s, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    float textW = 0, scW = 0;
    for (auto& it : items) {
        if (it.sep) continue;
        textW = std::max(textW, gfx::Measure(it.text, it.bold ? fb : f).width);
        if (!it.shortcut.empty()) scW = std::max(scW, gfx::Measure(it.shortcut, f).width);
    }
    m.scW = scW > 0 ? scW + 24 * s : 0;
    float itemH = 24 * s, sepH = 11 * s, pad = 6 * s;
    m.w = (int)std::ceil(std::max((float)minWidthPx, std::max(200 * s, 24 * s + textW + m.scW + 22 * s)));
    float yy = pad;
    for (auto& it : items) {
        float hh = it.sep ? sepH : itemH;
        m.rects.push_back(D2D1::RectF(0, yy, (float)m.w, yy + hh));
        yy += hh;
    }
    m.h = (int)std::ceil(yy + pad);
    if (anchorBottom) {  // abre para cima, centralizado em x (menus do dock)
        y -= m.h;
        x -= m.w / 2;
    }
    HMONITOR mon = MonitorFromPoint(POINT{ x, y }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    m.x = std::max((int)mi.rcMonitor.left + 4, std::min(x, (int)mi.rcMonitor.right - m.w - 4));
    m.y = std::max((int)mi.rcMonitor.top + 2, std::min(y, (int)mi.rcMonitor.bottom - m.h - 4));

    m.start = GetTickCount();
    m.ax = anchorBottom ? m.w / 2.f : 12 * s;
    m.ay = anchorBottom ? (float)m.h : 0.f;
    m.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kCls, L"", WS_POPUP, m.x, m.y, m.w, m.h,
                             nullptr, nullptr, g_inst, nullptr);
    if (!m.hwnd) return 0;
    g_menu = &m;
    m.surf.Attach(m.hwnd);
    gfx::SetBlur(m.hwnd, true);
    gfx::SetRoundRegion(m.hwnd, 0, 0, m.w, m.h, (int)(11 * s));
    Render(m);
    ShowWindow(m.hwnd, SW_SHOW);
    SetForegroundWindow(m.hwnd);
    SetFocus(m.hwnd);
    SetCapture(m.hwnd);
    SetTimer(m.hwnd, 1, 10, nullptr);
    MSG msg;
    while (!m.done) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) msg.hwnd = m.hwnd;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (GetCapture() == m.hwnd) ReleaseCapture();
    g_menu = nullptr;
    m.surf.Reset();
    DestroyWindow(m.hwnd);
    return m.result;
}
