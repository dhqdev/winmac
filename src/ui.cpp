#include "ui.h"

namespace ui {
namespace {
const wchar_t* kCls = L"WinMac.UiWindow";
const wchar_t* kGlassCls = L"WinMac.UiGlass";
enum { T_FRAME = 1, T_CARET = 2 };
const int kCtrlFlag = 0x10000;

bool In(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
}  // namespace

Theme Current() {
    Theme t{};
    t.dark = gfx::DarkMode();
    t.accent = gfx::Accent();
    if (t.dark) {
        t.text = gfx::White(0.93f);
        t.sub = gfx::White(0.56f);
        t.faint = gfx::White(0.34f);
        t.sep = gfx::White(0.08f);
        t.panel = gfx::Panel(true, 0.62f, 0);
        t.group = gfx::White(0.055f);
        t.groupStroke = gfx::White(0.07f);
        t.field = gfx::White(0.08f);
        t.hover = gfx::White(0.08f);
        t.sidebar = gfx::Black(0.16f);
    } else {
        t.text = gfx::Black(0.86f);
        t.sub = gfx::Black(0.50f);
        t.faint = gfx::Black(0.30f);
        t.sep = gfx::Black(0.08f);
        t.panel = gfx::Panel(false, 0, 0.74f);
        t.group = gfx::White(0.62f);
        t.groupStroke = gfx::Black(0.06f);
        t.field = gfx::White(0.92f);
        t.hover = gfx::Black(0.05f);
        t.sidebar = gfx::Black(0.035f);
    }
    return t;
}

Window::~Window() {
    if (hwnd_) DestroyWindow(hwnd_);
}

bool Window::Create(const std::wstring& title, float lw, float lh, bool appWindow, bool topmost) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = g_inst;
        wc.lpszClassName = kCls;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(1));
        wc.style = CS_DBLCLKS;
        RegisterClassW(&wc);
        WNDCLASSW gc{};
        gc.lpfnWndProc = DefWindowProcW;
        gc.hInstance = g_inst;
        gc.lpszClassName = kGlassCls;
        RegisterClassW(&gc);
        reg = true;
    }
    s = gfx::Scale();
    pad = (int)std::round(30 * s);
    W = (int)std::round(lw * s);
    H = (int)std::round(lh * s);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    auto& wa = mi.rcWork;
    x_ = wa.left + ((wa.right - wa.left) - (W + 2 * pad)) / 2;
    y_ = wa.top + std::max(0, (int)((wa.bottom - wa.top) - (H + 2 * pad)) / 2 - (int)(20 * s));
    DWORD tm = topmost ? WS_EX_TOPMOST : 0;
    glass_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | tm, kGlassCls, L"", WS_POPUP,
                             x_ + pad, y_ + pad, W, H, nullptr, nullptr, g_inst, nullptr);
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | (appWindow ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW) | tm, kCls, title.c_str(),
                            WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX, x_, y_, W + 2 * pad, H + 2 * pad, glass_, nullptr, g_inst, nullptr);
    if (!hwnd_ || !glass_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);
    surf_.Attach(hwnd_);
    glassSurf_.Attach(glass_);
    gfx::SetBlur(glass_, true);
    th = Current();
    if (auto* g = glassSurf_.Begin(W, H)) {
        gfx::Fill(g, gfx::RR(0, 0, (float)W, (float)H, radius * s), th.dark ? gfx::Rgba(28, 28, 32, 0.12f) : gfx::Rgba(255, 255, 255, 0.14f));
        glassSurf_.End(x_ + pad, y_ + pad);
    }
    gfx::SetRoundRegion(glass_, 0, 0, W, H, (int)(radius * s));
    return true;
}

void Window::Resize(float lw, float lh) {
    int nw = (int)std::round(lw * s), nh = (int)std::round(lh * s);
    if (nw == W && nh == H) return;
    x_ += (W - nw) / 2;
    W = nw;
    H = nh;
    SetWindowPos(glass_, nullptr, x_ + pad, y_ + pad, W, H, SWP_NOZORDER | SWP_NOACTIVATE);
    if (auto* g = glassSurf_.Begin(W, H)) {
        gfx::Fill(g, gfx::RR(0, 0, (float)W, (float)H, radius * s), th.dark ? gfx::Rgba(28, 28, 32, 0.12f) : gfx::Rgba(255, 255, 255, 0.14f));
        glassSurf_.End(x_ + pad, y_ + pad);
    }
    gfx::SetRoundRegion(glass_, 0, 0, W, H, (int)(radius * s));
    Repaint();
}

