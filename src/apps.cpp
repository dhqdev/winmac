#include "apps.h"
#include "gfx.h"
#include <appmodel.h>

namespace apps {
static std::unordered_map<HWND, WinInfo> s_cache;

static bool IsCloaked(HWND h) {
    DWORD c = 0;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &c, sizeof(c))) && c != 0;
}

bool IsTaskWindow(HWND h) {
    if (!IsWindowVisible(h) || IsOwnWindow(h)) return false;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (!(ex & WS_EX_APPWINDOW)) {
        if (ex & WS_EX_TOOLWINDOW) return false;
        if (GetWindow(h, GW_OWNER)) return false;
        if (ex & WS_EX_NOACTIVATE) return false;
    }
    if (IsCloaked(h)) return false;
    wchar_t cls[96] = {};
    GetClassNameW(h, cls, 96);
    static const wchar_t* skip[] = { L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW",
                                     L"Windows.UI.Core.CoreWindow", L"NotifyIconOverflowWindow", L"TopLevelWindowForOverflowXamlIsland" };
    for (auto s : skip) if (!wcscmp(cls, s)) return false;
    if (!IsIconic(h)) {
        RECT r;
        GetWindowRect(h, &r);
        if (r.right - r.left <= 1 || r.bottom - r.top <= 1) return false;
    }
    return true;
}

std::wstring ProcessPath(DWORD pid) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return L"";
    wchar_t buf[MAX_PATH * 2];
    DWORD n = MAX_PATH * 2;
    std::wstring r;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) r.assign(buf, n);
    CloseHandle(p);
    return r;
}

std::wstring ProcessAumid(DWORD pid) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return L"";
    wchar_t buf[APPLICATION_USER_MODEL_ID_MAX_LENGTH];
    UINT32 n = APPLICATION_USER_MODEL_ID_MAX_LENGTH;
    std::wstring r;
    if (GetApplicationUserModelId(p, &n, buf) == ERROR_SUCCESS) r = buf;
    CloseHandle(p);
    return r;
}

static std::wstring WindowAumid(HWND h) {
    std::wstring r;
    ComPtr<IPropertyStore> ps;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(h, IID_PPV_ARGS(&ps)))) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(ps->GetValue(PKEY_AppUserModel_ID, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) r = pv.pwszVal;
        PropVariantClear(&pv);
    }
    return r;
}

const WinInfo& Describe(HWND h) {
    auto it = s_cache.find(h);
    if (it != s_cache.end() && IsWindow(h)) return it->second;
    WinInfo w;
    w.hwnd = h;
    GetWindowThreadProcessId(h, &w.pid);
    w.exe = Lower(ProcessPath(w.pid));
    w.aumid = WindowAumid(h);
    w.explicitAumid = !w.aumid.empty();
    if (w.aumid.empty()) {
        if (w.exe.size() >= 24 && w.exe.compare(w.exe.size() - 24, 24, L"applicationframehost.exe") == 0) {
            // App UWP: o processo real está na CoreWindow filha
            struct Ctx { DWORD host; DWORD pid; } ctx{ w.pid, 0 };
            EnumChildWindows(h, [](HWND c, LPARAM lp) -> BOOL {
                auto* x = (Ctx*)lp;
                DWORD p = 0;
                GetWindowThreadProcessId(c, &p);
                if (p != x->host) { x->pid = p; return FALSE; }
                return TRUE;
            }, (LPARAM)&ctx);
            if (ctx.pid) {
                w.aumid = ProcessAumid(ctx.pid);
                w.exe = Lower(ProcessPath(ctx.pid));
            } else {
                // a CoreWindow ainda não foi anexada (ou o app está minimizado): não guarda no cache
                static WinInfo tmp;
                tmp = w;
                return tmp;
            }
        } else {
            w.aumid = ProcessAumid(w.pid);
        }
    }
    auto& slot = s_cache[h];
    slot = w;
    return slot;
}

