#include "menubar.h"
#include "apps.h"
#include "config.h"
#include "popup.h"
#include "sys.h"
#include "tray.h"
#include "art.h"
#include "ui.h"
#include "wifiui.h"
#include "sysx.h"
#include <tlhelp32.h>

MenuBar g_menubar;

namespace {
const wchar_t* kCls = L"WinMac.MenuBar";
const wchar_t* kCcCls = L"WinMac.ControlCenter";
enum { T_CLOCK = 1, T_STATUS = 2, T_TRAY = 3 };
const wchar_t* kMenus[] = { L"Arquivo", L"Editar", L"Visualizar", L"Janela", L"Ajuda" };

void CALLBACK FgProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) { g_menubar.OnForeground(); }

PMItem It(const std::wstring& t, int id, const std::wstring& sc = L"", bool en = true, bool chk = false) {
    PMItem i;
    i.text = t;
    i.id = id;
    i.shortcut = sc;
    i.enabled = en;
    i.checked = chk;
    return i;
}

std::wstring KeyOf(HWND h) {
    const auto& w = apps::Describe(h);
    return Lower(w.aumid.empty() ? w.exe : w.aumid);
}

std::vector<HWND> WindowsOf(HWND h) {
    std::vector<HWND> r;
    if (!h) return r;
    std::wstring k = KeyOf(h);
    for (HWND x : apps::TaskWindows())
        if (KeyOf(x) == k) r.push_back(x);
    return r;
}

void DrawWifi(ID2D1DeviceContext* c, float cx, float cy, float s, int quality, bool on, D2D1_COLOR_F col) {
    // três arcos + ponto, estilo macOS
    for (int i = 0; i < 3; ++i) {
        float r = (3.6f + i * 3.4f) * s;
        float a0 = -3.14159f * 0.78f, a1 = -3.14159f * 0.22f;
        ComPtr<ID2D1PathGeometry> g;
        gfx::d2d->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> sk;
        g->Open(&sk);
        sk->BeginFigure(D2D1::Point2F(cx + r * std::cos(a0), cy + r * std::sin(a0)), D2D1_FIGURE_BEGIN_HOLLOW);
        sk->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + r * std::cos(a1), cy + r * std::sin(a1)), D2D1::SizeF(r, r), 0,
                                    D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sk->EndFigure(D2D1_FIGURE_END_OPEN);
        sk->Close();
        bool lit = on && quality >= (i == 0 ? 0 : i == 1 ? 34 : 67);
        D2D1_COLOR_F cc = col;
        if (!lit) cc.a *= 0.3f;
        static ComPtr<ID2D1StrokeStyle> rs;
        if (!rs) gfx::d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0, &rs);
        c->DrawGeometry(g.Get(), gfx::Brush(c, cc).Get(), 1.9f * s, rs.Get());
    }
    D2D1_COLOR_F dc = col;
    if (!on) dc.a *= 0.3f;
    gfx::Circle(c, cx, cy - 0.2f * s, 1.5f * s, dc);
}

void DrawBattery(ID2D1DeviceContext* c, float l, float cy, float s, int pct, bool charging, D2D1_COLOR_F col) {
    float w = 22 * s, h = 10.5f * s;
    auto body = gfx::RR(l, cy - h / 2, l + w, cy + h / 2, 3 * s);
    D2D1_COLOR_F oc = col;
    oc.a *= 0.45f;
    gfx::Stroke(c, body, oc, 1.f * s);
    gfx::Fill(c, gfx::RR(l + w + 1 * s, cy - 2 * s, l + w + 2.6f * s, cy + 2 * s, 1 * s), oc);
    float fw = (w - 3 * s) * std::clamp(pct, 0, 100) / 100.f;
    D2D1_COLOR_F fc = pct <= 20 && !charging ? gfx::Rgba(255, 69, 58) : col;
    if (fw > 0.5f) gfx::Fill(c, gfx::RR(l + 1.5f * s, cy - h / 2 + 1.5f * s, l + 1.5f * s + fw, cy + h / 2 - 1.5f * s, 1.6f * s), fc);
    if (charging) {
        ComPtr<ID2D1PathGeometry> g;
        gfx::d2d->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> sk;
        g->Open(&sk);
        float x = l + w / 2;
        sk->BeginFigure(D2D1::Point2F(x + 1.2f * s, cy - 5.2f * s), D2D1_FIGURE_BEGIN_FILLED);
        sk->AddLine(D2D1::Point2F(x - 2.6f * s, cy + 0.6f * s));
        sk->AddLine(D2D1::Point2F(x - 0.2f * s, cy + 0.6f * s));
        sk->AddLine(D2D1::Point2F(x - 1.2f * s, cy + 5.2f * s));
        sk->AddLine(D2D1::Point2F(x + 2.6f * s, cy - 0.6f * s));
        sk->AddLine(D2D1::Point2F(x + 0.2f * s, cy - 0.6f * s));
        sk->EndFigure(D2D1_FIGURE_END_CLOSED);
        sk->Close();
        c->FillGeometry(g.Get(), gfx::Brush(c, gfx::DarkMode() ? gfx::Black(0.85f) : gfx::White(1)).Get());
    }
}

void DrawSearch(ID2D1DeviceContext* c, float cx, float cy, float s, D2D1_COLOR_F col) {
    c->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 1.2f * s, cy - 1.2f * s), 4.6f * s, 4.6f * s), gfx::Brush(c, col).Get(), 1.7f * s);
    gfx::Line(c, cx + 2.3f * s, cy + 2.3f * s, cx + 5.6f * s, cy + 5.6f * s, col, 1.9f * s);
}

void DrawControl(ID2D1DeviceContext* c, float cx, float cy, float s, D2D1_COLOR_F col) {
    float w = 15 * s, h = 5.4f * s;
    for (int i = 0; i < 2; ++i) {
        float y = cy + (i == 0 ? -3.6f : 3.6f) * s;
        auto r = gfx::RR(cx - w / 2, y - h / 2, cx + w / 2, y + h / 2, h / 2);
        gfx::Stroke(c, r, col, 1.2f * s);
        float kx = i == 0 ? cx + w / 2 - h / 2 : cx - w / 2 + h / 2;
        gfx::Circle(c, kx, y, h / 2 - 0.2f * s, col);
    }
}

