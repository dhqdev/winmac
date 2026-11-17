#include "install.h"
#include "config.h"
#include "version.h"
#include "resids.h"
#include "art.h"
#include "gfx.h"
#include "sys.h"
#include <tlhelp32.h>
#include <appmodel.h>

namespace install {
namespace {
const wchar_t* kRun = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kUninst = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\WinMac";

struct CursorMap { const wchar_t* reg; const wchar_t* file; };
const CursorMap kCursors[] = {
    { L"Arrow", L"Pointer.cur" }, { L"Help", L"Help.cur" }, { L"AppStarting", L"Work.ani" }, { L"Wait", L"Busy.ani" },
    { L"Crosshair", L"Cross.cur" }, { L"IBeam", L"Text.cur" }, { L"NWPen", L"Handwriting.cur" }, { L"No", L"Unavailiable.cur" },
    { L"SizeNS", L"Vert.cur" }, { L"SizeWE", L"Horz.cur" }, { L"SizeNWSE", L"Dng1.cur" }, { L"SizeNESW", L"Dng2.cur" },
    { L"SizeAll", L"Move.cur" }, { L"UpArrow", L"Alternate.cur" }, { L"Hand", L"Link.cur" }, { L"Pin", L"Pin.cur" }, { L"Person", L"Person.cur" },
};

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    wchar_t* p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) r = p;
    CoTaskMemFree(p);
    return r;
}

bool WriteResource(int id, const std::wstring& path) {
    HRSRC r = FindResourceW(g_inst, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r) return false;
    HGLOBAL g = LoadResource(g_inst, r);
    DWORD sz = SizeofResource(g_inst, r);
    const void* data = g ? LockResource(g) : nullptr;
    if (!data) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(f, data, sz, &wr, nullptr);
    CloseHandle(f);
    return ok && wr == sz;
}

void RegStr(HKEY root, const wchar_t* key, const wchar_t* name, const std::wstring& v) {
    HKEY k;
    if (RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}
void RegDword(HKEY root, const wchar_t* key, const wchar_t* name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(k);
    }
}
std::wstring RegGetStr(HKEY root, const wchar_t* key, const wchar_t* name) {
    wchar_t buf[1024] = {};
    DWORD sz = sizeof(buf);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, buf, &sz) != ERROR_SUCCESS) return L"";
    return buf;
}

std::vector<DWORD> ProcessesOf(const std::wstring& exePath, DWORD except) {
    std::vector<DWORD> r;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return r;
    PROCESSENTRY32W pe{ sizeof(pe) };
    std::wstring want = Lower(exePath);
    std::wstring base = Lower(PathFindFileNameW(exePath.c_str()));
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (pe.th32ProcessID == except || Lower(pe.szExeFile) != base) continue;
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!p) continue;
        wchar_t path[MAX_PATH * 2];
        DWORD n = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(p, 0, path, &n) && Lower(path) == want) r.push_back(pe.th32ProcessID);
        CloseHandle(p);
    }
    CloseHandle(snap);
    return r;
}

void WaitOrKill(const std::wstring& exe, DWORD timeoutMs) {
    DWORD start = GetTickCount();
    while (GetTickCount() - start < timeoutMs) {
        if (ProcessesOf(exe, GetCurrentProcessId()).empty()) return;
        Sleep(150);
    }
    for (DWORD pid : ProcessesOf(exe, GetCurrentProcessId())) {
        if (HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid)) {
            TerminateProcess(p, 0);
            WaitForSingleObject(p, 2000);
            CloseHandle(p);
        }
    }
}

bool Shortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& args, const std::wstring& desc) {
    ComPtr<IShellLinkW> sl;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    sl->SetPath(target.c_str());
    sl->SetArguments(args.c_str());
    sl->SetDescription(desc.c_str());
    sl->SetIconLocation(target.c_str(), 0);
    ComPtr<IPersistFile> pf;
    if (FAILED(sl.As(&pf))) return false;
    return SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
}

std::wstring StartMenuDir() { return KnownFolder(FOLDERID_Programs) + L"\\WinMac"; }