void ForgetWindow(HWND h) { s_cache.erase(h); }

std::vector<HWND> TaskWindows() {
    std::vector<HWND> v;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        if (IsTaskWindow(h)) ((std::vector<HWND>*)lp)->push_back(h);
        return TRUE;
    }, (LPARAM)&v);
    // limpa cache de janelas que morreram
    if (s_cache.size() > 300) {
        for (auto it = s_cache.begin(); it != s_cache.end();) {
            if (!IsWindow(it->first)) it = s_cache.erase(it); else ++it;
        }
    }
    return v;
}

static bool IsDirectPath(const std::wstring& s) {
    return (s.size() > 2 && s[1] == L':' && (s[2] == L'\\' || s[2] == L'/')) || StartsWithI(s, L"\\\\") ||
           StartsWithI(s, L"shell:") || StartsWithI(s, L"::");
}

std::wstring ParsingName(const std::wstring& appId) {
    if (IsDirectPath(appId)) return appId;
    return L"shell:AppsFolder\\" + appId;
}

std::wstring ResolveTarget(const std::wstring& appId) {
    if (IsDirectPath(appId)) {
        if (Lower(appId).size() > 4 && Lower(appId).rfind(L".exe") == appId.size() - 4) return Lower(appId);
        return L"";
    }
    if (appId == L"Microsoft.Windows.Explorer") return L"c:\\windows\\explorer.exe";
    ComPtr<IShellItem2> it;
    if (FAILED(SHCreateItemFromParsingName(ParsingName(appId).c_str(), nullptr, IID_PPV_ARGS(&it)))) return L"";
    PWSTR s = nullptr;
    std::wstring r;
    if (SUCCEEDED(it->GetString(PKEY_Link_TargetParsingPath, &s)) && s) {
        r = Lower(s);
        CoTaskMemFree(s);
    }
    if (!r.empty() && r[0] == L':') r.clear();
    return r;
}

std::wstring DisplayName(const std::wstring& appId) {
    ComPtr<IShellItem> it;
    if (FAILED(SHCreateItemFromParsingName(ParsingName(appId).c_str(), nullptr, IID_PPV_ARGS(&it)))) return L"";
    PWSTR s = nullptr;
    std::wstring r;
    if (SUCCEEDED(it->GetDisplayName(SIGDN_NORMALDISPLAY, &s)) && s) {
        r = s;
        CoTaskMemFree(s);
    }
    return r;
}

std::wstring FileDescription(const std::wstring& exe) {
    DWORD h = 0, sz = GetFileVersionInfoSizeW(exe.c_str(), &h);
    std::wstring r;
    if (sz) {
        std::vector<BYTE> buf(sz);
        if (GetFileVersionInfoW(exe.c_str(), 0, sz, buf.data())) {
            struct LANG { WORD l, c; }* tr = nullptr;
            UINT n = 0;
            if (VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (void**)&tr, &n) && n >= sizeof(LANG)) {
                wchar_t key[96];
                swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr[0].l, tr[0].c);
                wchar_t* v = nullptr;
                UINT vl = 0;
                if (VerQueryValueW(buf.data(), key, (void**)&v, &vl) && v && vl > 1) r = v;
            }
        }
    }
    if (r.empty()) {
        r = PathFindFileNameW(exe.c_str());
        size_t dot = r.rfind(L'.');
        if (dot != std::wstring::npos) r.resize(dot);
        if (!r.empty()) r[0] = towupper(r[0]);
    }
    return r;
}

