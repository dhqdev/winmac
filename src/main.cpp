#include "common.h"
#include "gfx.h"
#include "config.h"
#include "dock.h"
#include "menubar.h"
#include "widgets.h"
#include "launchpad.h"
#include "apps.h"
#include "art.h"
#include "install.h"
#include "update.h"
#include "notify.h"
#include "settings.h"
#include "welcome.h"
#include "switcher.h"
#include "traffic.h"
#include "version.h"
#include "wifiui.h"
#include <ctime>

HINSTANCE g_inst = nullptr;
HWND g_ctrl = nullptr;
namespace anim { void Init(); void Reload(); }

namespace {
UINT g_shellMsg = 0, g_taskbarCreated = 0;
HWINEVENTHOOK g_tbHook = nullptr;
bool g_tbHidden = false;
std::set<HWND> g_appbars;
HWND g_corners[2] = {};
bool g_fullscreen = false;
enum { T_RELAYOUT = 1, T_WALLPAPER = 2, T_UPDATE = 3, T_THEME = 4, T_WELCOMEBACK = 5 };

// ---------------- barra de tarefas do Windows ----------------
HWND Tray() { return FindWindowW(L"Shell_TrayWnd", nullptr); }

void CALLBACK TbShowHook(HWINEVENTHOOK, DWORD, HWND h, LONG obj, LONG, DWORD, DWORD) {
    if (!g_tbHidden || obj != OBJID_WINDOW) return;
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd")) ShowWindow(h, SW_HIDE);
}

void SetTaskbarState(UINT st) {
    APPBARDATA abd{ sizeof(abd) };
    abd.hWnd = Tray();
    abd.lParam = st;
    SHAppBarMessage(ABM_SETSTATE, &abd);
}

void HideTaskbar() {
    HWND t = Tray();
    if (!t) return;
    if (cfg::Int(L"State", L"TaskbarHidden", 0) == 0) {
        APPBARDATA abd{ sizeof(abd) };
        abd.hWnd = t;
        cfg::SetInt(L"State", L"TaskbarOrigState", (int)SHAppBarMessage(ABM_GETSTATE, &abd));
        cfg::SetInt(L"State", L"TaskbarHidden", 1);
    }
    SetTaskbarState(ABS_AUTOHIDE);
    g_tbHidden = true;
    ShowWindow(t, SW_HIDE);
    for (HWND s = FindWindowW(L"Shell_SecondaryTrayWnd", nullptr); s; s = FindWindowExW(nullptr, s, L"Shell_SecondaryTrayWnd", nullptr)) ShowWindow(s, SW_HIDE);
    if (!g_tbHook) {
        DWORD pid = 0;
        GetWindowThreadProcessId(t, &pid);
        g_tbHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, TbShowHook, pid, 0, WINEVENT_OUTOFCONTEXT);
    }
}

void RestoreTaskbar() {
    g_tbHidden = false;
    if (g_tbHook) { UnhookWinEvent(g_tbHook); g_tbHook = nullptr; }
    HWND t = Tray();
    if (!t) return;
    int orig = cfg::Int(L"State", L"TaskbarOrigState", ABS_ALWAYSONTOP);
    SetTaskbarState((UINT)orig);
    ShowWindow(t, SW_SHOWNA);
    for (HWND s = FindWindowW(L"Shell_SecondaryTrayWnd", nullptr); s; s = FindWindowExW(nullptr, s, L"Shell_SecondaryTrayWnd", nullptr)) ShowWindow(s, SW_SHOWNA);
    cfg::SetInt(L"State", L"TaskbarHidden", 0);
}

void SaveAppBars() {
    std::wstring s;
    for (HWND h : g_appbars) s += std::to_wstring((unsigned long long)(ULONG_PTR)h) + L",";
    cfg::Set(L"State", L"AppBars", s);
}