void RunHidden(const std::wstring& cmdline, DWORD waitMs) {
    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring c = cmdline;
    if (CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, waitMs);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void RestartExplorer() {
    RunHidden(L"taskkill.exe /f /im explorer.exe", 8000);
    Sleep(1200);
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    ShellExecuteW(nullptr, L"open", (std::wstring(win) + L"\\explorer.exe").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    for (int i = 0; i < 40 && !FindWindowW(L"Shell_TrayWnd", nullptr); ++i) Sleep(200);
}

// Barra de tarefas do Windows no topo (fica escondida pelo WinMac): assim os menus da bandeja abrem perto da barra de menus.
bool TaskbarToTop(bool top) {
    const wchar_t* key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StuckRects3";
    BYTE v[64] = {};
    DWORD sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"Settings", RRF_RT_REG_BINARY, nullptr, v, &sz) != ERROR_SUCCESS || sz < 13) return false;
    BYTE want = top ? 1 : 3;
    if (v[12] == want) return false;
    if (top) cfg::SetInt(L"Backup", L"TaskbarEdge", v[12]);
    v[12] = want;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return false;
    RegSetValueExW(k, L"Settings", 0, REG_BINARY, v, sz);
    RegCloseKey(k);
    return true;
}

void MigrateFromMacDesk() {
    std::wstring old = KnownFolder(FOLDERID_LocalAppData) + L"\\MacDesk";
    std::wstring oldExe = old + L"\\MacDesk.exe";
    if (GetFileAttributesW(oldExe.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    // encerra a versão antiga (ela devolve a barra de tarefas ao sair)
    if (HWND w = FindWindowW(L"MacDesk.Ctrl", nullptr)) PostMessageW(w, WM_CLOSE, 0, 0);
    WaitOrKill(oldExe, 5000);
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRun, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        RegDeleteValueW(k, L"MacDesk");
        RegCloseKey(k);
    }
    std::wstring sm = KnownFolder(FOLDERID_Programs) + L"\\MacDesk";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((sm + L"\\*.lnk").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do DeleteFileW((sm + L"\\" + fd.cFileName).c_str());
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(sm.c_str());
    // leva as preferências do Dock e do clima
    if (cfg::Int(L"State", L"MigratedMacDesk", 0)) return;
    std::wstring oc = old + L"\\config.ini";
    auto get = [&](const wchar_t* sec, const wchar_t* key) {
        wchar_t b[8192] = {};
        GetPrivateProfileStringW(sec, key, L"", b, 8192, oc.c_str());
        return std::wstring(b);
    };
    std::wstring pinned = get(L"Dock", L"Pinned");
    if (!pinned.empty()) {
        size_t p = pinned.find(L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel");
        if (p != std::wstring::npos) pinned.replace(p, wcslen(L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel"), L"::ajustes");
        else pinned += L"|::ajustes";
        cfg::Set(L"Dock", L"Pinned", pinned);
    }
    for (const wchar_t* key : { L"Folders", L"IconSize", L"Magnification", L"MagnifyScale", L"ShowRecent" }) {
        std::wstring v = get(L"Dock", key);
        if (!v.empty()) cfg::Set(L"Dock", key, v);
    }
    for (const wchar_t* key : { L"City", L"Lat", L"Lon" }) {
        std::wstring v = get(L"Weather", key);
        if (!v.empty()) cfg::Set(L"Weather", key, v);
    }
    for (const wchar_t* key : { L"PosDate", L"PosMonth", L"PosClock", L"PosWeather" }) {
        std::wstring v = get(L"Widgets", key);
        if (!v.empty()) cfg::Set(L"Widgets", key, v);
    }
    cfg::SetInt(L"State", L"MigratedMacDesk", 1);
}

void InstallFont() {
    ComPtr<IDWriteFontCollection> coll;
    if (SUCCEEDED(gfx::dw->GetSystemFontCollection(&coll, TRUE))) {
        UINT32 i;
        BOOL e1 = FALSE, e2 = FALSE;
        coll->FindFamilyName(L"Inter", &i, &e1);
        coll->FindFamilyName(L"Inter Variable", &i, &e2);
        if (e1 || e2) return;
    }
    std::wstring dir = KnownFolder(FOLDERID_LocalAppData) + L"\\Microsoft\\Windows\\Fonts";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    std::wstring f = dir + L"\\InterVariable.ttf";
    if (!WriteResource(IDR_FONT_INTER, f)) return;
    RegStr(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", L"Inter Variable (TrueType)", f);
    AddFontResourceW(f.c_str());
    DWORD_PTR r;
    SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0, SMTO_ABORTIFHUNG, 300, &r);
    cfg::SetInt(L"Backup", L"FontInstalled", 1);
}

void WriteUninstallEntry() {
    std::wstring exe = InstalledExe();
    RegStr(HKEY_CURRENT_USER, kUninst, L"DisplayName", L"WinMac");
    RegStr(HKEY_CURRENT_USER, kUninst, L"DisplayVersion", WINMAC_VERSION);
    RegStr(HKEY_CURRENT_USER, kUninst, L"Publisher", L"dhqdev");
    RegStr(HKEY_CURRENT_USER, kUninst, L"DisplayIcon", exe + L",0");
    RegStr(HKEY_CURRENT_USER, kUninst, L"InstallLocation", Dir());
    RegStr(HKEY_CURRENT_USER, kUninst, L"UninstallString", L"\"" + exe + L"\" --uninstall");
    RegStr(HKEY_CURRENT_USER, kUninst, L"URLInfoAbout", L"https://github.com/" WINMAC_REPO);
    RegDword(HKEY_CURRENT_USER, kUninst, L"NoModify", 1);
    RegDword(HKEY_CURRENT_USER, kUninst, L"NoRepair", 1);
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fa)) RegDword(HKEY_CURRENT_USER, kUninst, L"EstimatedSize", fa.nFileSizeLow / 1024 + 64);
}

bool CopySelfTo(const std::wstring& dst) {
    std::wstring self = SelfPath();
    if (_wcsicmp(self.c_str(), dst.c_str()) == 0) return true;
    for (int i = 0; i < 30; ++i) {
        if (CopyFileW(self.c_str(), dst.c_str(), FALSE)) return true;
        Sleep(300);
    }
    return false;
}
}  // namespace

std::wstring Dir() { return KnownFolder(FOLDERID_LocalAppData) + L"\\WinMac"; }
std::wstring InstalledExe() { return Dir() + L"\\WinMac.exe"; }
bool IsInstalledCopy() { return _wcsicmp(SelfPath().c_str(), InstalledExe().c_str()) == 0; }
bool IsInstalled() { return GetFileAttributesW(InstalledExe().c_str()) != INVALID_FILE_ATTRIBUTES; }

void StopShell(DWORD timeoutMs) {
    if (HWND w = FindWindowW(L"WinMac.Ctrl", nullptr)) PostMessageW(w, WM_CLOSE, 0, 0);
    WaitOrKill(InstalledExe(), timeoutMs);
}

void LaunchShell() {
    std::wstring exe = InstalledExe();
    std::wstring cmd = L"\"" + exe + L"\" --wait";
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, Dir().c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void SetAutostart(bool on) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRun, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring v = L"\"" + InstalledExe() + L"\"";
        RegSetValueExW(k, L"WinMac", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else RegDeleteValueW(k, L"WinMac");
    RegCloseKey(k);
}

bool Autostart() { return !RegGetStr(HKEY_CURRENT_USER, kRun, L"WinMac").empty(); }

void ApplyCursors(bool mac) {
    const wchar_t* key = L"Control Panel\\Cursors";
    if (mac) {
        std::wstring cd = Dir() + L"\\cursors";
        CreateDirectoryW(cd.c_str(), nullptr);
        if (!cfg::Int(L"Backup", L"CursorsSaved", 0)) {
            for (auto& m : kCursors) cfg::Set(L"Backup", (std::wstring(L"Cursor_") + m.reg).c_str(), L"\"" + RegGetStr(HKEY_CURRENT_USER, key, m.reg) + L"\"");
            cfg::Set(L"Backup", L"Cursor_Scheme", L"\"" + RegGetStr(HKEY_CURRENT_USER, key, nullptr) + L"\"");
            cfg::SetInt(L"Backup", L"CursorsSaved", 1);
        }
        int i = 0;
        for (auto& m : kCursors) {
            std::wstring f = cd + L"\\" + m.file;
            WriteResource(IDR_CURSOR_FIRST + i++, f);
            RegStr(HKEY_CURRENT_USER, key, m.reg, f);
        }
        RegStr(HKEY_CURRENT_USER, key, nullptr, L"WinMac");
    } else {
        for (auto& m : kCursors) {
            std::wstring v = cfg::Str(L"Backup", (std::wstring(L"Cursor_") + m.reg).c_str(), L"");
            if (v.find(L"\\WinMac\\cursors") != std::wstring::npos) v.clear();
            RegStr(HKEY_CURRENT_USER, key, m.reg, v);
        }
        std::wstring sch = cfg::Str(L"Backup", L"Cursor_Scheme", L"");
        RegStr(HKEY_CURRENT_USER, key, nullptr, sch == L"WinMac" ? L"" : sch);
    }
    cfg::SetInt(L"Appearance", L"MacCursors", mac ? 1 : 0);
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

bool ApplyWallpaperFile(const std::wstring& path) {
    wchar_t cur[MAX_PATH] = {};
    SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, cur, 0);
    if (cfg::Str(L"Backup", L"Wallpaper").empty() && cur[0] && !wcsstr(cur, L"\\WinMac\\")) cfg::Set(L"Backup", L"Wallpaper", cur);
    RegDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\DesktopSpotlight\\Settings", L"EnabledState", 0);
    RegDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Wallpapers", L"BackgroundType", 0);
    RegStr(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallpaperStyle", L"10");
    RegStr(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"TileWallpaper", L"0");
    cfg::Set(L"Screen", L"Wallpaper", path);
    return SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)path.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE) != FALSE;
}

bool ApplyWallpaper(int preset) {
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    w = std::max(w, 1280);
    h = std::max(h, 720);
    std::wstring path = Dir() + L"\\wallpaper-" + std::to_wstring(preset) + L".jpg";
    CreateDirectoryW(Dir().c_str(), nullptr);
    if (!art::SaveWallpaper(preset, w, h, path)) return false;
    cfg::SetInt(L"Screen", L"WallpaperPreset", preset);
    return ApplyWallpaperFile(path);
}

void ApplyTheme(int theme) {
    cfg::SetInt(L"Appearance", L"Theme", theme);
    bool dark = theme == 1;
    if (theme == 2) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        dark = st.wHour < 7 || st.wHour >= 19;
    }
    if (dark != gfx::DarkMode() || theme != 2) sys::SetDarkMode(dark);
}

void DisableAds() {
    const wchar_t* c = L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager";
    for (const wchar_t* n : { L"ContentDeliveryAllowed", L"FeatureManagementEnabled", L"OemPreInstalledAppsEnabled", L"PreInstalledAppsEnabled",
                              L"PreInstalledAppsEverEnabled", L"SilentInstalledAppsEnabled", L"SoftLandingEnabled", L"SystemPaneSuggestionsEnabled",
                              L"SubscribedContentEnabled", L"SubscribedContent-310093Enabled", L"SubscribedContent-338388Enabled",
                              L"SubscribedContent-338389Enabled", L"SubscribedContent-338393Enabled", L"SubscribedContent-353694Enabled",
                              L"SubscribedContent-353696Enabled", L"SubscribedContent-353698Enabled", L"RotatingLockScreenOverlayEnabled" })
        RegDword(HKEY_CURRENT_USER, c, n, 0);
    RegDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\UserProfileEngagement", L"ScoobeSystemSettingEnabled", 0);
    RegDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"ShowSyncProviderNotifications", 0);
    RegDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", 0);
    RegDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Notifications\\Settings\\Windows.SystemToast.Suggested", L"Enabled", 0);
}