void DrawLogo(ID2D1DeviceContext* c, float cx, float cy, float s, D2D1_COLOR_F col) {
    art::Logo(c, cx, cy, 16.5f * s, col);
}

bool ProcessRunning(const wchar_t* exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{ sizeof(pe) };
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) found = _wcsicmp(pe.szExeFile, exe) == 0;
    CloseHandle(snap);
    return found;
}

// Painel de Wi-Fi que abre embaixo do ícone da barra de menus.
class WifiPopup : public ui::Window {
public:
    WifiPanel panel;
    float contentH = 0;
    static WifiPopup* current;
    void Paint() override {
        float h = panel.Draw(*this, D2D1::RectF(16 * s, 6 * s, W - 16 * s, 4000.f), true) + 12 * s;
        if (std::fabs(h - contentH) > 1) {
            contentH = h;
            PostMessageW(Hwnd(), WM_APP + 41, 0, 0);
        }
    }
    bool OnCustom(UINT m, WPARAM, LPARAM) override {
        if (m == WM_APP + 41) {
            float lh = std::min(contentH / s, 560.f);
            int keepRight = 0;
            RECT r;
            GetWindowRect(Hwnd(), &r);
            keepRight = r.right - pad;
            Resize(320, lh);
            SetPanelPos(keepRight - W, r.top + pad);
            return true;
        }
        return false;
    }
    void OnTimer(UINT_PTR) override { if (panel.Refresh()) Invalidate(); }
    void OnActiveChanged(bool a) override { if (!a) PostMessageW(Hwnd(), WM_CLOSE, 0, 0); }
    bool OnKey(UINT vk) override {
        if (vk == VK_ESCAPE && FocusId() == 0) { Close(); return true; }
        return false;
    }
    void OnClosed() override {
        current = nullptr;
        delete this;
    }
};
WifiPopup* WifiPopup::current = nullptr;

void OpenWifiPopup(int rightX, int topY) {
    if (WifiPopup::current) { WifiPopup::current->Close(); return; }
    auto* p = new WifiPopup();
    p->radius = 12;
    if (!p->Create(L"Wi-Fi", 320, 240, false, true)) { delete p; return; }
    WifiPopup::current = p;
    p->SetPanelPos(rightX - p->W, topY);
    SetTimer(p->Hwnd(), 1001, 2500, nullptr);
    p->Show();
}
}  // namespace

bool MenuBar::Create() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = g_inst;
    wc.lpszClassName = kCls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.style = CS_DBLCLKS;
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kCls, L"WinMac Menu Bar", WS_POPUP,
                            0, 0, 1, 1, nullptr, nullptr, g_inst, nullptr);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);
    surf_.Attach(hwnd_);
    gfx::SetBlur(hwnd_, true);
    UpdateStatus();
    if (cfg::Int(L"MenuBar", L"ShowTray", 1)) tray::Refresh();
    Relayout();
    SetTimer(hwnd_, T_STATUS, 20000, nullptr);
    SetTimer(hwnd_, T_TRAY, 2000, nullptr);
    fgHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, FgProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    OnForeground();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    return true;
}

void MenuBar::Destroy() {
    if (fgHook_) UnhookWinEvent(fgHook_);
    AppBarRemove(hwnd_);
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

void MenuBar::Relayout() {
    s_ = gfx::Scale();
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    x_ = mi.rcMonitor.left;
    y_ = mi.rcMonitor.top;
    w_ = mi.rcMonitor.right - mi.rcMonitor.left;
    h_ = (int)std::round(std::clamp(cfg::Int(L"MenuBar", L"Height", 28), 20, 44) * s_);
    AppBarReserve(hwnd_, ABE_TOP, h_);
    Render();
}

void MenuBar::SetHidden(bool hidden) {
    if (hidden == hidden_) return;
    hidden_ = hidden;
    ShowWindow(hwnd_, hidden ? SW_HIDE : SW_SHOWNOACTIVATE);
    if (!hidden) Render();
}

std::wstring MenuBar::AppTitle(HWND h) {
    static std::map<std::wstring, std::wstring> cache;
    const auto& w = apps::Describe(h);
    std::wstring key = w.aumid.empty() ? w.exe : w.aumid;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    std::wstring n;
    if (w.exe.find(L"\\explorer.exe") != std::wstring::npos) n = L"Explorador";
    if (n.empty() && !w.aumid.empty()) n = apps::DisplayName(w.aumid);
    if (n.empty() && !w.exe.empty()) n = apps::FileDescription(w.exe);
    if (n.empty()) n = apps::Title(h);
    cache[key] = n;
    return n;
}

void MenuBar::OnForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg || IsOwnWindow(fg)) return;
    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd")) {
        target_ = nullptr;
        appName_ = L"WinMac";
    } else {
        HWND root = apps::ForegroundApp();
        if (!root || !apps::IsTaskWindow(root)) return;
        target_ = root;
        appName_ = AppTitle(root);
    }
    Render();
}

void MenuBar::UpdateStatus() {
    auto b = sys::GetBattery();
    batPresent_ = b.present;
    batPct_ = b.pct;
    batCharging_ = b.charging;
    auto w = sys::GetWifi();
    wifiOn_ = w.connected;
    wifiWireless_ = w.wireless;
    wifiQ_ = w.quality;
    ssid_ = w.ssid;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t d[64] = {}, t[32] = {};
    GetDateFormatEx(L"pt-BR", 0, &st, L"ddd d 'de' MMM", d, 64, nullptr);
    GetTimeFormatEx(L"pt-BR", TIME_NOSECONDS, &st, L"HH:mm", t, 32);
    std::wstring ds = d;
    if (!ds.empty()) ds[0] = towupper(ds[0]);
    clock_ = cfg::Int(L"MenuBar", L"ClockDate", 1) ? ds + L"   " + t : std::wstring(t);
    // próximo minuto cheio
    SetTimer(hwnd_, T_CLOCK, (60 - st.wSecond) * 1000 - st.wMilliseconds + 30, nullptr);
}

