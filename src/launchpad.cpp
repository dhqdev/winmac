#include "launchpad.h"
#include "apps.h"
#include "gfx.h"

namespace launchpad {
namespace {
struct App {
    std::wstring name, id;
    ComPtr<IWICBitmapSource> icon;
    bool tried = false;
};
std::vector<App> g_apps;
bool g_loaded = false;
size_t g_iconCursor = 0;
HWND g_hwnd = nullptr;
HWND g_loader = nullptr;
Surface g_surf;
const wchar_t* kCls = L"WinMac.Launchpad";
const wchar_t* kLoaderCls = L"WinMac.LaunchpadLoader";
enum { T_ANIM = 1, T_ICONS = 2 };

struct State {
    int x = 0, y = 0, w = 0, h = 0;
    float s = 1;
    int page = 0;
    float pageAnim = 0;      // deslocamento animado em páginas
    float open = 0;          // 0..1
    bool closing = false;
    DWORD last = 0;
    std::wstring query;
    int hover = -1;
    std::vector<int> visible;   // índices em g_apps
    int cols = 7, rows = 5;
    float cellW = 0, cellH = 0, gridL = 0, gridT = 0, icon = 0;
} st;

void LoadList() {
    if (g_loaded) return;
    g_loaded = true;
    ComPtr<IShellItem> folder;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder)))) return;
    ComPtr<IEnumShellItems> en;
    if (FAILED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&en)))) return;
    IShellItem* it = nullptr;
    std::set<std::wstring> seen;
    while (en->Next(1, &it, nullptr) == S_OK) {
        PWSTR n = nullptr, id = nullptr;
        it->GetDisplayName(SIGDN_NORMALDISPLAY, &n);
        it->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &id);
        if (n && id) {
            std::wstring name = n, sid = id, ln = Lower(name), lid = Lower(sid);
            bool skip = ln.find(L"uninstall") != std::wstring::npos || ln.find(L"desinstalar") != std::wstring::npos ||
                        ln.find(L"readme") != std::wstring::npos || ln.find(L"leia-me") != std::wstring::npos ||
                        ln.find(L"documentation") != std::wstring::npos || ln.find(L"manuals") != std::wstring::npos;
            for (const wchar_t* ext : { L".url", L".chm", L".txt", L".html", L".htm", L".pdf", L".rtf" })
                if (lid.size() > wcslen(ext) && lid.compare(lid.size() - wcslen(ext), wcslen(ext), ext) == 0) skip = true;
            if (lid.rfind(L"http", 0) == 0) skip = true;
            if (!skip && seen.insert(ln).second) g_apps.push_back({ name, sid, nullptr, false });
        }
        if (n) CoTaskMemFree(n);
        if (id) CoTaskMemFree(id);
        it->Release();
    }
    std::sort(g_apps.begin(), g_apps.end(), [](const App& a, const App& b) {
        return CompareStringEx(L"pt-BR", LINGUISTIC_IGNORECASE, a.name.c_str(), -1, b.name.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
}

void LoadSomeIcons(int n) {
    for (; g_iconCursor < g_apps.size() && n > 0; ++g_iconCursor) {
        auto& a = g_apps[g_iconCursor];
        if (a.tried) continue;
        a.tried = true;
        a.icon = apps::AppIcon(a.id, L"", nullptr, 128);
        --n;
    }
}

void Filter() {
    st.visible.clear();
    std::wstring q = Lower(st.query);
    for (int i = 0; i < (int)g_apps.size(); ++i)
        if (q.empty() || Lower(g_apps[i].name).find(q) != std::wstring::npos) st.visible.push_back(i);
    int per = st.cols * st.rows;
    int pages = std::max(1, ((int)st.visible.size() + per - 1) / per);
    st.page = std::clamp(st.page, 0, pages - 1);
    st.hover = -1;
}

int Pages() {
    int per = st.cols * st.rows;
    return std::max(1, ((int)st.visible.size() + per - 1) / per);
}

void Render() {
    auto* c = g_surf.Begin(st.w, st.h);
    if (!c) return;
    float s = st.s;
    float e = 1.f - std::pow(1.f - st.open, 3.f);  // ease-out
    gfx::FillRect(c, D2D1::RectF(0, 0, (float)st.w, (float)st.h), gfx::Rgba(10, 10, 14, 0.42f * e));
    // busca
    float sw = 240 * s, sh = 30 * s, sx = st.w / 2.f - sw / 2, sy = 46 * s;
    auto srr = gfx::RR(sx, sy, sx + sw, sy + sh, 8 * s);
    gfx::Fill(c, srr, gfx::White(0.14f * e));
    gfx::Stroke(c, srr, gfx::White(0.22f * e), 1.f);
    gfx::Text(c, L"", gfx::Font(13 * s, DWRITE_FONT_WEIGHT_REGULAR, L"Segoe MDL2 Assets"), D2D1::RectF(sx + 10 * s, sy, sx + 30 * s, sy + sh), gfx::White(0.7f * e));
    gfx::Text(c, st.query.empty() ? L"Buscar" : st.query + L"|", gfx::Font(14 * s), D2D1::RectF(sx + 34 * s, sy, sx + sw - 10 * s, sy + sh),
              gfx::White((st.query.empty() ? 0.55f : 0.95f) * e));
    // grade com zoom de abertura (efeito de "expandir")
    float zoom = 1.12f - 0.12f * e;
    c->SetTransform(D2D1::Matrix3x2F::Scale(zoom, zoom, D2D1::Point2F(st.w / 2.f, st.h / 2.f)));
    int per = st.cols * st.rows;
    auto* f = gfx::Font(12.5f * s, DWRITE_FONT_WEIGHT_MEDIUM);
    int pages = Pages();
    for (int pg = 0; pg < pages; ++pg) {
        float off = (pg - st.pageAnim) * st.w;
        if (std::fabs(off) >= st.w) continue;
        for (int k = 0; k < per; ++k) {
            int vi = pg * per + k;
            if (vi >= (int)st.visible.size()) break;
            auto& a = g_apps[st.visible[vi]];
            int col = k % st.cols, row = k / st.cols;
            float cx = st.gridL + col * st.cellW + st.cellW / 2 + off, cy = st.gridT + row * st.cellH;
            float isz = st.icon * (vi == st.hover ? 1.06f : 1.f);
            D2D1_RECT_F ir = D2D1::RectF(cx - isz / 2, cy + (st.icon - isz) / 2, cx + isz / 2, cy + (st.icon + isz) / 2);
            if (!a.tried) { a.tried = true; a.icon = apps::AppIcon(a.id, L"", nullptr, 128); }
            if (a.icon) g_surf.DrawBitmap(a.icon.Get(), ir, e);
            else gfx::Fill(c, gfx::RR(ir.left, ir.top, ir.right, ir.bottom, isz * 0.22f), gfx::White(0.2f * e));
            gfx::Text(c, a.name, f, D2D1::RectF(cx - st.cellW / 2 + 6 * s, cy + st.icon + 6 * s, cx + st.cellW / 2 - 6 * s, cy + st.icon + 26 * s),
                      gfx::White(0.95f * e), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    c->SetTransform(D2D1::Matrix3x2F::Identity());
    if (pages > 1) {
        float dy = st.h - 54 * s, dx = st.w / 2.f - (pages - 1) * 9 * s;
        for (int i = 0; i < pages; ++i) gfx::Circle(c, dx + i * 18 * s, dy, 4 * s, gfx::White((i == st.page ? 0.9f : 0.35f) * e));
    }
    g_surf.End(st.x, st.y);
}

int HitApp(POINT p) {
    float zoom = 1.f;
    (void)zoom;
    int per = st.cols * st.rows;
    for (int k = 0; k < per; ++k) {
        int vi = st.page * per + k;
        if (vi >= (int)st.visible.size()) break;
        int col = k % st.cols, row = k / st.cols;
        float l = st.gridL + col * st.cellW, t = st.gridT + row * st.cellH;
        if (p.x >= l + st.cellW * 0.12f && p.x < l + st.cellW * 0.88f && p.y >= t - 4 && p.y < t + st.icon + 28 * st.s) return vi;
    }
    return -1;
}

void Close() {
    if (!g_hwnd || st.closing) return;
    st.closing = true;
    st.last = GetTickCount();
    SetTimer(g_hwnd, T_ANIM, 15, nullptr);
}

void Animate() {
    DWORD now = GetTickCount();
    float dt = std::min(0.05f, (now - st.last) / 1000.f);
    st.last = now;
    bool busy = false;
    if (st.closing) {
        st.open = std::max(0.f, st.open - dt / 0.16f);
        if (st.open <= 0) {
            KillTimer(g_hwnd, T_ANIM);
            HWND h = g_hwnd;
            g_hwnd = nullptr;
            g_surf.Reset();
            DestroyWindow(h);
            return;
        }
        busy = true;
    } else if (st.open < 1) {
        st.open = std::min(1.f, st.open + dt / 0.22f);
        busy = true;
    }
    float target = (float)st.page;
    if (std::fabs(st.pageAnim - target) > 0.001f) {
        st.pageAnim += (target - st.pageAnim) * (1.f - std::exp(-dt / 0.07f));
        if (std::fabs(st.pageAnim - target) < 0.002f) st.pageAnim = target;
        busy = true;
    }
    Render();
    if (!busy) KillTimer(g_hwnd, T_ANIM);
}

void Kick() {
    st.last = GetTickCount();
    SetTimer(g_hwnd, T_ANIM, 15, nullptr);
}

void Launch(int vi) {
    if (vi < 0 || vi >= (int)st.visible.size()) return;
    std::wstring id = g_apps[st.visible[vi]].id;
    apps::Launch(id);
    Close();
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (h != g_hwnd) return DefWindowProcW(h, m, w, l);
    POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
    switch (m) {
    case WM_TIMER:
        if (w == T_ANIM) Animate();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) Close();
        return 0;
    case WM_MOUSEMOVE: {
        int hit = HitApp(p);
        if (hit != st.hover) { st.hover = hit; Render(); }
        return 0;
    }
    case WM_LBUTTONUP: {
        int hit = HitApp(p);
        if (hit >= 0) Launch(hit);
        else if (p.y > 90 * st.s) Close();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(w) < 0 ? 1 : -1;
        st.page = std::clamp(st.page + d, 0, Pages() - 1);
        Kick();
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            if (!st.query.empty()) { st.query.clear(); Filter(); Render(); }
            else Close();
        } else if (w == VK_RIGHT) { st.page = std::min(st.page + 1, Pages() - 1); Kick(); }
        else if (w == VK_LEFT) { st.page = std::max(st.page - 1, 0); Kick(); }
        else if (w == VK_RETURN) { if (!st.visible.empty()) Launch(st.hover >= 0 ? st.hover : st.page * st.cols * st.rows); }
        return 0;
    case WM_CHAR:
        if (w == 8) { if (!st.query.empty()) st.query.pop_back(); }
        else if (w >= 32) st.query += (wchar_t)w;
        else return 0;
        Filter();
        st.pageAnim = (float)st.page;
        Render();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK LoaderProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER && w == T_ICONS) {
        if (g_hwnd) return 0;  // aberto: carrega sob demanda no Render
        LoadList();
        LoadSomeIcons(4);
        if (g_iconCursor >= g_apps.size()) KillTimer(h, T_ICONS);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}  // namespace

bool IsOpen() { return g_hwnd != nullptr; }

void Preload() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = LoaderProc;
    wc.hInstance = g_inst;
    wc.lpszClassName = kLoaderCls;
    RegisterClassW(&wc);
    g_loader = CreateWindowExW(0, kLoaderCls, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_inst, nullptr);
    SetTimer(g_loader, T_ICONS, 120, nullptr);
}

void Toggle() {
    if (g_hwnd) { Close(); return; }
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
    LoadList();
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    st = State{};
    st.s = gfx::Scale();
    st.x = mi.rcMonitor.left;
    st.y = mi.rcMonitor.top;
    st.w = mi.rcMonitor.right - mi.rcMonitor.left;
    st.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    float s = st.s;
    st.icon = 70 * s;
    float gridW = std::min(st.w - 160 * s, 1040 * s);
    st.cellW = gridW / st.cols;
    st.cellH = std::min((st.h - 220 * s) / st.rows, 132 * s);
    st.gridL = st.w / 2.f - gridW / 2;
    st.gridT = 110 * s + ((st.h - 220 * s) - st.cellH * st.rows) / 2;
    Filter();
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kCls, L"Launchpad", WS_POPUP, st.x, st.y, st.w, st.h,
                             nullptr, nullptr, g_inst, nullptr);
    if (!g_hwnd) return;
    g_surf.Attach(g_hwnd);
    gfx::SetBlur(g_hwnd, true);
    st.last = GetTickCount();
    Render();
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
    SetFocus(g_hwnd);
    Kick();
}
}  // namespace launchpad