const std::vector<PaidApp>& PaidApps() {
    static std::vector<PaidApp> v = {
        { L"Microsoft.MicrosoftOfficeHub_8wekyb3d8bbwe", L"Microsoft 365 (Office)", L"vitrine da assinatura — Word, Excel e PowerPoint continuam" },
        { L"Microsoft.GamingApp_8wekyb3d8bbwe", L"Xbox", L"Game Pass (assinatura)" },
        { L"Microsoft.XboxApp_8wekyb3d8bbwe", L"Xbox Console Companion", L"Game Pass (assinatura)" },
        { L"Microsoft.MicrosoftSolitaireCollection_8wekyb3d8bbwe", L"Microsoft Solitaire Collection", L"anúncios e versão premium" },
        { L"Clipchamp.Clipchamp_yxz26nhyzhsrt", L"Clipchamp", L"recursos pagos" },
        { L"Microsoft.SkypeApp_kzf8qxf38zg5c", L"Skype", L"créditos pagos (serviço encerrado)" },
        { L"Disney.37853FC22B2CE_6rarf9sa4v8jt", L"Disney+", L"assinatura" },
        { L"4DF9E0F8.Netflix_mcm4njqhnhss8", L"Netflix", L"assinatura" },
        { L"AmazonVideo.PrimeVideo_pwbj9vvecjh7j", L"Prime Video", L"assinatura" },
        { L"SpotifyAB.SpotifyMusic_zpdnekdrzrea0", L"Spotify", L"assinatura Premium (o app tem plano grátis)" },
        { L"king.com.CandyCrushSaga_kgqvnymyfvs32", L"Candy Crush Saga", L"compras no jogo" },
        { L"king.com.CandyCrushSodaSaga_kgqvnymyfvs32", L"Candy Crush Soda Saga", L"compras no jogo" },
        { L"king.com.CandyCrushFriends_kgqvnymyfvs32", L"Candy Crush Friends", L"compras no jogo" },
        { L"5A894077.McAfeeSecurity_wafk5atnkzcwy", L"McAfee", L"antivírus de teste pago" },
        { L"Microsoft.Getstarted_8wekyb3d8bbwe", L"Dicas", L"propaganda do Windows" },
    };
    return v;
}

