#include "sysx.h"
#include "config.h"
#include <powrprof.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

namespace sysx {
namespace {
const GUID kVideoSub = { 0x7516b95f, 0xf776, 0x4464, { 0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99 } };
const GUID kVideoIdle = { 0x3c0bc021, 0xc8a8, 0x4e07, { 0xa9, 0x73, 0x6b, 0x14, 0xcb, 0xcb, 0x2b, 0x7e } };
const GUID kSleepSub = { 0x238c9fa8, 0x0aad, 0x41ed, { 0x83, 0xf4, 0x97, 0xbe, 0x24, 0x2c, 0x8f, 0x20 } };
const GUID kStandbyIdle = { 0x29f6c1db, 0x86da, 0x48c5, { 0x9f, 0xdb, 0xf2, 0xb6, 0x7b, 0x1f, 0x44, 0xda } };
const GUID kSaverSub = { 0xde830923, 0xa562, 0x41af, { 0xa0, 0x86, 0xe3, 0xa2, 0xc6, 0xba, 0xd2, 0xda } };
const GUID kSaverThreshold = { 0xe69653ca, 0xcf7f, 0x4f05, { 0xaa, 0x73, 0xcb, 0x83, 0x3f, 0xa9, 0x0a, 0xd4 } };
const GUID kOverlayEfficiency = { 0x961cc777, 0x2547, 0x4f9d, { 0x81, 0x74, 0x7d, 0x86, 0x18, 0x1b, 0x8a, 0x7a } };
const GUID kOverlayPerformance = { 0xded574b5, 0x45a0, 0x4f42, { 0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38 } };

std::wstring RootName(HKEY r) { return r == HKEY_LOCAL_MACHINE ? L"HKLM" : L"HKCU"; }

// guarda o valor original uma única vez: [RegBackup] "HKCU\chave|nome" = d:123 | s:texto | none
void Remember(HKEY root, const wchar_t* key, const wchar_t* name, bool isString) {
    std::wstring id = RootName(root) + L"\\" + key + L"|" + (name ? name : L"");
    if (!cfg::Str(L"RegBackup", id.c_str(), L"").empty()) return;
    std::wstring v = L"none";
    if (isString) {
        wchar_t buf[1024];
        DWORD sz = sizeof(buf);
        if (RegGetValueW(root, key, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, buf, &sz) == ERROR_SUCCESS) v = L"s:" + std::wstring(buf);
    } else {
        DWORD d = 0, sz = sizeof(d);
        if (RegGetValueW(root, key, name, RRF_RT_REG_DWORD, nullptr, &d, &sz) == ERROR_SUCCESS) v = L"d:" + std::to_wstring(d);
    }
    cfg::Set(L"RegBackup", id.c_str(), L"\"" + v + L"\"");
}

void RememberSpi(const wchar_t* name, int v) {
    if (cfg::Str(L"SpiBackup", name, L"").empty()) cfg::SetInt(L"SpiBackup", name, v);
}

GUID ActiveScheme() {
    GUID* g = nullptr;
    GUID r{};
    if (PowerGetActiveScheme(nullptr, &g) == ERROR_SUCCESS && g) { r = *g; LocalFree(g); }
    return r;
}

int ReadPower(bool ac, const GUID& sub, const GUID& set) {
    GUID sch = ActiveScheme();
    DWORD v = 0;
    if (ac) PowerReadACValueIndex(nullptr, &sch, &sub, &set, &v);
    else PowerReadDCValueIndex(nullptr, &sch, &sub, &set, &v);
    return (int)v;
}

void WritePower(bool ac, const GUID& sub, const GUID& set, DWORD v, const wchar_t* backupName) {
    std::wstring bn = std::wstring(backupName) + (ac ? L"_AC" : L"_DC");
    if (cfg::Str(L"PowerBackup", bn.c_str(), L"").empty()) cfg::SetInt(L"PowerBackup", bn.c_str(), ReadPower(ac, sub, set));
    GUID sch = ActiveScheme();
    if (ac) PowerWriteACValueIndex(nullptr, &sch, &sub, &set, v);
    else PowerWriteDCValueIndex(nullptr, &sch, &sub, &set, v);
    PowerSetActiveScheme(nullptr, &sch);
}

// Interface não documentada (usada pelo próprio Windows) para escolher o dispositivo de som padrão.
MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
const CLSID kPolicyConfigClient = { 0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 } };
}  // namespace