void MenuBar::Layout(ID2D1DeviceContext*) {
    elems_.clear();
    float s = s_, H = (float)h_;
    auto* f = gfx::Font(13.5f * s);
    auto* fb = gfx::Font(13.5f * s, DWRITE_FONT_WEIGHT_BOLD);
    float pad = 9 * s;
    float x = 10 * s;
    elems_.push_back({ Logo, 0, D2D1::RectF(x, 0, x + 34 * s, H), L"" });
    x += 34 * s;
    float w = gfx::Measure(appName_, fb).width + 2 * pad;
    w = std::min(w, 260 * s);
    elems_.push_back({ AppName, 0, D2D1::RectF(x, 0, x + w, H), appName_ });
    x += w;
    for (int i = 0; i < 5; ++i) {
        float mw = gfx::Measure(kMenus[i], f).width + 2 * pad;
        elems_.push_back({ Menu, i, D2D1::RectF(x, 0, x + mw, H), kMenus[i] });
        x += mw;
    }
    float leftEnd = x + 30 * s;
    float r = (float)w_ - 8 * s;
    float cw = gfx::Measure(clock_, f).width + 2 * pad;
    elems_.push_back({ Clock, 0, D2D1::RectF(r - cw, 0, r, H), clock_ });
    r -= cw;
    float iw = 30 * s;
    elems_.push_back({ Control, 0, D2D1::RectF(r - iw, 0, r, H), L"" });
    r -= iw;
    elems_.push_back({ Search, 0, D2D1::RectF(r - iw, 0, r, H), L"" });
    r -= iw;
    elems_.push_back({ Wifi, 0, D2D1::RectF(r - iw, 0, r, H), L"" });
    r -= iw;
    if (batPresent_) {
        std::wstring p = cfg::Int(L"MenuBar", L"BatteryPercent", 1) ? std::to_wstring(batPct_) + L"%" : L"";
        float bw = (p.empty() ? 0 : gfx::Measure(p, f).width) + 30 * s + 2 * pad;
        elems_.push_back({ Battery, 0, D2D1::RectF(r - bw, 0, r, H), p });
        r -= bw;
    }
    if (cfg::Int(L"MenuBar", L"ShowTray", 1)) {
        r -= 4 * s;
        const auto& icons = tray::Icons();
        float tw = 27 * s;
        for (int i = (int)icons.size() - 1; i >= 0; --i) {
            if (r - tw < leftEnd) break;
            elems_.push_back({ TrayIcon, i, D2D1::RectF(r - tw, 0, r, H), icons[i].tip });
            r -= tw;
        }
    }
}

void MenuBar::Render() {
    if (!hwnd_ || hidden_) return;
    auto* c = surf_.Begin(w_, h_);
    if (!c) return;
    Layout(c);
    bool dark = gfx::DarkMode();
    float s = s_;
    gfx::FillRect(c, D2D1::RectF(0, 0, (float)w_, (float)h_), gfx::ReduceTransparency() ? gfx::Panel(dark, 0, 0) : (dark ? gfx::Rgba(16, 16, 20, 0.34f) : gfx::Rgba(250, 250, 252, 0.50f)));
    // brilho de vidro bem sutil no topo da barra
    auto sheen = gfx::Gradient(c, D2D1::Point2F(0, 0), D2D1::Point2F(0, (float)h_), { { 0.f, gfx::White(0.10f * gfx::GlassStrength()) }, { 1.f, gfx::White(0) } });
    c->FillRectangle(D2D1::RectF(0, 0, (float)w_, (float)h_), sheen.Get());
    gfx::FillRect(c, D2D1::RectF(0, (float)h_ - 1, (float)w_, (float)h_), dark ? gfx::White(0.06f) : gfx::Black(0.06f));
    D2D1_COLOR_F fg = dark ? gfx::White(0.96f) : gfx::Black(0.88f);
    auto* f = gfx::Font(13.5f * s);
    auto* fb = gfx::Font(13.5f * s, DWRITE_FONT_WEIGHT_BOLD);
    for (int i = 0; i < (int)elems_.size(); ++i) {
        auto& e = elems_[i];
        float cy = (float)h_ / 2;
        if (i == open_ || i == hover_) {
            float a = i == open_ ? 0.24f : 0.12f;
            gfx::Fill(c, gfx::RR(e.r.left + 1 * s, 3 * s, e.r.right - 1 * s, h_ - 3 * s, 5 * s), dark ? gfx::White(a) : gfx::Black(a * 0.7f));
        }
        float cx = (e.r.left + e.r.right) / 2;
        switch (e.kind) {
        case Logo:
            DrawLogo(c, cx, cy, s, fg);
            if (!cfg::Str(L"Update", L"Available").empty()) gfx::Circle(c, cx + 7 * s, cy - 6 * s, 3.5f * s, gfx::Rgba(255, 69, 58));
            break;
        case AppName: gfx::Text(c, e.label, fb, D2D1::RectF(e.r.left + 9 * s, 0, e.r.right - 9 * s, (float)h_), fg); break;
        case Menu: gfx::Text(c, e.label, f, e.r, fg, DWRITE_TEXT_ALIGNMENT_CENTER); break;
        case Clock: gfx::Text(c, e.label, f, e.r, fg, DWRITE_TEXT_ALIGNMENT_CENTER); break;
        case Control: DrawControl(c, cx, cy, s, fg); break;
        case Search: DrawSearch(c, cx, cy, s, fg); break;
        case Wifi:
            if (wifiOn_ && !wifiWireless_) {  // cabo
                gfx::Text(c, L"", gfx::Font(15 * s, DWRITE_FONT_WEIGHT_REGULAR, L"Segoe MDL2 Assets"), e.r, fg, DWRITE_TEXT_ALIGNMENT_CENTER);
            } else {
                DrawWifi(c, cx, cy + 5 * s, s, wifiQ_, wifiOn_, fg);
            }
            break;
        case Battery: {
            float tx = e.r.left + 9 * s;
            gfx::Text(c, e.label, f, D2D1::RectF(tx, 0, e.r.right - 34 * s, (float)h_), fg, DWRITE_TEXT_ALIGNMENT_TRAILING);
            DrawBattery(c, e.r.right - 33 * s, cy, s, batPct_, batCharging_, fg);
            break;
        }
        case TrayIcon: {
            const auto& icons = tray::Icons();
            if (e.index < (int)icons.size() && icons[e.index].bmp) {
                float sz = 16 * s;
                surf_.DrawBitmap(icons[e.index].bmp.Get(), D2D1::RectF(cx - sz / 2, cy - sz / 2, cx + sz / 2, cy + sz / 2));
            }
            break;
        }
        }
    }
    surf_.End(x_, y_);
}