bool IsPackageInstalled(const wchar_t* family) {
    UINT32 count = 0, len = 0;
    LONG r = GetPackagesByPackageFamily(family, &count, nullptr, &len, nullptr);
    return (r == ERROR_INSUFFICIENT_BUFFER || r == ERROR_SUCCESS) && count > 0;
}

bool RemovePackages(const std::vector<std::wstring>& families) {
    if (families.empty()) return true;
    std::wstring script;
    for (auto& f : families) {
        std::wstring name = f.substr(0, f.find(L'_'));
        bool safe = !name.empty() && std::all_of(name.begin(), name.end(), [](wchar_t ch) { return iswalnum(ch) || ch == L'.' || ch == L'-'; });
        if (!safe) continue;
        script += L"Get-AppxPackage -Name '" + name + L"' | Remove-AppxPackage -ErrorAction SilentlyContinue; ";
    }
    if (script.empty()) return false;
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    RunHidden(L"\"" + std::wstring(sys) + L"\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"" + script + L"\"", 180000);
    return true;
}

void Migrate() {
    // versões futuras: ajustes de configuração entre versões entram aqui
    if (cfg::Str(L"State", L"ConfigVersion") != WINMAC_VERSION) cfg::Set(L"State", L"ConfigVersion", WINMAC_VERSION);
}