// Processo vigia: se o WinMac fechar sem restaurar a barra de tarefas (falha), restaura e reabre.
void RunWatchdog(DWORD pid) {
    HANDLE p = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return;
    WaitForSingleObject(p, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(p, &code);
    CloseHandle(p);
    if (cfg::Int(L"State", L"TaskbarHidden", 0) != 1) return;  // saiu normalmente
    std::wstring bars = cfg::Str(L"State", L"AppBars");
    size_t a = 0;
    while (a < bars.size()) {
        size_t b = bars.find(L',', a);
        if (b == std::wstring::npos) b = bars.size();
        unsigned long long v = _wcstoui64(bars.substr(a, b - a).c_str(), nullptr, 10);
        if (v) {
            APPBARDATA abd{ sizeof(abd) };
            abd.hWnd = (HWND)(ULONG_PTR)v;
            SHAppBarMessage(ABM_REMOVE, &abd);
        }
        a = b + 1;
    }
    RestoreTaskbar();
    long long now = (long long)time(nullptr);
    long long start = _wtoi64(cfg::Str(L"State", L"CrashWindow", L"0").c_str());
    int n = cfg::Int(L"State", L"Crashes", 0);
    if (now - start > 300) { start = now; n = 0; }
    n++;
    cfg::Set(L"State", L"CrashWindow", std::to_wstring(start));
    cfg::SetInt(L"State", L"Crashes", n);
    Log(L"WinMac terminou inesperadamente (código 0x%08X), falha %d", code, n);
    if (n <= 3) ShellExecuteW(nullptr, L"open", SelfPath().c_str(), L"--shell --wait", nullptr, SW_SHOWNORMAL);
}

void StartWatchdog() {
    std::wstring exe = SelfPath();
    std::wstring cmd = L"\"" + exe + L"\" --watchdog " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS*) {
    RestoreTaskbar();
    for (HWND h : g_appbars) {
        APPBARDATA abd{ sizeof(abd) };
        abd.hWnd = h;
        SHAppBarMessage(ABM_REMOVE, &abd);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------- cantos arredondados da tela (como a tela do MacBook) ----------------
const wchar_t* kCornerCls = L"WinMac.Corner";

void DrawCorner(HWND h, int i, int r, int x, int y) {
    Surface s;
    s.Attach(h);
    if (auto* c = s.Begin(r, r)) {
        ComPtr<ID2D1PathGeometry> g;
        gfx::d2d->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> sk;
        g->Open(&sk);
        float R = (float)r;
        if (i == 0) {
            sk->BeginFigure(D2D1::Point2F(0, 0), D2D1_FIGURE_BEGIN_FILLED);
            sk->AddLine(D2D1::Point2F(R, 0));
            sk->AddArc(D2D1::ArcSegment(D2D1::Point2F(0, R), D2D1::SizeF(R, R), 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        } else {
            sk->BeginFigure(D2D1::Point2F(R, 0), D2D1_FIGURE_BEGIN_FILLED);
            sk->AddLine(D2D1::Point2F(R, R));
            sk->AddArc(D2D1::ArcSegment(D2D1::Point2F(0, 0), D2D1::SizeF(R, R), 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        }
        sk->EndFigure(D2D1_FIGURE_END_CLOSED);
        sk->Close();
        c->FillGeometry(g.Get(), gfx::Brush(c, gfx::Black(1)).Get());
        s.End(x, y);
    }
}

void CreateCorners() {
    if (!cfg::Int(L"Screen", L"RoundedCorners", 1)) {
        for (HWND& c : g_corners)
            if (c) { DestroyWindow(c); c = nullptr; }
        return;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = g_inst;
    wc.lpszClassName = kCornerCls;
    RegisterClassW(&wc);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    int r = (int)std::round(std::clamp(cfg::Int(L"Screen", L"CornerRadius", 10), 2, 40) * gfx::Scale());
    for (int i = 0; i < 2; ++i) {
        int x = i == 0 ? mi.rcMonitor.left : mi.rcMonitor.right - r;
        int y = mi.rcMonitor.top;
        if (!g_corners[i])
            g_corners[i] = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kCornerCls, L"",
                                           WS_POPUP, x, y, r, r, nullptr, nullptr, g_inst, nullptr);
        DrawCorner(g_corners[i], i, r, x, y);
        ShowWindow(g_corners[i], SW_SHOWNOACTIVATE);
        SetWindowPos(g_corners[i], HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// O Windows Spotlight às vezes religa sozinho e troca o papel de parede: só nesse caso o WinMac devolve o escolhido.
// Se você trocar o papel de parede pelo próprio Windows, o WinMac respeita.
void EnforceWallpaper() {
    if (!cfg::Int(L"Screen", L"Enforce", 1)) return;
    std::wstring wp = cfg::Str(L"Screen", L"Wallpaper", L"");
    if (wp.empty() || wp == L"-" || GetFileAttributesW(wp.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    DWORD spot = 0, type = 0, sz = sizeof(DWORD);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\DesktopSpotlight\\Settings", L"EnabledState", RRF_RT_REG_DWORD, nullptr, &spot, &sz);
    sz = sizeof(DWORD);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Wallpapers", L"BackgroundType", RRF_RT_REG_DWORD, nullptr, &type, &sz);
    if (spot == 0 && type == 0) return;
    install::ApplyWallpaperFile(wp);
}

void RelayoutAll() {
    gfx::ReloadTheme();
    g_menubar.Relayout();
    g_dock.Relayout();
    g_widgets.Relayout();
    CreateCorners();
}

void OnReload(WPARAM what) {
    gfx::ReloadTheme();
    switch (what) {
    case RL_THEME:
        g_menubar.Render();
        g_dock.Redraw();
        g_widgets.RedrawAll();
        break;
    case RL_DOCK: g_dock.Reload(); break;
    case RL_MENUBAR: g_menubar.Reload(); break;
    case RL_WIDGETS:
        g_widgets.Destroy();
        g_widgets.Create();
        break;
    case RL_WINDOWS:
        traffic::Reload();
        switcher::Reload();
        anim::Reload();
        break;
    case RL_WALLPAPER: break;
    default:
        g_menubar.Reload();
        g_dock.Reload();
        g_widgets.Destroy();
        g_widgets.Create();
        CreateCorners();
        traffic::Reload();
        switcher::Reload();
        anim::Reload();
        break;
    }
}

void OnUpdateInfo(update::Info* in) {
    std::unique_ptr<update::Info> keep(in);
    update::SaveAvailable(*in);
    g_menubar.Render();
    if (!in->newer) return;
    if (cfg::Str(L"Update", L"Notified") == in->version) return;
    cfg::Set(L"Update", L"Notified", in->version);
    notify::Show(L"Atualização disponível", L"O WinMac " + in->version + L" chegou. Clique para ver as novidades e atualizar.", []() { OpenAjustes(L"update"); }, 12000);
}

LRESULT CALLBACK CtrlProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_shellMsg && g_shellMsg) {
        LRESULT r = 0;
        g_dock.OnShellHook(w, l, &r);
        return r;
    }
    if (m == g_taskbarCreated && g_taskbarCreated) {
        // o Explorer reiniciou: esconde a barra de novo e refaz as reservas de espaço
        if (g_tbHook) { UnhookWinEvent(g_tbHook); g_tbHook = nullptr; }
        for (HWND a : std::vector<HWND>(g_appbars.begin(), g_appbars.end())) AppBarRemove(a);
        HideTaskbar();
        RelayoutAll();
        return 0;
    }
    switch (m) {
    case WM_MD_FULLSCREEN: {
        bool fs = w != 0;
        if (fs && launchpad::IsOpen()) return 0;
        if (fs == g_fullscreen) return 0;
        g_fullscreen = fs;
        g_menubar.SetHidden(fs);
        g_dock.SetHidden(fs);
        for (HWND c : g_corners) if (c) ShowWindow(c, fs ? SW_HIDE : SW_SHOWNOACTIVATE);
        return 0;
    }
    case WM_MD_RELOAD:
        OnReload(w);
        return 0;
    case WM_MD_UPDATE:
        OnUpdateInfo((update::Info*)l);
        return 0;
    case WM_MD_ASYNC: {
        auto* fn = (std::function<void()>*)l;
        if (fn) { (*fn)(); delete fn; }
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (l && !wcscmp((const wchar_t*)l, L"ImmersiveColorSet")) {
            gfx::ReloadTheme();
            g_menubar.Render();
            g_dock.Redraw();
            g_widgets.RedrawAll();
        } else if (w == SPI_SETWORKAREA) {
            g_widgets.Relayout();
        }
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        SetTimer(h, T_RELAYOUT, 400, nullptr);
        return 0;
    case WM_TIMER:
        if (w == T_RELAYOUT) { KillTimer(h, T_RELAYOUT); RelayoutAll(); }
        else if (w == T_WALLPAPER) EnforceWallpaper();
        else if (w == T_UPDATE) {
            SetTimer(h, T_UPDATE, 6 * 3600 * 1000, nullptr);
            if (cfg::Int(L"Update", L"Auto", 1)) update::CheckAsync(h);
        } else if (w == T_THEME) {
            if (cfg::Int(L"Appearance", L"Theme", 1) == 2) install::ApplyTheme(2);
        } else if (w == T_WELCOMEBACK) {
            KillTimer(h, T_WELCOMEBACK);
            std::wstring v = cfg::Str(L"Update", L"JustUpdated");
            if (!v.empty()) {
                cfg::Set(L"Update", L"JustUpdated", L"");
                notify::Show(L"WinMac atualizado 🎉", L"Agora você está na versão " + v + L". Clique para ver o que mudou.", []() { OpenAjustes(L"update"); });
            }
        }
        return 0;
    case WM_POWERBROADCAST:
        g_menubar.Render();
        return TRUE;
    case WM_ENDSESSION:
        if (w) RestoreTaskbar();
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int RunShell(const std::wstring& args) {
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\WinMac.Singleton");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (args.find(L"--wait") != std::wstring::npos) {
            CloseHandle(mtx);
            mtx = nullptr;
            for (int i = 0; i < 100; ++i) {
                Sleep(100);
                mtx = CreateMutexW(nullptr, TRUE, L"Local\\WinMac.Singleton");
                if (GetLastError() != ERROR_ALREADY_EXISTS) break;
                CloseHandle(mtx);
                mtx = nullptr;
            }
            if (!mtx) return 0;
        } else {
            HWND other = FindWindowW(L"WinMac.Ctrl", nullptr);
            if (other && args.find(L"--quit") != std::wstring::npos) PostMessageW(other, WM_CLOSE, 0, 0);
            else if (other) OpenAjustes();   // abriu de novo pelo atalho: mostra os Ajustes
            if (mtx) CloseHandle(mtx);
            return 0;
        }
    }
    if (args.find(L"--quit") != std::wstring::npos) return 0;
    SetUnhandledExceptionFilter(CrashFilter);
    install::Migrate();

    WNDCLASSW wc{};
    wc.lpfnWndProc = CtrlProc;
    wc.hInstance = g_inst;
    wc.lpszClassName = L"WinMac.Ctrl";
    RegisterClassW(&wc);
    g_ctrl = CreateWindowExW(WS_EX_TOOLWINDOW, L"WinMac.Ctrl", L"WinMac", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, g_inst, nullptr);
    g_shellMsg = RegisterWindowMessageW(L"SHELLHOOK");
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(g_ctrl, g_taskbarCreated, MSGFLT_ALLOW, nullptr);

    if (args.find(L"--no-watchdog") == std::wstring::npos) StartWatchdog();
    EnforceWallpaper();
    SetTimer(g_ctrl, T_WALLPAPER, 10 * 60 * 1000, nullptr);
    SetTimer(g_ctrl, T_UPDATE, 90 * 1000, nullptr);
    SetTimer(g_ctrl, T_THEME, 10 * 60 * 1000, nullptr);
    SetTimer(g_ctrl, T_WELCOMEBACK, 4000, nullptr);
    HideTaskbar();
    if (cfg::Int(L"MenuBar", L"Enabled", 1)) g_menubar.Create();
    g_dock.Create();
    g_widgets.Create();
    CreateCorners();
    RegisterShellHookWindow(g_ctrl);
    launchpad::Preload();
    anim::Init();
    switcher::Init();
    traffic::Init();
    if (args.find(L"--bench") != std::wstring::npos) {
        double ms = g_dock.Bench(120);
        Log(L"bench: quadro do dock com ampliação = %.2f ms", ms);
        PostMessageW(g_ctrl, WM_CLOSE, 0, 0);
    }
    // devolve o espaço livre para o processo (o arranque usa mais memória que o regime normal)
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeregisterShellHookWindow(g_ctrl);
    traffic::Shutdown();
    switcher::Shutdown();
    g_widgets.Destroy();
    g_dock.Destroy();
    g_menubar.Destroy();
    for (HWND c : g_corners) if (c) DestroyWindow(c);
    RestoreTaskbar();
    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
    return 0;
}

std::wstring ArgAfter(const std::wstring& args, const wchar_t* flag) {
    size_t p = args.find(flag);
    if (p == std::wstring::npos) return L"";
    std::wstring r = args.substr(p + wcslen(flag));
    while (!r.empty() && r.front() == L' ') r.erase(0, 1);
    if (!r.empty() && r.front() == L'"') return r.substr(1, r.find(L'"', 1) - 1);
    size_t sp = r.find(L' ');
    return sp == std::wstring::npos ? r : r.substr(0, sp);
}

void RenderShots(const std::wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
    WifiPanel::demo = true;
    for (const wchar_t* p : { L"wifi", L"geral", L"update", L"aparencia", L"papel", L"dock", L"barra", L"janelas", L"arquivos", L"apps", L"som", L"sobre" })
        settings::RenderPreview(p, dir + L"\\ajustes_" + p + L".png");
    for (int i = 0; i < 6; ++i) welcome::RenderPreview(i, dir + L"\\welcome_" + std::to_wstring(i) + L".png");
    switcher::RenderPreview(dir + L"\\alttab.png");
    traffic::RenderPreview(dir + L"\\traffic.png");
}
}  // namespace

// ---------------- funções globais ----------------
std::wstring AppDataDir() {
    wchar_t* p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) r = std::wstring(p) + L"\\WinMac";
    CoTaskMemFree(p);
    return r;
}

std::wstring SelfPath() {
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
    return std::wstring(b, n);
}

std::wstring ExeDir() {
    wchar_t b[MAX_PATH];
    GetModuleFileNameW(nullptr, b, MAX_PATH);
    PathRemoveFileSpecW(b);
    return b;
}

void Log(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vswprintf_s(buf, fmt, ap);
    va_end(ap);
    std::wstring path = AppDataDir() + L"\\winmac.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"a, ccs=UTF-8") == 0 && f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fwprintf(f, L"%02d:%02d:%02d %s\n", st.wHour, st.wMinute, st.wSecond, buf);
        fclose(f);
    }
}

bool IsOwnWindow(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
}

void AppBarReserve(HWND h, UINT edge, int thick) {
    if (!h) return;
    APPBARDATA abd{ sizeof(abd) };
    abd.hWnd = h;
    abd.uCallbackMessage = WM_MD_APPBAR;
    if (!g_appbars.count(h)) {
        SHAppBarMessage(ABM_NEW, &abd);
        g_appbars.insert(h);
        SaveAppBars();
    }
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    abd.uEdge = edge;
    abd.rc = mi.rcMonitor;
    if (edge == ABE_TOP) abd.rc.bottom = abd.rc.top + thick;
    else abd.rc.top = abd.rc.bottom - thick;
    SHAppBarMessage(ABM_QUERYPOS, &abd);
    if (edge == ABE_TOP) abd.rc.bottom = abd.rc.top + thick;
    else abd.rc.top = abd.rc.bottom - thick;
    SHAppBarMessage(ABM_SETPOS, &abd);
}

void AppBarRemove(HWND h) {
    if (!g_appbars.erase(h)) return;
    APPBARDATA abd{ sizeof(abd) };
    abd.hWnd = h;
    SHAppBarMessage(ABM_REMOVE, &abd);
}

void OpenPath(const std::wstring& path) {
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

void OpenAjustes(const std::wstring& page) {
    std::wstring args = L"--ajustes" + (page.empty() ? L"" : L" " + page);
    std::wstring exe = install::IsInstalled() ? install::InstalledExe() : SelfPath();
    ShellExecuteW(nullptr, L"open", exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
}

void NotifyShell(WPARAM what) {
    if (HWND h = FindWindowW(L"WinMac.Ctrl", nullptr)) PostMessageW(h, WM_MD_RELOAD, what, 0);
}

void RunAsync(HWND target, std::function<void()> fn) {
    auto* p = new std::function<void()>(std::move(fn));
    if (!target || !PostMessageW(target, WM_MD_ASYNC, 0, (LPARAM)p)) delete p;
}

std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

void QuitWinMac() { PostMessageW(g_ctrl, WM_CLOSE, 0, 0); }

void RestartWinMac() {
    ShellExecuteW(nullptr, L"open", SelfPath().c_str(), L"--shell --wait", nullptr, SW_SHOWNORMAL);
    QuitWinMac();
}

void ToggleLaunchpad() { launchpad::Toggle(); }
void RefreshAll() { RelayoutAll(); }

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int) {
    g_inst = hi;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::wstring args = cmd ? cmd : L"";
    // funções da CRT com parâmetro inválido retornam erro em vez de derrubar o processo
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {});
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    cfg::Init();
    int rc = 0;
    if (args.find(L"--watchdog ") != std::wstring::npos) {
        RunWatchdog((DWORD)_wtoi(ArgAfter(args, L"--watchdog").c_str()));
    } else if (args.find(L"--restore-taskbar") != std::wstring::npos) {
        RestoreTaskbar();
    } else if (args.find(L"--uninstall") != std::wstring::npos) {
        rc = install::Uninstall();
    } else if (!gfx::Init()) {
        MessageBoxW(nullptr, L"Não foi possível iniciar o Direct2D.", L"WinMac", MB_ICONERROR);
        rc = 1;
    } else if (args.find(L"--apply-update") != std::wstring::npos) {
        rc = install::ApplyUpdate(ArgAfter(args, L"--apply-update"));
    } else if (args.find(L"--export-art") != std::wstring::npos) {
        art::ExportArt(ArgAfter(args, L"--export-art"));
    } else if (args.find(L"--shots") != std::wstring::npos) {
        RenderShots(ArgAfter(args, L"--shots"));
    } else if (args.find(L"--test-overlays") != std::wstring::npos) {
        // teste ao vivo só dos botões semáforo e do Alt+Tab (não mexe na barra de tarefas nem no Dock)
        int secs = std::max(3, _wtoi(ArgAfter(args, L"--test-overlays").c_str()));
        traffic::Init();
        switcher::Init();
        HWND t = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_inst, nullptr);
        SetTimer(t, 1, (secs / 2) * 1000, [](HWND h, UINT, UINT_PTR, DWORD) { KillTimer(h, 1); switcher::DebugShow(true); });
        SetTimer(t, 2, secs * 1000, [](HWND, UINT, UINT_PTR, DWORD) { PostQuitMessage(0); });
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        switcher::Shutdown();
        traffic::Shutdown();
    } else if (args.find(L"--ajustes") != std::wstring::npos) {
        rc = settings::Run(ArgAfter(args, L"--ajustes"));
    } else if (args.find(L"--welcome") != std::wstring::npos) {
        rc = welcome::Run(false);
    } else if (args.find(L"--setup") != std::wstring::npos || (!install::IsInstalledCopy() && args.find(L"--shell") == std::wstring::npos)) {
        rc = welcome::Run(true);   // rodando o WinMac-Setup.exe baixado: boas-vindas + instalação
    } else {
        if (args.find(L"--dump ") != std::wstring::npos) gfx::SetDumpDir(ArgAfter(args, L"--dump"));
        rc = RunShell(args);
    }
    // sai direto: evita liberar objetos gráficos estáticos depois do COM (travava na saída)
    ExitProcess((UINT)rc);
}