void SetDword(HKEY root, const wchar_t* key, const wchar_t* name, DWORD v) {
    Remember(root, key, name, false);
    HKEY k;
    if (RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(k);
    }
}

void SetString(HKEY root, const wchar_t* key, const wchar_t* name, const std::wstring& v) {
    Remember(root, key, name, true);
    HKEY k;
    if (RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}

DWORD GetDword(HKEY root, const wchar_t* key, const wchar_t* name, DWORD def) {
    DWORD v = def, sz = sizeof(v);
    if (RegGetValueW(root, key, name, RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS) return def;
    return v;
}

void RestoreAll() {
    // registro
    std::wstring all(65536, L'\0');
    DWORD n = GetPrivateProfileSectionW(L"RegBackup", all.data(), (DWORD)all.size(), cfg::Path().c_str());
    all.resize(n);
    for (size_t p = 0; p < all.size();) {
        std::wstring line = all.c_str() + p;
        p += line.size() + 1;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring id = line.substr(0, eq), val = line.substr(eq + 1);
        if (val.size() >= 2 && val.front() == L'"') val = val.substr(1, val.size() - 2);
        size_t bar = id.rfind(L'|'), sl = id.find(L'\\');
        if (bar == std::wstring::npos || sl == std::wstring::npos) continue;
        HKEY root = id.substr(0, sl) == L"HKLM" ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        std::wstring key = id.substr(sl + 1, bar - sl - 1), name = id.substr(bar + 1);
        const wchar_t* nm = name.empty() ? nullptr : name.c_str();
        HKEY k;
        if (RegOpenKeyExW(root, key.c_str(), 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) continue;
        if (val == L"none") RegDeleteValueW(k, nm);
        else if (val.rfind(L"d:", 0) == 0) {
            DWORD d = (DWORD)_wtoi64(val.c_str() + 2);
            RegSetValueExW(k, nm, 0, REG_DWORD, (const BYTE*)&d, sizeof(d));
        } else if (val.rfind(L"s:", 0) == 0) {
            std::wstring s = val.substr(2);
            RegSetValueExW(k, nm, 0, REG_SZ, (const BYTE*)s.c_str(), (DWORD)((s.size() + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(k);
    }
    // mouse
    if (!cfg::Str(L"SpiBackup", L"MouseSpeed", L"").empty()) {
        int v = cfg::Int(L"SpiBackup", L"MouseSpeed", 10);
        SystemParametersInfoW(SPI_SETMOUSESPEED, 0, (void*)(INT_PTR)v, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    }
    if (!cfg::Str(L"SpiBackup", L"WheelLines", L"").empty())
        SystemParametersInfoW(SPI_SETWHEELSCROLLLINES, cfg::Int(L"SpiBackup", L"WheelLines", 3), nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    if (!cfg::Str(L"SpiBackup", L"Swap", L"").empty())
        SystemParametersInfoW(SPI_SETMOUSEBUTTONSWAP, cfg::Int(L"SpiBackup", L"Swap", 0), nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    if (!cfg::Str(L"SpiBackup", L"DoubleClick", L"").empty())
        SystemParametersInfoW(SPI_SETDOUBLECLICKTIME, cfg::Int(L"SpiBackup", L"DoubleClick", 500), nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    // energia
    struct P { const wchar_t* n; const GUID* sub; const GUID* set; };
    P ps[] = { { L"ScreenOff", &kVideoSub, &kVideoIdle }, { L"Sleep", &kSleepSub, &kStandbyIdle }, { L"Saver", &kSaverSub, &kSaverThreshold } };
    GUID sch = ActiveScheme();
    for (auto& p : ps)
        for (int ac = 0; ac < 2; ++ac) {
            std::wstring bn = std::wstring(p.n) + (ac ? L"_AC" : L"_DC");
            if (cfg::Str(L"PowerBackup", bn.c_str(), L"").empty()) continue;
            DWORD v = (DWORD)cfg::Int(L"PowerBackup", bn.c_str(), 0);
            if (ac) PowerWriteACValueIndex(nullptr, &sch, p.sub, p.set, v);
            else PowerWriteDCValueIndex(nullptr, &sch, p.sub, p.set, v);
        }
    PowerSetActiveScheme(nullptr, &sch);
    if (!cfg::Str(L"PowerBackup", L"Mode", L"").empty()) SetPowerMode(cfg::Int(L"PowerBackup", L"Mode", 1));
    DWORD_PTR r;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet", SMTO_ABORTIFHUNG, 300, &r);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ShellState", SMTO_ABORTIFHUNG, 300, &r);
}

// ---------------- energia ----------------
Power GetPower() {
    Power p;
    SYSTEM_POWER_STATUS s{};
    if (GetSystemPowerStatus(&s)) {
        p.hasBattery = !(s.BatteryFlag & 128) && s.BatteryFlag != 255;
        p.pct = s.BatteryLifePercent <= 100 ? s.BatteryLifePercent : 100;
        p.plugged = s.ACLineStatus == 1;
        p.charging = (s.BatteryFlag & 8) != 0;
        p.saver = s.SystemStatusFlag != 0;
        p.secondsLeft = s.BatteryLifeTime == (DWORD)-1 ? -1 : (int)s.BatteryLifeTime;
    }
    return p;
}

int PowerMode() {
    typedef DWORD(WINAPI * Get)(GUID*);
    static auto f = (Get)GetProcAddress(LoadLibraryW(L"powrprof.dll"), "PowerGetEffectiveOverlayScheme");
    GUID g{};
    if (!f || f(&g) != ERROR_SUCCESS) return 1;
    if (IsEqualGUID(g, kOverlayEfficiency)) return 0;
    if (IsEqualGUID(g, kOverlayPerformance)) return 2;
    return 1;
}

bool SetPowerMode(int mode) {
    typedef DWORD(WINAPI * Set)(const GUID*);
    static auto f = (Set)GetProcAddress(LoadLibraryW(L"powrprof.dll"), "PowerSetActiveOverlayScheme");
    if (!f) return false;
    if (cfg::Str(L"PowerBackup", L"Mode", L"").empty()) cfg::SetInt(L"PowerBackup", L"Mode", PowerMode());
    GUID g = mode == 0 ? kOverlayEfficiency : mode == 2 ? kOverlayPerformance : GUID{};
    return f(&g) == ERROR_SUCCESS;
}

int ScreenOffMinutes(bool ac) { return ReadPower(ac, kVideoSub, kVideoIdle) / 60; }
void SetScreenOffMinutes(bool ac, int m) { WritePower(ac, kVideoSub, kVideoIdle, (DWORD)m * 60, L"ScreenOff"); }
int SleepMinutes(bool ac) { return ReadPower(ac, kSleepSub, kStandbyIdle) / 60; }
void SetSleepMinutes(bool ac, int m) { WritePower(ac, kSleepSub, kStandbyIdle, (DWORD)m * 60, L"Sleep"); }
int SaverThreshold() { return ReadPower(false, kSaverSub, kSaverThreshold); }
void SetSaverThreshold(int pct) { WritePower(false, kSaverSub, kSaverThreshold, (DWORD)std::clamp(pct, 0, 100), L"Saver"); }

// ---------------- som ----------------
std::vector<AudioDev> OutputDevices() {
    std::vector<AudioDev> out;
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return out;
    std::wstring defId;
    ComPtr<IMMDevice> def;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &def))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(def->GetId(&id))) { defId = id; CoTaskMemFree(id); }
    }
    ComPtr<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col))) return out;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        ComPtr<IMMDevice> d;
        if (FAILED(col->Item(i, &d))) continue;
        AudioDev a;
        LPWSTR id = nullptr;
        if (SUCCEEDED(d->GetId(&id))) { a.id = id; CoTaskMemFree(id); }
        ComPtr<IPropertyStore> ps;
        if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps))) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR) a.name = pv.pwszVal;
            PropVariantClear(&pv);
        }
        a.isDefault = a.id == defId;
        out.push_back(a);
    }
    return out;
}

