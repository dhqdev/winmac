#include "switcher.h"
#include "gfx.h"
#include "apps.h"
#include "config.h"
#include "art.h"

namespace switcher {
namespace {
enum { SW_START = 1, SW_NEXT, SW_PREV, SW_CANCEL, SW_COMMIT, SW_CLOSESEL };
enum { T_SHOW = 1 };
const wchar_t* kCls = L"WinMac.Switcher";

struct Entry {
    HWND hwnd = nullptr;
    std::wstring title, app;
    ComPtr<IWICBitmapSource> icon;
    HTHUMBNAIL thumb = nullptr;
    bool iconic = false;
    D2D1_RECT_F card{}, area{};
};

HHOOK g_hook = nullptr;
HWND g_wnd = nullptr;
Surface g_surf;
std::vector<Entry> g_items;
int g_sel = 0;
bool g_switching = false, g_shown = false, g_eatAltUp = false, g_previews = true;
float g_s = 1;
int g_x = 0, g_y = 0, g_w = 0, g_h = 0;
std::map<std::wstring, ComPtr<IWICBitmapSource>> g_icons;

ComPtr<IWICBitmapSource> IconFor(HWND h) {
    const auto& w = apps::Describe(h);
    std::wstring key = w.aumid.empty() ? w.exe : w.aumid;
    auto it = g_icons.find(key);
    if (it != g_icons.end()) return it->second;
    auto ic = apps::AppIcon(w.aumid, w.exe, h, 128);
    if (g_icons.size() > 120) g_icons.clear();
    g_icons[key] = ic;
    return ic;
}

std::wstring AppName(HWND h) {
    const auto& w = apps::Describe(h);
    std::wstring n;
    if (w.exe.find(L"\\explorer.exe") != std::wstring::npos) return L"Explorador de Arquivos";
    if (!w.aumid.empty()) n = apps::DisplayName(w.aumid);
    if (n.empty() && !w.exe.empty()) n = apps::FileDescription(w.exe);
    return n;
}

void Inject(std::initializer_list<std::pair<WORD, bool>> keys) {
    std::vector<INPUT> in;
    for (auto& k : keys) {
        INPUT i{};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = k.first;
        i.ki.dwFlags = k.second ? KEYEVENTF_KEYUP : 0;
        in.push_back(i);
    }
    SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}

void Unthumb() {
    for (auto& e : g_items)
        if (e.thumb) { DwmUnregisterThumbnail(e.thumb); e.thumb = nullptr; }
}

void Layout() {
    MONITORINFO mi{ sizeof(mi) };
    POINT cur;
    GetCursorPos(&cur);
    GetMonitorInfoW(MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY), &mi);
    float s = g_s = gfx::Scale();
    int n = (int)g_items.size();
    float pad = 18 * s;
    float cw, ch;
    if (g_previews) { cw = 214 * s; ch = 170 * s; }
    else { cw = 108 * s; ch = 108 * s; }
    float maxW = (mi.rcMonitor.right - mi.rcMonitor.left) * 0.9f;
    int per = std::max(1, std::min(n, (int)((maxW - 2 * pad) / cw)));
    int rows = (n + per - 1) / per;
    rows = std::min(rows, g_previews ? 3 : 4);
    float extra = g_previews ? 0 : 40 * s;   // título embaixo no modo só ícones
    g_w = (int)std::ceil(per * cw + 2 * pad);
    g_h = (int)std::ceil(rows * ch + 2 * pad + extra);
    g_x = mi.rcMonitor.left + ((mi.rcMonitor.right - mi.rcMonitor.left) - g_w) / 2;
    g_y = mi.rcMonitor.top + ((mi.rcMonitor.bottom - mi.rcMonitor.top) - g_h) / 2;
    for (int i = 0; i < n; ++i) {
        auto& e = g_items[i];
        int r = i / per, c = i % per;
        if (r >= rows) { e.card = D2D1::RectF(-1000, -1000, -999, -999); continue; }
        int inRow = std::min(per, n - r * per);
        float rowOff = (per - inRow) * cw / 2;
        float x0 = pad + rowOff + c * cw, y0 = pad + r * ch;
        e.card = D2D1::RectF(x0 + 4 * s, y0 + 4 * s, x0 + cw - 4 * s, y0 + ch - 4 * s);
        if (g_previews) e.area = D2D1::RectF(e.card.left + 10 * s, e.card.top + 10 * s, e.card.right - 10 * s, e.card.bottom - 40 * s);
        else e.area = D2D1::RectF(e.card.left + 12 * s, e.card.top + 12 * s, e.card.right - 12 * s, e.card.bottom - 12 * s);
    }
}

void Paint(ID2D1DeviceContext* c, Surface* surf, bool forFile) {
    float s = g_s;
    bool dark = gfx::DarkMode();
    auto rr = gfx::RR(0, 0, (float)g_w, (float)g_h, 22 * s);
    gfx::Fill(c, rr, gfx::Panel(dark, 0.42f, 0.55f));
    gfx::GlassOverlay(c, rr, dark, 0.9f);
    D2D1_COLOR_F fg = dark ? gfx::White(0.95f) : gfx::Black(0.85f), sub = dark ? gfx::White(0.6f) : gfx::Black(0.5f);
    for (int i = 0; i < (int)g_items.size(); ++i) {
        auto& e = g_items[i];
        if (e.card.left < 0) continue;
        bool sel = i == g_sel;
        auto cr = gfx::RR(e.card.left, e.card.top, e.card.right, e.card.bottom, 14 * s);
        if (sel) {
            gfx::Fill(c, cr, dark ? gfx::White(0.16f) : gfx::White(0.55f));
            gfx::Stroke(c, cr, gfx::Accent(), 2.f * s);
        }
        auto draw = [&](IWICBitmapSource* b, D2D1_RECT_F r, float op) {
            if (!b) return;
            if (surf) surf->DrawBitmap(b, r, op);
            else {
                ComPtr<ID2D1Bitmap> bm;
                if (SUCCEEDED(c->CreateBitmapFromWicBitmap(b, nullptr, &bm))) c->DrawBitmap(bm.Get(), r, op, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
            }
        };
        if (g_previews) {
            auto ar = gfx::RR(e.area.left, e.area.top, e.area.right, e.area.bottom, 8 * s);
            gfx::Fill(c, ar, dark ? gfx::White(0.05f) : gfx::Black(0.04f));
            if (!e.thumb || forFile) {
                // janela minimizada (ou prévia indisponível): ícone grande no lugar da miniatura
                float isz = std::min(64 * s, (e.area.bottom - e.area.top) * 0.7f);
                float cx = (e.area.left + e.area.right) / 2, cy = (e.area.top + e.area.bottom) / 2;
                draw(e.icon.Get(), D2D1::RectF(cx - isz / 2, cy - isz / 2, cx + isz / 2, cy + isz / 2), e.iconic ? 0.75f : 1.f);
            }
            float iy = e.card.bottom - 34 * s;
            draw(e.icon.Get(), D2D1::RectF(e.card.left + 10 * s, iy + 4 * s, e.card.left + 32 * s, iy + 26 * s), 1.f);
            std::wstring t = e.title.empty() ? e.app : e.title;
            gfx::Text(c, t, gfx::Font(12.5f * s, sel ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_MEDIUM), D2D1::RectF(e.card.left + 38 * s, iy, e.card.right - 10 * s, iy + 30 * s), fg);
        } else {
            draw(e.icon.Get(), e.area, 1.f);
        }
    }
    if (!g_previews && g_sel >= 0 && g_sel < (int)g_items.size()) {
        auto& e = g_items[g_sel];
        std::wstring t = e.app.empty() ? e.title : e.app + (e.title.empty() || e.title == e.app ? L"" : L" — " + e.title);
        gfx::Text(c, t, gfx::Font(14 * s, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(20 * s, g_h - 44 * s, g_w - 20 * s, g_h - 12 * s), fg, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    (void)sub;
}

void Render() {
    if (!g_wnd) return;
    auto* c = g_surf.Begin(g_w, g_h);
    if (!c) return;
    Paint(c, &g_surf, false);
    g_surf.End(g_x, g_y);
}

void PlaceThumbs() {
    for (auto& e : g_items) {
        if (!e.thumb) continue;
        SIZE src{};
        DwmQueryThumbnailSourceSize(e.thumb, &src);
        float aw = e.area.right - e.area.left, ah = e.area.bottom - e.area.top;
        float sc = src.cx > 0 && src.cy > 0 ? std::min(aw / src.cx, ah / src.cy) : 1.f;
        float tw = src.cx * sc, thh = src.cy * sc;
        float l = e.area.left + (aw - tw) / 2, t = e.area.top + (ah - thh) / 2;
        DWM_THUMBNAIL_PROPERTIES p{};
        p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        p.rcDestination = RECT{ (LONG)l, (LONG)t, (LONG)(l + tw), (LONG)(t + thh) };
        p.fVisible = e.card.left >= 0;
        p.opacity = 255;
        p.fSourceClientAreaOnly = FALSE;
        DwmUpdateThumbnailProperties(e.thumb, &p);
    }
}

void Show() {
    if (g_shown || g_items.empty()) return;
    g_shown = true;
    Layout();
    Render();
    gfx::SetRoundRegion(g_wnd, 0, 0, g_w, g_h, (int)(22 * g_s));
    SetWindowPos(g_wnd, HWND_TOPMOST, g_x, g_y, g_w, g_h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (g_previews) {
        for (auto& e : g_items)
            if (!e.iconic && e.card.left >= 0 && SUCCEEDED(DwmRegisterThumbnail(g_wnd, e.hwnd, &e.thumb))) {}
        PlaceThumbs();
        Render();
    }
}

void Hide() {
    KillTimer(g_wnd, T_SHOW);
    Unthumb();
    if (g_shown) ShowWindow(g_wnd, SW_HIDE);
    g_shown = false;
    g_switching = false;
    g_items.clear();
}

void Start(bool back) {
    g_items.clear();
    for (HWND h : apps::TaskWindows()) {
        Entry e;
        e.hwnd = h;
        e.iconic = IsIconic(h) != FALSE;
        e.title = apps::Title(h);
        e.app = AppName(h);
        e.icon = IconFor(h);
        g_items.push_back(std::move(e));
        if (g_items.size() >= 24) break;
    }
    if (g_items.empty()) return;
    g_switching = true;
    g_eatAltUp = true;
    int n = (int)g_items.size();
    g_sel = n == 1 ? 0 : (back ? n - 1 : 1);
    g_previews = cfg::Int(L"Windows", L"SwitcherPreviews", 1) != 0;
    SetTimer(g_wnd, T_SHOW, 130, nullptr);
}

void Move(int d) {
    int n = (int)g_items.size();
    if (!n) return;
    g_sel = (g_sel + d + n) % n;
    if (!g_shown) Show();
    else Render();
}

void Commit() {
    HWND target = (g_sel >= 0 && g_sel < (int)g_items.size()) ? g_items[g_sel].hwnd : nullptr;
    Hide();
    // uma tecla "neutra" impede que soltar o Alt ative o menu do app
    Inject({ { 0xE8, false }, { 0xE8, true } });
    if (target && IsWindow(target)) apps::Activate(target);
    Inject({ { VK_MENU, true } });
    g_eatAltUp = false;
}

LRESULT CALLBACK LLProc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) return CallNextHookEx(g_hook, code, wp, lp);
    auto* k = (KBDLLHOOKSTRUCT*)lp;
    if (k->flags & LLKHF_INJECTED) return CallNextHookEx(g_hook, code, wp, lp);
    bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
    bool up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
    bool alt = (k->flags & LLKHF_ALTDOWN) != 0;
    DWORD vk = k->vkCode;
    if (vk == VK_TAB && down && alt && GetAsyncKeyState(VK_CONTROL) >= 0 && GetAsyncKeyState(VK_LWIN) >= 0 && GetAsyncKeyState(VK_RWIN) >= 0) {
        bool shift = GetAsyncKeyState(VK_SHIFT) < 0;
        PostMessageW(g_wnd, WM_MD_SWITCH, g_switching ? (shift ? SW_PREV : SW_NEXT) : SW_START, shift);
        return 1;
    }
    if (g_switching && down) {
        if (vk == VK_ESCAPE) { PostMessageW(g_wnd, WM_MD_SWITCH, SW_CANCEL, 0); return 1; }
        if (vk == VK_LEFT || vk == VK_UP) { PostMessageW(g_wnd, WM_MD_SWITCH, SW_PREV, 0); return 1; }
        if (vk == VK_RIGHT || vk == VK_DOWN) { PostMessageW(g_wnd, WM_MD_SWITCH, SW_NEXT, 0); return 1; }
        if (vk == 'W' || vk == 'Q') { PostMessageW(g_wnd, WM_MD_SWITCH, SW_CLOSESEL, 0); return 1; }
        if (vk == VK_RETURN) { PostMessageW(g_wnd, WM_MD_SWITCH, SW_COMMIT, 0); return 1; }
    }
    if (up && (vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU) && g_eatAltUp) {
        PostMessageW(g_wnd, WM_MD_SWITCH, g_switching ? SW_COMMIT : SW_CANCEL, 1);
        return 1;
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MD_SWITCH:
        switch (w) {
        case SW_START: Start(l != 0); break;
        case SW_NEXT: Move(1); break;
        case SW_PREV: Move(-1); break;
        case SW_COMMIT: if (g_switching) Commit(); break;
        case SW_CLOSESEL:
            if (g_sel >= 0 && g_sel < (int)g_items.size()) {
                apps::Close(g_items[g_sel].hwnd);
                if (g_items[g_sel].thumb) DwmUnregisterThumbnail(g_items[g_sel].thumb);
                g_items.erase(g_items.begin() + g_sel);
                if (g_items.empty()) { Hide(); break; }
                g_sel = std::min(g_sel, (int)g_items.size() - 1);
                if (g_shown) { Unthumb(); g_shown = false; ShowWindow(g_wnd, SW_HIDE); Show(); }
            }
            break;
        case SW_CANCEL:
            Hide();
            if (l) {   // Alt foi solto depois de cancelar: solta sem ativar o menu do app
                Inject({ { 0xE8, false }, { 0xE8, true }, { VK_MENU, true } });
                g_eatAltUp = false;
            }
            break;
        }
        return 0;
    case WM_TIMER:
        if (w == T_SHOW) { KillTimer(h, T_SHOW); if (g_switching) Show(); }
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_LBUTTONUP: {
        float x = (float)GET_X_LPARAM(l), y = (float)GET_Y_LPARAM(l);
        for (int i = 0; i < (int)g_items.size(); ++i) {
            auto& r = g_items[i].card;
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) { g_sel = i; Commit(); break; }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        float x = (float)GET_X_LPARAM(l), y = (float)GET_Y_LPARAM(l);
        for (int i = 0; i < (int)g_items.size(); ++i) {
            auto& r = g_items[i].card;
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom && g_sel != i) { g_sel = i; Render(); break; }
        }
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}
}  // namespace

void Init() {
    if (!g_wnd) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = g_inst;
        wc.lpszClassName = kCls;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kCls, L"WinMac Switcher", WS_POPUP, 0, 0, 1, 1,
                                nullptr, nullptr, g_inst, nullptr);
        g_surf.Attach(g_wnd);
        gfx::SetBlur(g_wnd, true);
    }
    Reload();
}

void Reload() {
    bool on = cfg::Int(L"Windows", L"Switcher", 1) != 0;
    if (on && !g_hook) g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LLProc, g_inst, 0);
    else if (!on && g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
}

void Shutdown() {
    if (g_hook) UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
    if (g_wnd) { Hide(); g_surf.Reset(); DestroyWindow(g_wnd); }
    g_wnd = nullptr;
}

void DebugShow(bool show) {
    if (show) { Start(false); KillTimer(g_wnd, T_SHOW); Show(); }
    else Hide();
}

void RenderPreview(const std::wstring& png) {
    g_items.clear();
    struct Demo { const wchar_t* title; const wchar_t* app; const wchar_t* id; };
    Demo demo[] = { { L"Planilha de gastos.xlsx", L"Excel", L"Microsoft.Office.EXCEL.EXE.15" },
                    { L"Apresentação.pptx", L"PowerPoint", L"Microsoft.Office.POWERPNT.EXE.15" },
                    { L"Downloads", L"Explorador de Arquivos", L"Microsoft.Windows.Explorer" },
                    { L"Calculadora", L"Calculadora", L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App" },
                    { L"Ajustes do Sistema", L"Ajustes", L"" } };
    for (auto& d : demo) {
        Entry e;
        e.title = d.title;
        e.app = d.app;
        if (*d.id) e.icon = apps::AppIcon(d.id, L"", nullptr, 128);
        if (!e.icon) e.icon = gfx::Offscreen(128, 128, [](ID2D1DeviceContext* c) { art::SettingsIcon(c, D2D1::RectF(4, 4, 124, 124)); });
        g_items.push_back(e);
    }
    g_sel = 1;
    g_previews = true;
    Layout();
    g_x = g_y = 0;
    auto b = gfx::Offscreen(g_w, g_h, [](ID2D1DeviceContext* c) { Paint(c, nullptr, true); });
    gfx::SaveImage(b.Get(), png);
    g_items.clear();
}
}  // namespace switcher