bool Install(const Options& o, const std::function<void(int, int, const std::wstring&)>& progress, std::wstring* err) {
    const int N = 7;
    auto step = [&](int i, const wchar_t* t) { if (progress) progress(i, N, t); };
    std::wstring dir = Dir();
    step(0, L"Preparando…");
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    cfg::Init(dir);
    MigrateFromMacDesk();
    StopShell(6000);

    step(1, L"Copiando o WinMac…");
    if (!CopySelfTo(InstalledExe())) {
        if (err) *err = L"Não consegui copiar o WinMac para " + dir + L".";
        return false;
    }
    cfg::SetInt(L"Windows", L"TrafficLights", o.traffic);
    cfg::SetInt(L"Windows", L"Switcher", o.switcher);
    cfg::SetInt(L"Dock", L"AutoHide", o.autohide);
    cfg::SetInt(L"Widgets", L"Enabled", o.widgets);
    cfg::SetInt(L"Appearance", L"Accent", o.accent);

    step(2, L"Fonte e cursores…");
    InstallFont();
    if (o.cursors) ApplyCursors(true);

    step(3, L"Papel de parede e aparência…");
    if (o.useWallpaper) ApplyWallpaper(o.wallpaper);
    ApplyTheme(o.theme);

    step(4, L"Ajustando o Windows…");
    SetAutostart(true);
    std::wstring sm = StartMenuDir();
    SHCreateDirectoryExW(nullptr, sm.c_str(), nullptr);
    Shortcut(sm + L"\\WinMac.lnk", InstalledExe(), L"", L"Inicia o WinMac");
    Shortcut(sm + L"\\Ajustes do WinMac.lnk", InstalledExe(), L"--ajustes", L"Ajustes do Sistema do WinMac");
    WriteUninstallEntry();
    if (o.debloat) DisableAds();
    bool explorer = o.taskbarTop && TaskbarToTop(true);

    step(5, o.debloat ? L"Removendo apps pagos e propagandas…" : L"Quase lá…");
    if (o.debloat) {
        std::vector<std::wstring> rm;
        for (auto& a : PaidApps())
            if (wcscmp(a.name, L"Spotify") != 0 && IsPackageInstalled(a.family)) rm.push_back(a.family);
        RemovePackages(rm);
    }
    if (explorer) RestartExplorer();

    step(6, L"Iniciando o WinMac…");
    cfg::Set(L"State", L"InstalledVersion", WINMAC_VERSION);
    cfg::SetInt(L"State", L"Onboarded", 1);
    LaunchShell();
    step(7, L"Pronto!");
    return true;
}