void Window::SetPanelPos(int x, int y) {
    if (hwnd_) Place(x - pad, y - pad);
    else { x_ = x - pad; y_ = y - pad; }
}

bool Window::SaveFrame(const std::wstring& png) {
    Repaint();
    Repaint();   // segundo quadro: controles já sabem onde está o mouse e as alturas
    return surf_.SavePng(png);
}

void Window::Show() {
    openStart_ = GetTickCount();
    Repaint();
    ShowWindow(glass_, SW_SHOWNOACTIVATE);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void Window::Close() {
    if (!hwnd_ || closing_) return;
    closing_ = true;
    closeStart_ = GetTickCount();
    EnableWindow(hwnd_, FALSE);
    Repaint();
}

void Window::CloseNow() {
    if (hwnd_) DestroyWindow(hwnd_);
}

// curva de mola suave (passa um tiquinho do alvo e volta), como as animações do macOS
static float Spring(float t) {
    t = Clamp01(t);
    const float c1 = 1.25f, c3 = c1 + 1.f;
    float u = t - 1.f;
    return 1.f + c3 * u * u * u + c1 * u * u;
}

void Window::Invalidate() {
    if (!hwnd_ || pending_) return;
    pending_ = true;
    PostMessageW(hwnd_, WM_PAINT + WM_APP, 0, 0);
}

void Window::Place(int x, int y) {
    x_ = x;
    y_ = y;
    HDWP d = BeginDeferWindowPos(2);
    d = DeferWindowPos(d, glass_, nullptr, x + pad, y + pad, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    d = DeferWindowPos(d, hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(d);
}

void Window::UpdateHot() {
    hot_ = 0;
    for (auto it = lastHits_.rbegin(); it != lastHits_.rend(); ++it)
        if (In(it->second, mx_, my_)) { hot_ = it->first; break; }
}

D2D1_RECT_F Window::Clip(D2D1_RECT_F r) const {
    r.top += dy_;
    r.bottom += dy_;
    return D2D1::RectF(std::max(r.left, clip_.left), std::max(r.top, clip_.top), std::min(r.right, clip_.right), std::min(r.bottom, clip_.bottom));
}

void Window::Repaint() {
    if (!hwnd_ || minimized_) return;
    pending_ = false;
    DWORD now = GetTickCount();
    dt_ = lastFrame_ ? std::min(0.05f, (now - lastFrame_) / 1000.f) : 0.016f;
    lastFrame_ = now;
    th = Current();
    auto* ctx = surf_.Begin(W + 2 * pad, H + 2 * pad);
    if (!ctx) return;
    c = ctx;
    hits_.clear();
    again_ = false;
    clip_ = D2D1::RectF(-1e6f, -1e6f, 1e6f, 1e6f);
    dy_ = 0;
    int focusBefore = focus_;
    // abrir: cresce de 94% com mola e aparece; fechar: encolhe para 96% e some
    float scale = 1.f, alpha = 1.f;
    if (closing_) {
        float t = Clamp01((now - closeStart_) / 170.f);
        float e = t * t;
        scale = 1.f - 0.04f * e;
        alpha = 1.f - e;
        if (t >= 1.f) {
            surf_.End(x_, y_, 0);
            c = nullptr;
            CloseNow();
            return;
        }
        again_ = true;
    } else if (openStart_) {
        float t = Clamp01((now - openStart_) / 380.f);
        scale = 0.94f + 0.06f * Spring(t);
        alpha = Clamp01(t * 3.f);
        if (t < 1.f) again_ = true;
        else openStart_ = 0;
    }
    D2D1_MATRIX_3X2_F base = D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(W / 2.f, H / 2.f)) * D2D1::Matrix3x2F::Translation((float)pad, (float)pad);
    c->SetTransform(base);
    auto rr = gfx::RR(0, 0, (float)W, (float)H, radius * s);
    // fundo (sombra + vidro) desenhado uma vez e reaproveitado em todos os quadros
    wchar_t key[96];
    swprintf_s(key, L"%d|%d|%d|%d|%.2f|%d", W, H, (int)th.dark, (int)(th.panel.a * 100), gfx::GlassStrength(), (int)gfx::ReduceTransparency());
    if (!bg_ || bgKey_ != key) {
        bgKey_ = key;
        int bw = W + 2 * pad, bh = H + 2 * pad;
        Theme tt = th;
        float sc = s, rad = radius;
        int pd = pad, ww = W, hh = H;
        bg_ = gfx::Offscreen(bw, bh, [&](ID2D1DeviceContext* g) {
            g->SetTransform(D2D1::Matrix3x2F::Translation((float)pd, (float)pd));
            auto r2 = gfx::RR(0, 0, (float)ww, (float)hh, rad * sc);
            gfx::Shadow(g, r2, 24 * sc, tt.dark ? 0.55f : 0.30f, 9 * sc);
            gfx::Fill(g, r2, tt.panel);
            gfx::GlassOverlay(g, r2, tt.dark, 0.5f);
        });
    }
    if (bg_) {
        c->SetTransform(D2D1::Matrix3x2F::Translation((float)-pad, (float)-pad) * base);
        surf_.DrawBitmap(bg_.Get(), D2D1::RectF(0, 0, (float)(W + 2 * pad), (float)(H + 2 * pad)), 1.f, false);
        c->SetTransform(base);
    }
    ComPtr<ID2D1RoundedRectangleGeometry> clipGeo;
    gfx::d2d->CreateRoundedRectangleGeometry(rr, &clipGeo);
    c->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clipGeo.Get()), nullptr);   // nada desenha fora do painel
    Paint();
    c->SetTransform(base);
    c->PopLayer();
    c->SetTransform(base);
    gfx::Stroke(c, gfx::RR(0.5f, 0.5f, W - 0.5f, H - 0.5f, radius * s), th.dark ? gfx::White(0.10f) : gfx::White(0.5f), 1.f);
    c->SetTransform(D2D1::Matrix3x2F::Identity());
    BYTE a8 = (BYTE)(255 * Clamp01(alpha));
    surf_.End(x_, y_, a8);
    if (alpha < 1.f || openStart_ || closing_) glassSurf_.SetAlpha(x_ + pad, y_ + pad, a8);
    c = nullptr;
    // um clique fora do campo de texto em foco tira o foco
    if (pressed_ && focus_ == focusBefore && focus_ != 0 && hot_ != focus_) focus_ = 0;
    if (released_) active_ = 0;
    pressed_ = released_ = dbl_ = false;
    wheel_ = 0;
    typed_.clear();
    keys_.clear();
    lastHits_.swap(hits_);
    int oldHot = hot_;
    UpdateHot();
    if (oldHot != hot_) again_ = true;
    if (focus_) SetTimer(hwnd_, T_CARET, 530, nullptr);
    else KillTimer(hwnd_, T_CARET);
    if (again_) SetTimer(hwnd_, T_FRAME, 15, nullptr);
}