int MenuBar::HitElem(POINT p) const {
    for (int i = 0; i < (int)elems_.size(); ++i) {
        auto& r = elems_[i].r;
        if (p.x >= r.left && p.x < r.right && p.y >= 0 && p.y < h_) return i;
    }
    return -1;
}

std::vector<PMItem> MenuBar::BuildMenu(const Elem& e) {
    std::vector<PMItem> m;
    bool has = target_ && IsWindow(target_);
    switch (e.kind) {
    case Logo:
        m = { It(L"Sobre Este Computador", 1001), PMItem::Sep(), It(L"Ajustes do Sistema…", 1002), It(L"Microsoft Store…", 1003) };
        {
            std::wstring av = cfg::Str(L"Update", L"Available");
            if (!av.empty()) {
                PMItem u = It(L"Atualizar para o WinMac " + av + L"…", 1014);
                u.bold = true;
                m.push_back(u);
            }
        }
        for (auto& x : std::vector<PMItem>{ PMItem::Sep(), It(L"Forçar Encerrar…", 1004, L"Ctrl+Shift+Esc"), PMItem::Sep(), It(L"Repouso", 1005),
                                            It(L"Reiniciar…", 1006), It(L"Desligar…", 1007), PMItem::Sep(), It(L"Bloquear Tela", 1008, L"Win+L"),
                                            It(L"Encerrar Sessão…", 1009), PMItem::Sep(), It(L"Reiniciar o WinMac", 1010), It(L"Sair do WinMac", 1011) })
            m.push_back(x);
        break;
    case AppName:
        m = { It(L"Sobre o " + appName_, 1101, L"", has), PMItem::Sep(), It(L"Ocultar " + appName_, 1102, L"", has),
              It(L"Ocultar Outros", 1103), It(L"Mostrar Tudo", 1104), PMItem::Sep(), It(L"Encerrar " + appName_, 1105, L"", has) };
        break;
    case Menu:
        switch (e.index) {
        case 0:
            m = { It(L"Nova Janela", 1201, L"Ctrl+N"), It(L"Abrir…", 1202, L"Ctrl+O"), PMItem::Sep(), It(L"Salvar", 1203, L"Ctrl+S"),
                  It(L"Salvar Como…", 1204, L"Ctrl+Shift+S"), PMItem::Sep(), It(L"Imprimir…", 1205, L"Ctrl+P"), PMItem::Sep(),
                  It(L"Fechar Janela", 1206, L"Alt+F4", has) };
            break;
        case 1:
            m = { It(L"Desfazer", 1301, L"Ctrl+Z"), It(L"Refazer", 1302, L"Ctrl+Y"), PMItem::Sep(), It(L"Recortar", 1303, L"Ctrl+X"),
                  It(L"Copiar", 1304, L"Ctrl+C"), It(L"Colar", 1305, L"Ctrl+V"), It(L"Selecionar Tudo", 1306, L"Ctrl+A"), PMItem::Sep(),
                  It(L"Localizar…", 1307, L"Ctrl+F"), PMItem::Sep(), It(L"Emoji e Símbolos", 1308, L"Win+.") };
            break;
        case 2:
            m = { It(L"Entrar em Tela Cheia", 1401, L"F11"), PMItem::Sep(), It(L"Ampliar", 1402, L"Ctrl++"), It(L"Reduzir", 1403, L"Ctrl+-"),
                  It(L"Tamanho Real", 1404, L"Ctrl+0"), PMItem::Sep(), It(L"Atualizar", 1405, L"F5") };
            break;
        case 3: {
            m = { It(L"Minimizar", 1501, L"Win+↓", has), It(L"Zoom", 1502, L"", has), PMItem::Sep(), It(L"Encaixar à Esquerda", 1503, L"Win+←", has),
                  It(L"Encaixar à Direita", 1504, L"Win+→", has), It(L"Centralizar", 1505, L"", has), PMItem::Sep(),
                  It(L"Trazer Tudo para a Frente", 1506, L"", has) };
            auto wins = WindowsOf(target_);
            if (!wins.empty()) m.push_back(PMItem::Sep());
            for (size_t i = 0; i < wins.size() && i < 10; ++i) {
                std::wstring t = apps::Title(wins[i]);
                if (t.empty()) t = appName_;
                if (t.size() > 60) t = t.substr(0, 57) + L"…";
                m.push_back(It(t, 1510 + (int)i, L"", true, wins[i] == target_));
            }
            break;
        }
        case 4:
            m = { It(L"Ajuda do " + appName_, 1601, L"F1"), PMItem::Sep(), It(L"Atalhos do Teclado do Windows", 1603), It(L"Sobre o WinMac", 1602),
                  It(L"Boas-vindas do WinMac", 1604) };
            break;
        }
        break;
    default: break;
    }
    return m;
}

static bool Confirm(const wchar_t* text) {
    return MessageBoxW(nullptr, text, L"WinMac", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) == IDYES;
}