int ApplyUpdate(const std::wstring& dir) {
    (void)dir;
    StopShell(6000);
    if (!CopySelfTo(InstalledExe())) {
        MessageBoxW(nullptr, L"Não consegui substituir o WinMac pela versão nova. Feche o WinMac e tente de novo.", L"WinMac", MB_ICONERROR | MB_TOPMOST);
        LaunchShell();
        return 1;
    }
    cfg::Init(Dir());
    cfg::Set(L"Update", L"JustUpdated", WINMAC_VERSION);
    cfg::Set(L"Update", L"Available", L"");
    cfg::Set(L"State", L"InstalledVersion", WINMAC_VERSION);
    WriteUninstallEntry();
    LaunchShell();
    return 0;
}

int Uninstall() {
    if (MessageBoxW(nullptr, L"Remover o WinMac deste computador?\n\nA barra de tarefas, os cursores e o papel de parede anteriores voltam.",
                    L"Desinstalar o WinMac", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) != IDYES)
        return 0;
    cfg::Init(Dir());
    StopShell(6000);
    if (cfg::Int(L"Appearance", L"MacCursors", 0)) ApplyCursors(false);
    std::wstring wp = cfg::Str(L"Backup", L"Wallpaper");
    if (!wp.empty() && GetFileAttributesW(wp.c_str()) != INVALID_FILE_ATTRIBUTES)
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)wp.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    SetAutostart(false);
    std::wstring sm = StartMenuDir();
    DeleteFileW((sm + L"\\WinMac.lnk").c_str());
    DeleteFileW((sm + L"\\Ajustes do WinMac.lnk").c_str());
    RemoveDirectoryW(sm.c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, kUninst);
    int edge = cfg::Int(L"Backup", L"TaskbarEdge", -1);
    if (edge == 3 && TaskbarToTop(false)) RestartExplorer();
    // apaga a pasta depois que este processo sair
    std::wstring d = Dir();
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(sys) + L"\\cmd.exe\" /c ping 127.0.0.1 -n 3 >nul & rmdir /s /q \"" + d + L"\"";
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, sys, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    MessageBoxW(nullptr, L"O WinMac foi removido. Obrigado por experimentar! 💧", L"WinMac", MB_ICONINFORMATION | MB_TOPMOST);
    return 0;
}
}  // namespace install