bool SetDefaultOutput(const std::wstring& id) {
    ComPtr<IPolicyConfig> pc;
    if (FAILED(CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig), (void**)pc.GetAddressOf()))) return false;
    bool ok = SUCCEEDED(pc->SetDefaultEndpoint(id.c_str(), eConsole));
    pc->SetDefaultEndpoint(id.c_str(), eMultimedia);
    pc->SetDefaultEndpoint(id.c_str(), eCommunications);
    return ok;
}

// ---------------- notificações ----------------
static const wchar_t* kPush = L"Software\\Microsoft\\Windows\\CurrentVersion\\PushNotifications";
bool ToastsEnabled() { return GetDword(HKEY_CURRENT_USER, kPush, L"ToastEnabled", 1) != 0; }
void SetToasts(bool on) {
    SetDword(HKEY_CURRENT_USER, kPush, L"ToastEnabled", on ? 1 : 0);
    DWORD_PTR r;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Policy", SMTO_ABORTIFHUNG, 300, &r);
}

// ---------------- mouse ----------------
int MouseSpeed() { int v = 10; SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &v, 0); return v; }
void SetMouseSpeed(int v) {
    RememberSpi(L"MouseSpeed", MouseSpeed());
    SystemParametersInfoW(SPI_SETMOUSESPEED, 0, (void*)(INT_PTR)std::clamp(v, 1, 20), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}
int WheelLines() { UINT v = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &v, 0); return (int)v; }
void SetWheelLines(int v) {
    RememberSpi(L"WheelLines", WheelLines());
    SystemParametersInfoW(SPI_SETWHEELSCROLLLINES, std::clamp(v, 1, 20), nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}
bool ButtonsSwapped() { return GetSystemMetrics(SM_SWAPBUTTON) != 0; }
void SetButtonsSwapped(bool v) {
    RememberSpi(L"Swap", ButtonsSwapped() ? 1 : 0);
    SystemParametersInfoW(SPI_SETMOUSEBUTTONSWAP, v ? 1 : 0, nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}
int DoubleClickMs() { return (int)GetDoubleClickTime(); }
void SetDoubleClickMs(int ms) {
    RememberSpi(L"DoubleClick", DoubleClickMs());
    SystemParametersInfoW(SPI_SETDOUBLECLICKTIME, std::clamp(ms, 200, 900), nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

// ---------------- armazenamento ----------------
std::vector<Drive> Drives() {
    std::vector<Drive> out;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        wchar_t root[] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
        UINT t = GetDriveTypeW(root);
        if (t != DRIVE_FIXED && t != DRIVE_REMOVABLE) continue;
        Drive d;
        d.root = root;
        ULARGE_INTEGER fa, tot, fr;
        if (!GetDiskFreeSpaceExW(root, &fa, &tot, &fr)) continue;
        d.total = tot.QuadPart;
        d.free = fr.QuadPart;
        wchar_t label[MAX_PATH] = {};
        GetVolumeInformationW(root, label, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
        d.label = label[0] ? label : (t == DRIVE_FIXED ? L"Disco Local" : L"Unidade removível");
        out.push_back(d);
    }
    return out;
}

uint64_t FolderSize(const std::wstring& dir) {
    uint64_t total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) total += FolderSize(dir + L"\\" + fd.cFileName);
        else total += ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return total;
}

static uint64_t CleanDir(const std::wstring& dir, const FILETIME& older, int depth) {
    uint64_t freed = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        std::wstring p = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < 8) freed += CleanDir(p, older, depth + 1);
            RemoveDirectoryW(p.c_str());   // só some se ficou vazia
        } else if (CompareFileTime(&fd.ftLastWriteTime, &older) < 0) {
            uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if (DeleteFileW(p.c_str())) freed += sz;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return freed;
}

uint64_t CleanTemp() {
    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (!n || n > MAX_PATH) return 0;
    std::wstring dir = tmp;
    if (!dir.empty() && dir.back() == L'\\') dir.pop_back();
    if (Lower(dir).find(L"\\temp") == std::wstring::npos) return 0;   // segurança: só a pasta temporária
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER u{ { now.dwLowDateTime, now.dwHighDateTime } };
    u.QuadPart -= 24ull * 3600 * 10000000;
    FILETIME older{ u.LowPart, u.HighPart };
    return CleanDir(dir, older, 0);
}

std::wstring Bytes(uint64_t b) {
    wchar_t buf[64];
    if (b >= 1ull << 30) swprintf_s(buf, L"%.1f GB", b / 1073741824.0);
    else if (b >= 1ull << 20) swprintf_s(buf, L"%.1f MB", b / 1048576.0);
    else swprintf_s(buf, L"%.0f KB", b / 1024.0);
    std::wstring s = buf;
    for (auto& ch : s) if (ch == L'.') ch = L',';
    return s;
}

std::wstring Resolution() {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm);
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), MDT_EFFECTIVE_DPI, &dx, &dy);
    return std::to_wstring(dm.dmPelsWidth) + L" × " + std::to_wstring(dm.dmPelsHeight) + L" · " + std::to_wstring(dx * 100 / 96) + L"% · " +
           std::to_wstring(dm.dmDisplayFrequency) + L" Hz";
}
}  // namespace sysx