void MenuBar::RunCommand(int id) {
    HWND t = (target_ && IsWindow(target_)) ? target_ : nullptr;
    auto keys = [&](std::initializer_list<WORD> k) {
        if (t) apps::Activate(t);
        Sleep(70);
        apps::SendKeys(k);
    };
    switch (id) {
    case 1001: OpenAjustes(L"geral"); break;
    case 1002: OpenAjustes(); break;
    case 1013: OpenPath(L"ms-settings:"); break;
    case 1014: OpenAjustes(L"update"); break;
    case 1604: ShellExecuteW(nullptr, L"open", SelfPath().c_str(), L"--welcome", nullptr, SW_SHOWNORMAL); break;
    case 1003: OpenPath(L"ms-windows-store:"); break;
    case 1004: ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL); break;
    case 1005: {
        typedef BOOLEAN(WINAPI * SS)(BOOLEAN, BOOLEAN, BOOLEAN);
        if (HMODULE pm = LoadLibraryW(L"powrprof.dll"))
            if (auto f = (SS)GetProcAddress(pm, "SetSuspendState")) f(FALSE, FALSE, FALSE);
        break;
    }
    case 1006:
        if (Confirm(L"Tem certeza de que deseja reiniciar o computador agora?"))
            ShellExecuteW(nullptr, L"open", L"shutdown.exe", L"/r /t 0", nullptr, SW_HIDE);
        break;
    case 1007:
        if (Confirm(L"Tem certeza de que deseja desligar o computador agora?"))
            ShellExecuteW(nullptr, L"open", L"shutdown.exe", L"/s /hybrid /t 0", nullptr, SW_HIDE);
        break;
    case 1008: LockWorkStation(); break;
    case 1009:
        if (Confirm(L"Encerrar a sessão agora? Os apps abertos serão fechados.")) ExitWindowsEx(EWX_LOGOFF, 0);
        break;
    case 1010: RestartWinMac(); break;
    case 1015: OpenAjustes(L"barra"); break;
    case 1011: QuitWinMac(); break;
    case 1012: OpenAjustes(); break;
    case 1101:
        if (t) SHObjectProperties(nullptr, SHOP_FILEPATH, apps::Describe(t).exe.c_str(), nullptr);
        break;
    case 1102: for (HWND h : WindowsOf(t)) apps::Minimize(h); break;
    case 1103: {
        std::wstring k = t ? KeyOf(t) : L"";
        for (HWND h : apps::TaskWindows())
            if (KeyOf(h) != k) apps::Minimize(h);
        break;
    }
    case 1104:
        for (HWND h : apps::TaskWindows())
            if (IsIconic(h)) ShowWindowAsync(h, SW_SHOWNOACTIVATE);
        break;
    case 1105: for (HWND h : WindowsOf(t)) apps::Close(h); break;
    case 1201: keys({ VK_CONTROL, 'N' }); break;
    case 1202: keys({ VK_CONTROL, 'O' }); break;
    case 1203: keys({ VK_CONTROL, 'S' }); break;
    case 1204: keys({ VK_CONTROL, VK_SHIFT, 'S' }); break;
    case 1205: keys({ VK_CONTROL, 'P' }); break;
    case 1206: if (t) apps::Close(t); break;
    case 1301: keys({ VK_CONTROL, 'Z' }); break;
    case 1302: keys({ VK_CONTROL, 'Y' }); break;
    case 1303: keys({ VK_CONTROL, 'X' }); break;
    case 1304: keys({ VK_CONTROL, 'C' }); break;
    case 1305: keys({ VK_CONTROL, 'V' }); break;
    case 1306: keys({ VK_CONTROL, 'A' }); break;
    case 1307: keys({ VK_CONTROL, 'F' }); break;
    case 1308: keys({ VK_LWIN, VK_OEM_PERIOD }); break;
    case 1401: keys({ VK_F11 }); break;
    case 1402: keys({ VK_CONTROL, VK_OEM_PLUS }); break;
    case 1403: keys({ VK_CONTROL, VK_OEM_MINUS }); break;
    case 1404: keys({ VK_CONTROL, '0' }); break;
    case 1405: keys({ VK_F5 }); break;
    case 1501: if (t) apps::Minimize(t); break;
    case 1502: if (t) ShowWindowAsync(t, IsZoomed(t) ? SW_RESTORE : SW_MAXIMIZE); break;
    case 1503: keys({ VK_LWIN, VK_LEFT }); break;
    case 1504: keys({ VK_LWIN, VK_RIGHT }); break;
    case 1505:
        if (t) {
            if (IsZoomed(t)) ShowWindow(t, SW_RESTORE);
            RECT r;
            GetWindowRect(t, &r);
            MONITORINFO mi{ sizeof(mi) };
            GetMonitorInfoW(MonitorFromWindow(t, MONITOR_DEFAULTTONEAREST), &mi);
            int w = r.right - r.left, h = r.bottom - r.top;
            auto& wa = mi.rcWork;
            SetWindowPos(t, nullptr, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            apps::Activate(t);
        }
        break;
    case 1506: {
        auto wins = WindowsOf(t);
        for (auto it = wins.rbegin(); it != wins.rend(); ++it) apps::Activate(*it);
        break;
    }
    case 1601: keys({ VK_F1 }); break;
    case 1602: OpenAjustes(L"sobre"); break;
    case 1603: OpenPath(L"https://support.microsoft.com/pt-br/windows/atalhos-de-teclado-no-windows-dcc61a57-8ff0-cffe-9796-cb9706c75eec"); break;
    default:
        if (id >= 1510 && id < 1530) {
            auto wins = WindowsOf(t);
            if (id - 1510 < (int)wins.size()) apps::Activate(wins[id - 1510]);
        }
    }
}