// ---- ícones de apps da Store: o shell devolve o ícone dentro de um quadrado colorido ("plated").
// Lemos o manifesto do pacote e usamos o asset "unplated", como a barra de tarefas faz.
static std::wstring PackageRoot(const std::wstring& family) {
    UINT32 count = 0, bufLen = 0;
    if (GetPackagesByPackageFamily(family.c_str(), &count, nullptr, &bufLen, nullptr) != ERROR_INSUFFICIENT_BUFFER || !count) return L"";
    std::vector<PWSTR> names(count);
    std::vector<wchar_t> buf(bufLen);
    if (GetPackagesByPackageFamily(family.c_str(), &count, names.data(), &bufLen, buf.data()) != ERROR_SUCCESS) return L"";
    UINT32 len = 0;
    GetPackagePathByFullName(names[0], &len, nullptr);
    if (!len) return L"";
    std::wstring path(len, L'\0');
    if (GetPackagePathByFullName(names[0], &len, path.data()) != ERROR_SUCCESS) return L"";
    path.resize(wcslen(path.c_str()));
    return path;
}

static std::wstring ReadUtf8(const std::wstring& p) {
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    DWORD sz = GetFileSize(f, nullptr), rd = 0;
    std::string s(sz, '\0');
    ReadFile(f, s.data(), sz, &rd, nullptr);
    CloseHandle(f);
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)rd, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)rd, w.data(), n);
    return w;
}

static int NumAfter(const std::wstring& s, const wchar_t* key) {
    size_t p = s.find(key);
    return p == std::wstring::npos ? 0 : _wtoi(s.c_str() + p + wcslen(key));
}

static ComPtr<IWICBitmapSource> PackagedIcon(const std::wstring& aumid) {
    size_t bang = aumid.find(L'!');
    if (bang == std::wstring::npos) return nullptr;
    std::wstring root = PackageRoot(aumid.substr(0, bang));
    if (root.empty()) return nullptr;
    std::wstring xml = ReadUtf8(root + L"\\AppxManifest.xml"), lx = Lower(xml);
    size_t pos = lx.find(L"id=\"" + Lower(aumid.substr(bang + 1)) + L"\"");
    if (pos == std::wstring::npos) return nullptr;
    size_t lp = lx.find(L"square44x44logo=\"", pos);
    if (lp == std::wstring::npos) lp = lx.find(L"square150x150logo=\"", pos);
    if (lp == std::wstring::npos) return nullptr;
    size_t q1 = lx.find(L'"', lp) + 1, q2 = lx.find(L'"', q1);
    std::wstring rel = xml.substr(q1, q2 - q1);
    std::wstring full = root + L"\\" + rel;
    size_t sl = full.rfind(L'\\'), dot = full.rfind(L'.');
    if (sl == std::wstring::npos || dot == std::wstring::npos || dot < sl) return nullptr;
    std::wstring dir = full.substr(0, sl), base = full.substr(sl + 1, dot - sl - 1), ext = full.substr(dot);
    bool dark = gfx::DarkMode();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\" + base + L"*" + ext).c_str(), &fd);
    std::wstring best;
    int bestScore = -1;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = Lower(fd.cFileName);
            if (n.find(L"contrast-") != std::wstring::npos) continue;
            bool unpl = n.find(L"altform-unplated") != std::wstring::npos;
            bool light = n.find(L"altform-lightunplated") != std::wstring::npos;
            int ts = NumAfter(n, L"targetsize-"), sc = NumAfter(n, L"scale-");
            int score = (unpl ? (dark ? 3000 : 2000) : light ? (dark ? 2000 : 3000) : 0) + (ts ? std::min(ts, 256) : sc ? sc * 44 / 100 : 40);
            if (score > bestScore) { bestScore = score; best = dir + L"\\" + fd.cFileName; }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (best.empty()) return nullptr;
    return gfx::LoadImageFile(best, 256, 256);
}