// ---------------- controles ----------------
bool Window::Hover(int id, D2D1_RECT_F r) {
    D2D1_RECT_F cr = Clip(r);
    if (cr.right > cr.left && cr.bottom > cr.top) hits_.push_back({ id, cr });
    return hot_ == id;
}

bool Window::Clicked(int id, D2D1_RECT_F r) {
    bool h = Hover(id, r);
    if (pressed_ && h) active_ = id;
    bool fire = released_ && active_ == id && h;
    if (fire) again_ = true;
    return fire;
}

float Window::Anim(int id, float target, float dur) {
    auto it = anim_.find(id);
    if (it == anim_.end()) { anim_[id] = target; return target; }
    float& v = it->second;
    if (std::fabs(v - target) < 0.002f) { v = target; return v; }
    v += (target - v) * (1.f - std::exp(-dt_ / std::max(0.01f, dur * 0.45f)));
    again_ = true;
    return v;
}

void Window::Label(const std::wstring& t, float px, D2D1_RECT_F r, D2D1_COLOR_F col, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT a, DWRITE_PARAGRAPH_ALIGNMENT pa) {
    gfx::Text(c, t, gfx::Font(px * s, w), r, col, a, pa);
}

void Window::Paragraph(const std::wstring& t, float px, D2D1_RECT_F r, D2D1_COLOR_F col, DWRITE_TEXT_ALIGNMENT a) {
    if (t.empty()) return;
    ComPtr<IDWriteTextLayout> l;
    auto* f = gfx::Font(px * s);
    if (FAILED(gfx::dw->CreateTextLayout(t.c_str(), (UINT32)t.size(), f, std::max(1.f, r.right - r.left), std::max(1.f, r.bottom - r.top), &l))) return;
    l->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    l->SetTextAlignment(a);
    c->DrawTextLayout(D2D1::Point2F(r.left, r.top), l.Get(), gfx::Brush(c, col).Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

float Window::ParagraphHeight(const std::wstring& t, float px, float width) {
    if (t.empty()) return 0;
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(gfx::dw->CreateTextLayout(t.c_str(), (UINT32)t.size(), gfx::Font(px * s), std::max(1.f, width), 100000.f, &l))) return 0;
    l->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return m.height;
}

bool Window::Button(int id, D2D1_RECT_F r, const std::wstring& text, bool primary, bool enabled) {
    bool fire = enabled && Clicked(id, r);
    bool h = enabled && hot_ == id, p = h && active_ == id && down_;
    float rad = std::min(7 * s, (r.bottom - r.top) / 2);
    auto rr = gfx::RR(r.left, r.top, r.right, r.bottom, rad);
    D2D1_COLOR_F bg, fg;
    if (primary) {
        bg = th.accent;
        if (h) { bg.r = std::min(1.f, bg.r * 1.08f + 0.03f); bg.g = std::min(1.f, bg.g * 1.08f + 0.03f); bg.b = std::min(1.f, bg.b * 1.08f + 0.03f); }
        if (p) { bg.r *= 0.85f; bg.g *= 0.85f; bg.b *= 0.85f; }
        fg = gfx::White(1);
    } else {
        bg = th.dark ? gfx::White(h ? 0.20f : 0.14f) : gfx::White(h ? 1.f : 0.92f);
        if (p) bg = th.dark ? gfx::White(0.26f) : gfx::Rgba(228, 228, 232);
        fg = th.text;
    }
    if (!enabled) { bg.a *= 0.5f; fg.a *= 0.45f; }
    if (!primary && !th.dark) gfx::Shadow(c, rr, 3 * s, 0.10f, 1 * s);
    gfx::Fill(c, rr, bg);
    if (primary) {
        auto hi = gfx::Gradient(c, D2D1::Point2F(0, r.top), D2D1::Point2F(0, r.bottom), { { 0.f, gfx::White(0.22f) }, { 1.f, gfx::White(0) } });
        c->FillRoundedRectangle(rr, hi.Get());
    } else gfx::Stroke(c, rr, th.dark ? gfx::White(0.10f) : gfx::Black(0.10f), 1.f);
    Label(text, 13, r, fg, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
    return fire;
}

bool Window::LinkButton(int id, D2D1_RECT_F r, const std::wstring& text) {
    bool fire = Clicked(id, r);
    D2D1_COLOR_F col = th.accent;
    if (hot_ == id) col.a = 0.8f;
    Label(text, 13, r, col, DWRITE_FONT_WEIGHT_MEDIUM);
    return fire;
}

bool Window::Toggle(int id, float right, float cy, bool& v) {
    float w = 38 * s, h = 22 * s;
    D2D1_RECT_F r = D2D1::RectF(right - w, cy - h / 2, right, cy + h / 2);
    bool fire = Clicked(id, D2D1::RectF(r.left - 4 * s, r.top - 4 * s, r.right + 4 * s, r.bottom + 4 * s));
    if (fire) v = !v;
    float k = Anim(id, v ? 1.f : 0.f, 0.16f);
    D2D1_COLOR_F off = th.dark ? gfx::White(0.18f) : gfx::Black(0.12f), on = th.accent;
    D2D1_COLOR_F col{ Lerp(off.r, on.r, k), Lerp(off.g, on.g, k), Lerp(off.b, on.b, k), Lerp(off.a, on.a, k) };
    auto rr = gfx::RR(r.left, r.top, r.right, r.bottom, h / 2);
    gfx::Fill(c, rr, col);
    float kr = h / 2 - 2 * s;
    float kx = Lerp(r.left + h / 2, r.right - h / 2, k);
    gfx::Circle(c, kx, cy + 0.8f * s, kr + 0.6f * s, gfx::Black(0.12f));
    gfx::Circle(c, kx, cy, kr, gfx::White(1));
    return fire;
}

bool Window::Slider(int id, D2D1_RECT_F r, float& v) {
    float cy = (r.top + r.bottom) / 2, kr = 9 * s;
    bool h = Hover(id, D2D1::RectF(r.left - kr, cy - kr - 4 * s, r.right + kr, cy + kr + 4 * s));
    if (pressed_ && h) active_ = id;
    bool changed = false;
    if (active_ == id && (down_ || released_)) {
        float nv = Clamp01((mx_ - r.left) / std::max(1.f, r.right - r.left));
        if (std::fabs(nv - v) > 0.0005f) { v = nv; changed = true; }
    }
    float th2 = 4 * s;
    gfx::Fill(c, gfx::RR(r.left, cy - th2 / 2, r.right, cy + th2 / 2, th2 / 2), th.dark ? gfx::White(0.16f) : gfx::Black(0.10f));
    float fx = r.left + (r.right - r.left) * Clamp01(v);
    gfx::Fill(c, gfx::RR(r.left, cy - th2 / 2, fx, cy + th2 / 2, th2 / 2), th.accent);
    gfx::Circle(c, fx, cy + 1 * s, kr + 1 * s, gfx::Black(0.14f));
    gfx::Circle(c, fx, cy, kr, gfx::White(1));
    gfx::Stroke(c, gfx::RR(fx - kr, cy - kr, fx + kr, cy + kr, kr), gfx::Black(0.08f), 1.f);
    return changed;
}

bool Window::Segmented(int id, D2D1_RECT_F r, const std::vector<std::wstring>& opts, int& sel) {
    int n = (int)opts.size();
    if (!n) return false;
    float rad = 7 * s;
    gfx::Fill(c, gfx::RR(r.left, r.top, r.right, r.bottom, rad), th.dark ? gfx::White(0.09f) : gfx::Black(0.06f));
    float w = (r.right - r.left) / n;
    float k = Anim(id, (float)std::clamp(sel, 0, n - 1), 0.14f);
    auto knob = gfx::RR(r.left + k * w + 2 * s, r.top + 2 * s, r.left + (k + 1) * w - 2 * s, r.bottom - 2 * s, rad - 2 * s);
    gfx::Shadow(c, knob, 3 * s, 0.12f, 1 * s);
    gfx::Fill(c, knob, th.dark ? gfx::White(0.24f) : gfx::White(1));
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        D2D1_RECT_F cr = D2D1::RectF(r.left + i * w, r.top, r.left + (i + 1) * w, r.bottom);
        if (Clicked(id * 100 + i + 1, cr) && sel != i) { sel = i; changed = true; }
        Label(opts[i], 12.5f, cr, th.text, i == sel ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    return changed;
}

bool Window::TextField(int id, D2D1_RECT_F r, std::wstring& v, const std::wstring& placeholder, bool password, bool* enter) {
    if (Clicked(id, r)) focus_ = id;
    bool focused = focus_ == id;
    bool changed = false;
    if (enter) *enter = false;
    if (focused) {
        for (wchar_t ch : typed_) {
            if (ch == 8) { if (!v.empty()) { v.pop_back(); changed = true; } }
            else if (ch >= 32 && v.size() < 256) { v += ch; changed = true; }
        }
        for (UINT k : keys_) {
            if (k == (UINT)('V' | kCtrlFlag) && OpenClipboard(hwnd_)) {
                if (HANDLE hd = GetClipboardData(CF_UNICODETEXT)) {
                    if (auto* p = (const wchar_t*)GlobalLock(hd)) {
                        for (const wchar_t* q = p; *q && v.size() < 256; ++q) if (*q >= 32) v += *q;
                        GlobalUnlock(hd);
                        changed = true;
                    }
                }
                CloseClipboard();
            }
            if (k == VK_RETURN && enter) *enter = true;
        }
    }
    float rad = 7 * s;
    auto rr = gfx::RR(r.left, r.top, r.right, r.bottom, rad);
    gfx::Fill(c, rr, th.field);
    gfx::Stroke(c, rr, focused ? th.accent : (th.dark ? gfx::White(0.12f) : gfx::Black(0.12f)), focused ? 2.f * s : 1.f);
    std::wstring shown = password ? std::wstring(v.size(), L'•') : v;
    auto* f = gfx::Font(13.5f * s);
    float tx = r.left + 10 * s, avail = r.right - r.left - 20 * s;
    float tw = gfx::Measure(shown, f).width;
    float off = std::max(0.f, tw - avail);
    c->PushAxisAlignedClip(D2D1::RectF(r.left + 4 * s, r.top, r.right - 4 * s, r.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
    if (shown.empty() && !focused) gfx::Text(c, placeholder, f, D2D1::RectF(tx, r.top, r.right - 8 * s, r.bottom), th.faint);
    else gfx::Text(c, shown, f, D2D1::RectF(tx - off, r.top, tx - off + tw + 40 * s, r.bottom), th.text, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    if (focused && ((GetTickCount() - caretTick_) / 530) % 2 == 0) {
        float cx = tx - off + tw + 1 * s;
        gfx::Line(c, cx, r.top + 7 * s, cx, r.bottom - 7 * s, th.accent, 1.6f * s, false);
    }
    c->PopAxisAlignedClip();
    return changed;
}

bool Window::Row(int id, D2D1_RECT_F r, float rad) {
    bool fire = Clicked(id, r);
    if (hot_ == id) gfx::Fill(c, gfx::RR(r.left, r.top, r.right, r.bottom, rad * s), active_ == id && down_ ? (th.dark ? gfx::White(0.12f) : gfx::Black(0.08f)) : th.hover);
    return fire;
}

void Window::Spinner(float cx, float cy, float r, D2D1_COLOR_F col) {
    float t = (GetTickCount() % 1000) / 1000.f;
    for (int i = 0; i < 8; ++i) {
        float a = (i / 8.f) * 6.2831853f;
        int phase = (i - (int)(t * 8) + 8) % 8;
        D2D1_COLOR_F cc = col;
        cc.a *= 0.25f + 0.75f * (phase / 7.f);
        gfx::Line(c, cx + std::cos(a) * r * 0.45f, cy + std::sin(a) * r * 0.45f, cx + std::cos(a) * r, cy + std::sin(a) * r, cc, std::max(1.5f, r * 0.22f));
    }
    again_ = true;
}

void Window::Progress(D2D1_RECT_F r, float frac) {
    float h = r.bottom - r.top;
    gfx::Fill(c, gfx::RR(r.left, r.top, r.right, r.bottom, h / 2), th.dark ? gfx::White(0.12f) : gfx::Black(0.08f));
    float fx = r.left + (r.right - r.left) * Clamp01(frac);
    if (fx > r.left + 1) gfx::Fill(c, gfx::RR(r.left, r.top, std::max(fx, r.left + h), r.bottom, h / 2), th.accent);
}

void Window::TrafficLights(float x, float cy, bool canZoom) {
    float d = 12.5f * s, gap = 20 * s;
    D2D1_RECT_F group = D2D1::RectF(x - 4 * s, cy - d, x + gap * 2 + d + 4 * s, cy + d);
    bool over = Hover(-9000, group);
    static const D2D1_COLOR_F fill[3] = { gfx::Rgba(255, 95, 87), gfx::Rgba(254, 188, 46), gfx::Rgba(40, 200, 64) };
    static const D2D1_COLOR_F edge[3] = { gfx::Rgba(224, 68, 62), gfx::Rgba(222, 161, 35), gfx::Rgba(26, 171, 41) };
    static const D2D1_COLOR_F ink[3] = { gfx::Rgba(77, 0, 0, 0.75f), gfx::Rgba(153, 87, 0, 0.85f), gfx::Rgba(0, 101, 0, 0.8f) };
    for (int i = 0; i < 3; ++i) {
        float cx = x + d / 2 + i * gap;
        D2D1_RECT_F hr = D2D1::RectF(cx - gap / 2, cy - d, cx + gap / 2, cy + d);
        bool enabled = i != 2 || canZoom;
        bool fire = enabled && Clicked(-9001 - i, hr);
        bool lit = activeWin_ || over;
        D2D1_COLOR_F f = lit && enabled ? fill[i] : (th.dark ? gfx::Rgba(80, 80, 84) : gfx::Rgba(208, 208, 212));
        gfx::Circle(c, cx, cy, d / 2, f);
        c->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), d / 2 - 0.4f, d / 2 - 0.4f), gfx::Brush(c, lit && enabled ? edge[i] : gfx::Black(0.12f)).Get(), 0.8f);
        if (over && enabled) {
            float k = d * 0.22f;
            D2D1_COLOR_F ic = ink[i];
            if (i == 0) {
                gfx::Line(c, cx - k, cy - k, cx + k, cy + k, ic, 1.3f * s);
                gfx::Line(c, cx - k, cy + k, cx + k, cy - k, ic, 1.3f * s);
            } else if (i == 1) {
                gfx::Line(c, cx - k * 1.2f, cy, cx + k * 1.2f, cy, ic, 1.4f * s);
            } else {
                gfx::Line(c, cx - k * 1.2f, cy, cx + k * 1.2f, cy, ic, 1.3f * s);
                gfx::Line(c, cx, cy - k * 1.2f, cx, cy + k * 1.2f, ic, 1.3f * s);
            }
        }
        if (fire) {
            if (i == 0) OnCloseRequest();
            else if (i == 1) ShowWindow(hwnd_, SW_MINIMIZE);
        }
    }
}

void Window::BeginScroll(D2D1_RECT_F view, float& offset, float contentH) {
    float maxOff = std::max(0.f, contentH - (view.bottom - view.top));
    D2D1_RECT_F vv = Clip(view);
    if (wheel_ != 0 && In(vv, mx_, my_)) {
        offset -= wheel_ * 70 * s;
        wheel_ = 0;
    }
    offset = std::clamp(offset, 0.f, maxOff);
    ScrollCtx sc{ view, offset, {}, clip_, dy_ };
    c->GetTransform(&sc.old);
    scroll_.push_back(sc);
    c->PushAxisAlignedClip(view, D2D1_ANTIALIAS_MODE_ALIASED);
    clip_ = D2D1::RectF(std::max(vv.left, clip_.left), std::max(vv.top, clip_.top), std::min(vv.right, clip_.right), std::min(vv.bottom, clip_.bottom));
    dy_ -= offset;
    c->SetTransform(D2D1::Matrix3x2F::Translation(0, -offset) * sc.old);
    if (maxOff > 0) {
        // indicador de rolagem fininho
        float vh = view.bottom - view.top;
        float bh = std::max(30 * s, vh * vh / contentH);
        float by = view.top + (vh - bh) * (offset / maxOff) + offset;
        gfx::Fill(c, gfx::RR(view.right - 6 * s, by + 2 * s, view.right - 2.5f * s, by + bh - 2 * s, 2 * s), th.dark ? gfx::White(0.22f) : gfx::Black(0.18f));
    }
}

void Window::EndScroll() {
    if (scroll_.empty()) return;
    auto sc = scroll_.back();
    scroll_.pop_back();
    c->PopAxisAlignedClip();
    c->SetTransform(sc.old);
    clip_ = sc.oldClip;
    dy_ = sc.oldDy;
}

// ---------------- mensagens ----------------
LRESULT CALLBACK Window::Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* self = (Window*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (self && self->hwnd_ == h) return self->OnMsg(m, w, l);
    return DefWindowProcW(h, m, w, l);
}

LRESULT Window::OnMsg(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT + WM_APP:
        Repaint();
        return 0;
    case WM_MD_ASYNC: {
        auto* fn = (std::function<void()>*)l;
        if (fn) { (*fn)(); delete fn; }
        return 0;
    }
    case WM_TIMER:
        if (w == T_FRAME) { KillTimer(hwnd_, T_FRAME); Repaint(); }
        else if (w == T_CARET) Invalidate();
        else OnTimer(w);
        return 0;
    case WM_MOUSEMOVE: {
        if (!tracking_) {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd_, 0 };
            TrackMouseEvent(&t);
            tracking_ = true;
        }
        if (dragging_) {
            POINT p;
            GetCursorPos(&p);
            int nx = dragX_ + p.x - dragPt_.x, ny = dragY_ + p.y - dragPt_.y;
            MONITORINFO mi{ sizeof(mi) };
            GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi);
            ny = std::max(ny, (int)mi.rcWork.top - pad);
            Place(nx, ny);
            return 0;
        }
        mx_ = (float)GET_X_LPARAM(l) - pad;
        my_ = (float)GET_Y_LPARAM(l) - pad;
        int old = hot_;
        UpdateHot();
        if (old != hot_ || active_ != 0) Invalidate();
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        if (!dragging_ && !down_) {
            mx_ = my_ = -1e6f;
            UpdateHot();
            Invalidate();
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        mx_ = (float)GET_X_LPARAM(l) - pad;
        my_ = (float)GET_Y_LPARAM(l) - pad;
        UpdateHot();
        SetCapture(hwnd_);
        down_ = true;
        if (hot_ == 0 && my_ >= 0 && my_ < dragH_ && mx_ >= 0 && mx_ < W) {
            dragging_ = true;
            GetCursorPos(&dragPt_);
            dragX_ = x_;
            dragY_ = y_;
            return 0;
        }
        pressed_ = true;
        dbl_ = m == WM_LBUTTONDBLCLK;
        caretTick_ = GetTickCount();
        Repaint();
        return 0;
    }
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd_) ReleaseCapture();
        down_ = false;
        if (dragging_) { dragging_ = false; return 0; }
        mx_ = (float)GET_X_LPARAM(l) - pad;
        my_ = (float)GET_Y_LPARAM(l) - pad;
        released_ = true;
        Repaint();
        return 0;
    case WM_CAPTURECHANGED:
        dragging_ = false;
        return 0;
    case WM_MOUSEWHEEL:
        wheel_ += GET_WHEEL_DELTA_WPARAM(w) / 120.f;
        {
            POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            ScreenToClient(hwnd_, &p);
            mx_ = (float)p.x - pad;
            my_ = (float)p.y - pad;
        }
        Invalidate();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            bool text = false;
            for (auto& hh : lastHits_)
                if (hh.first == hot_ && hot_ >= 7000 && hot_ < 7100) text = true;
            SetCursor(LoadCursor(nullptr, text ? IDC_IBEAM : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_CHAR:
        if (focus_) {
            typed_ += (wchar_t)w;
            caretTick_ = GetTickCount();
            Invalidate();
        }
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (OnKey((UINT)w)) return 0;
        keys_.push_back((UINT)w | (GetKeyState(VK_CONTROL) < 0 ? kCtrlFlag : 0));
        if (w == VK_ESCAPE && focus_) focus_ = 0;
        Invalidate();
        if (m == WM_SYSKEYDOWN) break;
        return 0;
    case WM_ACTIVATE:
        activeWin_ = LOWORD(w) != WA_INACTIVE;
        Invalidate();
        OnActiveChanged(activeWin_);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_SIZE:
        if (w == SIZE_MINIMIZED) {
            minimized_ = true;
            ShowWindow(glass_, SW_HIDE);
        } else if (minimized_) {
            minimized_ = false;
            SetWindowPos(hwnd_, nullptr, x_, y_, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(glass_, SW_SHOWNOACTIVATE);
            Place(x_, y_);
            Repaint();
        }
        return 0;
    case WM_CLOSE:
        OnCloseRequest();
        return 0;
    case WM_DESTROY: {
        HWND g = glass_;
        glass_ = nullptr;
        hwnd_ = nullptr;
        surf_.Reset();
        glassSurf_.Reset();
        if (g) DestroyWindow(g);
        OnClosed();
        return 0;
    }
    }
    if (m >= WM_APP && OnCustom(m, w, l)) return 0;
    return DefWindowProcW(hwnd_, m, w, l);
}
}  // namespace ui