void MenuBar::OpenMenu(int idx) {
    auto isMenu = [&](int i) { return i >= 0 && i < (int)elems_.size() && (elems_[i].kind == Logo || elems_[i].kind == AppName || elems_[i].kind == Menu); };
    int cur = idx;
    while (isMenu(cur)) {
        open_ = cur;
        Render();
        auto items = BuildMenu(elems_[cur]);
        int mx = x_ + (int)elems_[cur].r.left;
        int my = y_ + h_ + 1;
        int res = PopupMenu(items, mx, my, [&](POINT sp) -> bool {
            POINT cp{ sp.x - x_, sp.y - y_ };
            if (cp.y < 0 || cp.y >= h_) return false;
            int hit = HitElem(cp);
            return isMenu(hit) && hit != cur;
        });
        if (res == kMenuSwitch) {
            POINT p;
            GetCursorPos(&p);
            p.x -= x_;
            p.y -= y_;
            cur = HitElem(p);
            continue;
        }
        if (res == kMenuLeft || res == kMenuRight) {
            int n = 0;
            while (isMenu(n)) n++;
            cur = (cur + (res == kMenuLeft ? n - 1 : 1)) % n;
            continue;
        }
        open_ = -1;
        hover_ = -1;
        Render();
        if (res > 0) RunCommand(res);
        break;
    }
    open_ = -1;
    Render();
}