// Corta as margens transparentes e centraliza num quadrado: todos os ícones ficam do mesmo tamanho visual.
static ComPtr<IWICBitmapSource> TrimIcon(IWICBitmapSource* src) {
    if (!src) return nullptr;
    UINT w = 0, h = 0;
    src->GetSize(&w, &h);
    if (!w || !h) return src;
    std::vector<BYTE> px((size_t)w * h * 4);
    if (FAILED(src->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) return src;
    int l = (int)w, t = (int)h, r = -1, b = -1;
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x)
            if (px[((size_t)y * w + x) * 4 + 3] > 10) {
                l = std::min(l, (int)x); r = std::max(r, (int)x);
                t = std::min(t, (int)y); b = std::max(b, (int)y);
            }
    if (r < 0) return src;
    int bw = r - l + 1, bh = b - t + 1;
    int side = (int)(std::max(bw, bh) * 1.04f) + 2;
    if (side >= (int)std::max(w, h) - 2) return src;
    std::vector<BYTE> out((size_t)side * side * 4, 0);
    int ox = (side - bw) / 2, oy = (side - bh) / 2;
    for (int y = 0; y < bh; ++y) memcpy(&out[((size_t)(y + oy) * side + ox) * 4], &px[((size_t)(y + t) * w + l) * 4], (size_t)bw * 4);
    ComPtr<IWICBitmap> nb;
    if (FAILED(gfx::wic->CreateBitmapFromMemory(side, side, GUID_WICPixelFormat32bppPBGRA, side * 4, (UINT)out.size(), out.data(), &nb))) return src;
    return nb;
}

ComPtr<IWICBitmapSource> AppIcon(const std::wstring& appId, const std::wstring& exe, HWND hwnd, int px) {
    ComPtr<IWICBitmapSource> b;
    if (appId.find(L'!') != std::wstring::npos) b = PackagedIcon(appId);
    if (!b && !appId.empty()) b = gfx::LoadShellImage(ParsingName(appId), px);
    if (!b && !exe.empty()) b = gfx::LoadShellImage(exe, px);
    if (!b && hwnd) {
        DWORD_PTR ic = 0;
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 200, &ic);
        if (!ic) ic = GetClassLongPtrW(hwnd, GCLP_HICON);
        if (ic) b = gfx::FromHIcon((HICON)ic);
    }
    auto t = TrimIcon(b.Get());
    return gfx::Downscale(t.Get(), px);
}

bool Launch(const std::wstring& appId, const std::wstring& args) {
    std::wstring target = ParsingName(appId);
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    sei.lpFile = target.c_str();
    sei.lpParameters = args.empty() ? nullptr : args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

typedef void(WINAPI* SwitchToThisWindow_t)(HWND, BOOL);

void Activate(HWND h) {
    if (IsIconic(h)) ShowWindowAsync(h, SW_RESTORE);
    if (!SetForegroundWindow(h)) {
        static auto sw = (SwitchToThisWindow_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SwitchToThisWindow");
        if (sw) sw(h, TRUE);
    }
}

void Minimize(HWND h) { ShowWindowAsync(h, SW_MINIMIZE); }
void Close(HWND h) { PostMessageW(h, WM_SYSCOMMAND, SC_CLOSE, 0); }

std::wstring Title(HWND h) {
    wchar_t buf[512] = {};
    DWORD_PTR r = 0;
    if (SendMessageTimeoutW(h, WM_GETTEXT, 512, (LPARAM)buf, SMTO_ABORTIFHUNG, 100, &r)) return buf;
    InternalGetWindowText(h, buf, 512);
    return buf;
}

HWND ForegroundApp() {
    HWND h = GetForegroundWindow();
    if (!h) return nullptr;
    HWND root = GetAncestor(h, GA_ROOTOWNER);
    if (root) h = root;
    if (IsOwnWindow(h)) return nullptr;
    return h;
}

void SendKeys(std::initializer_list<WORD> keys) {
    std::vector<INPUT> in;
    for (WORD k : keys) {
        INPUT i{};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = k;
        in.push_back(i);
    }
    for (auto it = keys.end(); it != keys.begin();) {
        --it;
        INPUT i{};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = *it;
        i.ki.dwFlags = KEYEVENTF_KEYUP;
        in.push_back(i);
    }
    SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}
}  // namespace apps
