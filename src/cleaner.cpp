#include "cleaner.h"

namespace cleaner {
namespace {
std::wstring Env(const wchar_t* p) {
    wchar_t b[MAX_PATH * 2];
    DWORD n = ExpandEnvironmentStringsW(p, b, MAX_PATH * 2);
    return n ? std::wstring(b) : L"";
}

struct Def {
    const wchar_t* id;
    const wchar_t* name;
    const wchar_t* detail;
    std::vector<std::wstring> (*dirs)();
    int minAgeHours;   // só apaga arquivos mais velhos que isso (0 = qualquer)
};

std::vector<std::wstring> BrowserCaches(const wchar_t* base) {
    std::vector<std::wstring> out;
    std::wstring root = Env(base);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring n = fd.cFileName;
        if (n != L"Default" && n.rfind(L"Profile ", 0) != 0) continue;
        for (const wchar_t* sub : { L"\\Cache", L"\\Code Cache", L"\\GPUCache", L"\\Service Worker\\CacheStorage" }) out.push_back(root + L"\\" + n + sub);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (const wchar_t* sub : { L"\\ShaderCache", L"\\GrShaderCache" }) out.push_back(root + sub);
    return out;
}

const std::vector<Def>& Defs() {
    static std::vector<Def> d = {
        { L"temp", L"Arquivos temporários", L"Restos que os apps deixam para trás", [] { return std::vector<std::wstring>{ Env(L"%TEMP%") }; }, 24 },
        { L"chrome", L"Cache do Google Chrome", L"Páginas e imagens guardadas (feche o Chrome para limpar tudo)", [] { return BrowserCaches(L"%LOCALAPPDATA%\\Google\\Chrome\\User Data"); }, 0 },
        { L"edge", L"Cache do Microsoft Edge", L"Páginas e imagens guardadas", [] { return BrowserCaches(L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data"); }, 0 },
        { L"inet", L"Cache da internet do Windows", L"Arquivos baixados por apps do Windows", [] { return std::vector<std::wstring>{ Env(L"%LOCALAPPDATA%\\Microsoft\\Windows\\INetCache") }; }, 0 },
        { L"shader", L"Cache de gráficos (DirectX)", L"Recriado sozinho quando os jogos e apps precisam", [] {
              return std::vector<std::wstring>{ Env(L"%LOCALAPPDATA%\\D3DSCache"), Env(L"%LOCALAPPDATA%\\NVIDIA\\DXCache"), Env(L"%LOCALAPPDATA%\\NVIDIA\\GLCache"),
                                                Env(L"%LOCALAPPDATA%\\AMD\\DxCache"), Env(L"%LOCALAPPDATA%\\Intel\\ShaderCache") }; }, 0 },
        { L"crash", L"Relatórios de erro e travamentos", L"Despejos de memória e relatórios antigos", [] {
              return std::vector<std::wstring>{ Env(L"%LOCALAPPDATA%\\CrashDumps"), Env(L"%LOCALAPPDATA%\\Microsoft\\Windows\\WER\\ReportArchive"),
                                                Env(L"%LOCALAPPDATA%\\Microsoft\\Windows\\WER\\ReportQueue") }; }, 0 },
        { L"thumbs", L"Miniaturas antigas das pastas", L"O Explorador recria quando abrir as pastas", [] {
              return std::vector<std::wstring>{ Env(L"%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer") }; }, 0 },
        { L"teams", L"Cache do Teams e do Discord", L"Arquivos guardados pelos apps de conversa", [] {
              return std::vector<std::wstring>{ Env(L"%APPDATA%\\Microsoft\\Teams\\Cache"), Env(L"%APPDATA%\\discord\\Cache"), Env(L"%APPDATA%\\discord\\Code Cache") }; }, 0 },
    };
    return d;
}

bool SafeDir(const std::wstring& d) {
    std::wstring l = Lower(d);
    std::wstring prof = Lower(Env(L"%USERPROFILE%"));
    return l.size() > prof.size() + 8 && l.rfind(prof, 0) == 0;   // só dentro da pasta do usuário e nunca ela inteira
}

bool Matches(const std::wstring& id, const std::wstring& file) {
    if (id != L"thumbs") return true;
    std::wstring f = Lower(file);
    return f.rfind(L"thumbcache_", 0) == 0 || f.rfind(L"iconcache_", 0) == 0;
}

uint64_t Walk(const std::wstring& dir, const std::wstring& id, bool del, const FILETIME* older, int depth) {
    uint64_t total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        std::wstring p = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (id == L"thumbs" || depth > 12) continue;
            total += Walk(p, id, del, older, depth + 1);
            if (del) RemoveDirectoryW(p.c_str());
        } else {
            if (!Matches(id, fd.cFileName)) continue;
            if (older && CompareFileTime(&fd.ftLastWriteTime, older) >= 0) continue;
            uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if (!del) total += sz;
            else {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileW(p.c_str())) total += sz;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return total;
}

FILETIME AgeLimit(int hours) {
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER u{ { now.dwLowDateTime, now.dwHighDateTime } };
    u.QuadPart -= (ULONGLONG)hours * 3600 * 10000000;
    return FILETIME{ u.LowPart, u.HighPart };
}
}  // namespace

std::vector<Item> Scan() {
    std::vector<Item> out;
    for (auto& d : Defs()) {
        Item it{ d.id, d.name, d.detail, 0, false };
        FILETIME lim = AgeLimit(d.minAgeHours);
        for (auto& dir : d.dirs())
            if (!dir.empty() && SafeDir(dir)) it.bytes += Walk(dir, d.id, false, d.minAgeHours ? &lim : nullptr, 0);
        if (it.bytes > 0) out.push_back(it);
    }
    SHQUERYRBINFO q{ sizeof(q) };
    if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &q)) && q.i64Size > 0)
        out.push_back(Item{ L"recycle", L"Lixeira", std::to_wstring(q.i64NumItems) + L" itens apagados", (uint64_t)q.i64Size, true });
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.bytes > b.bytes; });
    return out;
}

uint64_t Clean(const std::vector<std::wstring>& ids, HWND owner) {
    uint64_t freed = 0;
    for (auto& id : ids) {
        if (id == L"recycle") {
            SHQUERYRBINFO q{ sizeof(q) };
            SHQueryRecycleBinW(nullptr, &q);
            if (SUCCEEDED(SHEmptyRecycleBinW(owner, nullptr, 0))) {
                SHQUERYRBINFO q2{ sizeof(q2) };
                SHQueryRecycleBinW(nullptr, &q2);
                if (q2.i64Size < q.i64Size) freed += (uint64_t)(q.i64Size - q2.i64Size);
            }
            continue;
        }
        for (auto& d : Defs()) {
            if (id != d.id) continue;
            FILETIME lim = AgeLimit(d.minAgeHours);
            for (auto& dir : d.dirs())
                if (!dir.empty() && SafeDir(dir)) freed += Walk(dir, d.id, true, d.minAgeHours ? &lim : nullptr, 0);
        }
    }
    return freed;
}

uint64_t Total(const std::vector<Item>& items) {
    uint64_t t = 0;
    for (auto& i : items) t += i.bytes;
    return t;
}
}  // namespace cleaner