// ---------------- Central de Controle ----------------
namespace {
struct CCState {
    HWND hwnd = nullptr;
    Surface surf;
    float s = 1;
    int x = 0, y = 0, w = 0, h = 0;
    float vol = 0, bright = -1;
    bool muted = false, dark = true, done = false, openWifi = false;
    int drag = -1;   // 0 = brilho, 1 = volume
    DWORD lastBright = 0;
    std::wstring ssid;
    bool wifi = false;
    D2D1_RECT_F tWifi{}, tBt{}, tDark{}, tDnd{}, tileBright{}, tileVol{}, sBright{}, sVol{};
    DWORD start = 0;
    int bt = -1;            // Bluetooth: -1 sem rádio, 0 desligado, 1 ligado
    bool dnd = false;       // não perturbe (notificações desligadas)
};
CCState* g_cc = nullptr;

void CCRender(CCState& m) {
    auto* c = m.surf.Begin(m.w, m.h);
    if (!c) return;
    float s = m.s;
    float pt = Clamp01((GetTickCount() - m.start) / 200.f), pu = pt - 1.f;
    float pe = 1.f + 2.2f * pu * pu * pu + 1.2f * pu * pu;
    float psc = 0.92f + 0.08f * pe;
    c->SetTransform(D2D1::Matrix3x2F::Scale(psc, psc, D2D1::Point2F(m.w - 20 * s, 0)));
    bool dk = gfx::DarkMode();
    auto rr = gfx::RR(0, 0, (float)m.w, (float)m.h, 18 * s);
    gfx::Fill(c, rr, gfx::Panel(dk, 0.55f, 0.62f));
    gfx::GlassOverlay(c, rr, dk, 0.8f);
    D2D1_COLOR_F fg = dk ? gfx::White(0.95f) : gfx::Black(0.86f);
    D2D1_COLOR_F sub = dk ? gfx::White(0.55f) : gfx::Black(0.5f);
    D2D1_COLOR_F tile = dk ? gfx::White(0.09f) : gfx::White(0.55f);
    auto* fT = gfx::Font(13 * s, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto* fS = gfx::Font(11.5f * s);
    auto* glyph = gfx::Font(14 * s, DWRITE_FONT_WEIGHT_REGULAR, L"Segoe MDL2 Assets");
    auto tileRR = [&](D2D1_RECT_F r) { return gfx::RR(r.left, r.top, r.right, r.bottom, 14 * s); };
    auto toggle = [&](float cx, float cy, bool on, const wchar_t* g) {
        gfx::Circle(c, cx, cy, 15 * s, on ? gfx::Accent() : (dk ? gfx::White(0.16f) : gfx::Black(0.08f)));
        gfx::Text(c, g, glyph, D2D1::RectF(cx - 15 * s, cy - 15 * s, cx + 15 * s, cy + 15 * s), on ? gfx::White(1) : fg, DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    // bloco de conectividade
    D2D1_RECT_F big = D2D1::RectF(m.tWifi.left, m.tWifi.top, m.tBt.right, m.tBt.bottom);
    gfx::Fill(c, tileRR(big), tile);
    toggle(m.tWifi.left + 26 * s, (m.tWifi.top + m.tWifi.bottom) / 2, m.wifi, L"");
    gfx::Text(c, L"Wi-Fi", fT, D2D1::RectF(m.tWifi.left + 48 * s, m.tWifi.top + 8 * s, m.tWifi.right - 6 * s, (m.tWifi.top + m.tWifi.bottom) / 2 + 1 * s), fg, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    gfx::Text(c, m.wifi ? m.ssid : L"Desconectado", fS, D2D1::RectF(m.tWifi.left + 48 * s, (m.tWifi.top + m.tWifi.bottom) / 2 + 1 * s, m.tWifi.right - 6 * s, m.tWifi.bottom - 6 * s), sub, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    toggle(m.tBt.left + 26 * s, (m.tBt.top + m.tBt.bottom) / 2, m.bt == 1, L"");
    gfx::Text(c, L"Bluetooth", fT, D2D1::RectF(m.tBt.left + 48 * s, m.tBt.top + 8 * s, m.tBt.right - 6 * s, (m.tBt.top + m.tBt.bottom) / 2 + 1 * s), fg, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    gfx::Text(c, m.bt < 0 ? L"Indisponível" : m.bt ? L"Ligado" : L"Desligado", fS, D2D1::RectF(m.tBt.left + 48 * s, (m.tBt.top + m.tBt.bottom) / 2 + 1 * s, m.tBt.right - 6 * s, m.tBt.bottom - 6 * s), sub, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    // modo escuro e não perturbe
    gfx::Fill(c, tileRR(m.tDark), tile);
    toggle(m.tDark.left + 26 * s, (m.tDark.top + m.tDark.bottom) / 2, m.dark, L"");
    gfx::Text(c, L"Modo Escuro", fT, D2D1::RectF(m.tDark.left + 48 * s, m.tDark.top, m.tDark.right - 6 * s, m.tDark.bottom), fg);
    gfx::Fill(c, tileRR(m.tDnd), tile);
    toggle(m.tDnd.left + 26 * s, (m.tDnd.top + m.tDnd.bottom) / 2, m.dnd, L"");
    gfx::Text(c, L"Não Perturbe", fT, D2D1::RectF(m.tDnd.left + 48 * s, m.tDnd.top, m.tDnd.right - 6 * s, m.tDnd.bottom), fg);
    // sliders
    auto slider = [&](D2D1_RECT_F tileR, const wchar_t* title, float v, const wchar_t* g, bool enabled) {
        gfx::Fill(c, tileRR(tileR), tile);
        gfx::Text(c, title, fT, D2D1::RectF(tileR.left + 14 * s, tileR.top + 6 * s, tileR.right, tileR.top + 26 * s), fg);
        D2D1_RECT_F tr = D2D1::RectF(tileR.left + 12 * s, tileR.top + 30 * s, tileR.right - 12 * s, tileR.top + 52 * s);
        float rad = (tr.bottom - tr.top) / 2;
        gfx::Fill(c, gfx::RR(tr.left, tr.top, tr.right, tr.bottom, rad), dk ? gfx::White(0.14f) : gfx::Black(0.08f));
        if (enabled) {
            float fx = tr.left + rad * 2 + (tr.right - tr.left - rad * 2) * std::clamp(v, 0.f, 1.f);
            gfx::Fill(c, gfx::RR(tr.left, tr.top, fx, tr.bottom, rad), gfx::White(0.95f));
            gfx::Circle(c, fx - rad, (tr.top + tr.bottom) / 2, rad - 1 * s, gfx::White(1));
            gfx::Stroke(c, gfx::RR(fx - rad * 2, tr.top, fx, tr.bottom, rad), gfx::Black(0.10f), 1.f);
        }
        gfx::Text(c, g, gfx::Font(12 * s, DWRITE_FONT_WEIGHT_REGULAR, L"Segoe MDL2 Assets"), D2D1::RectF(tr.left, tr.top, tr.left + rad * 2, tr.bottom),
                  gfx::Rgba(90, 90, 96), DWRITE_TEXT_ALIGNMENT_CENTER);
        if (!enabled) gfx::Text(c, L"Indisponível", fS, tr, sub, DWRITE_TEXT_ALIGNMENT_CENTER);
        return tr;
    };
    m.sBright = slider(m.tileBright, L"Tela", m.bright / 100.f, L"", m.bright >= 0);
    m.sVol = slider(m.tileVol, L"Som", m.muted ? 0.f : m.vol, m.muted ? L"" : L"", true);
    c->SetTransform(D2D1::Matrix3x2F::Identity());
    m.surf.End(m.x, m.y, (BYTE)(255 * Clamp01(pt * 2.5f)));
}

float SliderValue(const D2D1_RECT_F& tr, float x, float s) {
    float rad = (tr.bottom - tr.top) / 2;
    return std::clamp((x - tr.left - rad) / (tr.right - tr.left - rad * 2), 0.f, 1.f);
}

bool In(const D2D1_RECT_F& r, POINT p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

LRESULT CALLBACK CCProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    CCState* m = g_cc;
    if (!m || m->hwnd != h) return DefWindowProcW(h, msg, wp, lp);
    POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    switch (msg) {
    case WM_LBUTTONDOWN:
        if (p.x < 0 || p.y < 0 || p.x >= m->w || p.y >= m->h) { m->done = true; return 0; }
        if (In(m->sBright, p) && m->bright >= 0) m->drag = 0;
        else if (In(m->sVol, p)) {
            float rad = (m->sVol.bottom - m->sVol.top) / 2;
            if (p.x < m->sVol.left + rad * 2) { m->muted = !m->muted; sys::SetMute(m->muted); CCRender(*m); return 0; }
            m->drag = 1;
        }
        [[fallthrough]];
    case WM_MOUSEMOVE:
        if (m->drag == 0) {
            m->bright = SliderValue(m->sBright, (float)p.x, m->s) * 100.f;
            DWORD now = GetTickCount();
            if (now - m->lastBright > 70) { sys::SetBrightness((int)m->bright); m->lastBright = now; }
            CCRender(*m);
        } else if (m->drag == 1) {
            m->vol = SliderValue(m->sVol, (float)p.x, m->s);
            m->muted = false;
            sys::SetVolume(m->vol);
            CCRender(*m);
        }
        return 0;
    case WM_LBUTTONUP:
        if (m->drag == 0) sys::SetBrightness((int)m->bright);
        if (m->drag >= 0) { m->drag = -1; return 0; }
        if (In(m->tWifi, p)) { m->openWifi = true; m->done = true; }
        else if (In(m->tBt, p)) {
            if (m->bt >= 0) {
                bool on = m->bt != 1;
                m->bt = on ? 1 : 0;
                CCRender(*m);
                std::thread([on]() { sysx::SetRadioState(sysx::Radio::Bluetooth, on); }).detach();
            } else { OpenAjustes(L"bluetooth"); m->done = true; }
        }
        else if (In(m->tDark, p)) { m->dark = !m->dark; sys::SetDarkMode(m->dark); CCRender(*m); }
        else if (In(m->tDnd, p)) { m->dnd = !m->dnd; sysx::SetToasts(!m->dnd); CCRender(*m); }
        return 0;
    case WM_MOUSEWHEEL: {
        m->vol = std::clamp(m->vol + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 0.04f : -0.04f), 0.f, 1.f);
        sys::SetVolume(m->vol);
        CCRender(*m);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) m->done = true;
        return 0;
    case WM_CAPTURECHANGED:
        if ((HWND)lp != h) m->done = true;
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) m->done = true;
        return 0;
    case WM_TIMER:
        CCRender(*m);
        if (GetTickCount() - m->start > 220) KillTimer(h, 1);
        return 0;
    case WM_APP + 70:   // estado do Bluetooth lido em segundo plano
        m->bt = (int)wp;
        CCRender(*m);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
}  // namespace

void MenuBar::ControlCenter() {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = CCProc;
        wc.hInstance = g_inst;
        wc.lpszClassName = kCcCls;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        reg = true;
    }
    CCState m;
    float s = m.s = s_;
    m.w = (int)(330 * s);
    m.h = (int)(258 * s);
    m.x = x_ + w_ - m.w - (int)(8 * s);
    m.y = y_ + h_ + (int)(5 * s);
    m.vol = sys::GetVolume(&m.muted);
    m.bright = (float)sys::GetBrightness();
    m.dark = gfx::DarkMode();
    auto wf = sys::GetWifi();
    m.wifi = wf.connected;
    m.ssid = wf.wireless ? wf.ssid : L"Cabo de rede";
    float p = 12 * s, gw = (m.w - 3 * p) / 2;
    m.tWifi = D2D1::RectF(p, p, p + gw, p + 52 * s);
    m.tBt = D2D1::RectF(p, p + 52 * s, p + gw, p + 104 * s);
    m.tDark = D2D1::RectF(2 * p + gw, p, m.w - p, p + 48 * s);
    m.tDnd = D2D1::RectF(2 * p + gw, p + 56 * s, m.w - p, p + 104 * s);
    float y1 = p + 104 * s + 8 * s;
    m.tileBright = D2D1::RectF(p, y1, m.w - p, y1 + 60 * s);
    m.tileVol = D2D1::RectF(p, y1 + 68 * s, m.w - p, y1 + 128 * s);
    m.h = (int)(m.tileVol.bottom + p);
    m.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kCcCls, L"", WS_POPUP, m.x, m.y, m.w, m.h, nullptr, nullptr, g_inst, nullptr);
    if (!m.hwnd) return;
    g_cc = &m;
    m.surf.Attach(m.hwnd);
    gfx::SetBlur(m.hwnd, true);
    gfx::SetRoundRegion(m.hwnd, 0, 0, m.w, m.h, (int)(18 * s));
    m.start = GetTickCount();
    m.dnd = !sysx::ToastsEnabled();
    {
        HWND target = m.hwnd;
        std::thread([target]() { PostMessageW(target, WM_APP + 70, (WPARAM)(INT_PTR)sysx::RadioState(sysx::Radio::Bluetooth), 0); }).detach();
    }
    CCRender(m);
    SetTimer(m.hwnd, 1, 10, nullptr);
    ShowWindow(m.hwnd, SW_SHOW);
    SetForegroundWindow(m.hwnd);
    SetFocus(m.hwnd);
    SetCapture(m.hwnd);
    MSG msg;
    while (!m.done) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) { PostQuitMessage((int)msg.wParam); break; }
        if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) msg.hwnd = m.hwnd;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (GetCapture() == m.hwnd) ReleaseCapture();
    g_cc = nullptr;
    m.surf.Reset();
    DestroyWindow(m.hwnd);
    if (m.openWifi) {
        for (auto& e : elems_)
            if (e.kind == Wifi) { OpenWifiPopup(x_ + (int)e.r.right + (int)(8 * s_), y_ + h_ + (int)(4 * s_)); break; }
    }
}

// ---------------- mensagens ----------------
LRESULT CALLBACK MenuBar::Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* mb = (MenuBar*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (mb && mb->hwnd_ == h) return mb->OnMsg(m, w, l);
    return DefWindowProcW(h, m, w, l);
}

LRESULT MenuBar::OnMsg(UINT m, WPARAM w, LPARAM l) {
    POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
    switch (m) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_TIMER:
        if (w == T_CLOCK || w == T_STATUS) { UpdateStatus(); Render(); }
        else if (w == T_TRAY && cfg::Int(L"MenuBar", L"ShowTray", 1) && !hidden_) {
            if (tray::Refresh()) Render();
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (!tracking_) {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd_, 0 };
            TrackMouseEvent(&t);
            tracking_ = true;
        }
        int hit = HitElem(p);
        if (hit != hover_) { hover_ = hit; Render(); }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        if (hover_ != -1) { hover_ = -1; Render(); }
        return 0;
    case WM_LBUTTONDOWN: {
        int hit = HitElem(p);
        if (hit >= 0 && (elems_[hit].kind == Logo || elems_[hit].kind == AppName || elems_[hit].kind == Menu)) OpenMenu(hit);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK: {
        int hit = HitElem(p);
        if (hit < 0) return 0;
        auto& e = elems_[hit];
        switch (e.kind) {
        case TrayIcon:
            if (e.index < (int)tray::Icons().size()) tray::Click(tray::Icons()[e.index], false, m == WM_LBUTTONDBLCLK);
            break;
        case Battery: OpenAjustes(L"energia"); break;
        case Wifi:
            if (m == WM_LBUTTONUP) OpenWifiPopup(x_ + (int)e.r.right + (int)(8 * s_), y_ + h_ + (int)(4 * s_));
            break;
        case Search:
            if (ProcessRunning(L"Flow.Launcher.exe")) apps::SendKeys({ VK_MENU, VK_SPACE });
            else ToggleLaunchpad();
            break;
        case Control:
            if (m == WM_LBUTTONUP) {
                open_ = hit;
                Render();
                ControlCenter();
                open_ = -1;
                hover_ = -1;
                Render();
            }
            break;
        case Clock: apps::SendKeys({ VK_LWIN, 'A' }); break;
        default: break;
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        int hit = HitElem(p);
        if (hit >= 0 && elems_[hit].kind == TrayIcon) {
            if (elems_[hit].index < (int)tray::Icons().size()) tray::Click(tray::Icons()[elems_[hit].index], true, false);
            return 0;
        }
        POINT sp = p;
        ClientToScreen(hwnd_, &sp);
        int r = PopupMenu({ It(L"Ajustes da Barra de Menus…", 1015), It(L"Reiniciar o WinMac", 1010), It(L"Sair do WinMac", 1011) }, sp.x, y_ + h_ + 1);
        if (r > 0) RunCommand(r);
        return 0;
    }
    case WM_MD_APPBAR:
        if (w == ABN_FULLSCREENAPP) PostMessageW(g_ctrl, WM_MD_FULLSCREEN, l, 0);
        else if (w == ABN_POSCHANGED) AppBarReserve(hwnd_, ABE_TOP, h_);
        return 0;
    }
    return DefWindowProcW(hwnd_, m, w, l);
}
