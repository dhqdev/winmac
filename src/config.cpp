#include "config.h"

namespace cfg {
static std::wstring s_path;

// Configuração inicial. Os apps fixos no Dock são escolhidos na primeira execução conforme o que está instalado.
static const wchar_t* kDefault =
    L"; WinMac — configuração. Prefira mudar pelo app Ajustes (menu do logo > Ajustes do Sistema).\r\n"
    L"[Dock]\r\n"
    L"IconSize=50\r\n"
    L"Magnification=1\r\n"
    L"MagnifyScale=1.6\r\n"
    L"ShowRecent=1\r\n"
    L"AutoHide=0\r\n"
    L"Folders=shell:Downloads\r\n"
    L"[MenuBar]\r\n"
    L"Enabled=1\r\n"
    L"Height=28\r\n"
    L"ShowTray=1\r\n"
    L"BatteryPercent=1\r\n"
    L"ClockDate=1\r\n"
    L"[Appearance]\r\n"
    L"Accent=0\r\n"
    L"Glass=100\r\n"
    L"ReduceTransparency=0\r\n"
    L"[Windows]\r\n"
    L"TrafficLights=0\r\n"
    L"Switcher=1\r\n"
    L"SwitcherPreviews=1\r\n"
    L"[Effects]\r\n"
    L"OpenClose=1\r\n"
    L"[Widgets]\r\n"
    L"Enabled=1\r\n"
    L"[Weather]\r\n"
    L"City=\r\n"
    L"Lat=\r\n"
    L"Lon=\r\n"
    L"[Screen]\r\n"
    L"RoundedCorners=1\r\n"
    L"CornerRadius=10\r\n"
    L"[Update]\r\n"
    L"Auto=1\r\n";

void Init(const std::wstring& dirOverride) {
    std::wstring dir = dirOverride.empty() ? AppDataDir() : dirOverride;
    CreateDirectoryW(dir.c_str(), nullptr);
    s_path = dir + L"\\config.ini";
    if (GetFileAttributesW(s_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        HANDLE f = CreateFileW(s_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD wr;
            WORD bom = 0xFEFF;
            WriteFile(f, &bom, 2, &wr, nullptr);
            WriteFile(f, kDefault, (DWORD)(wcslen(kDefault) * sizeof(wchar_t)), &wr, nullptr);
            CloseHandle(f);
        }
    }
}

std::wstring Path() { return s_path; }

bool Has(const wchar_t* sec, const wchar_t* key) {
    wchar_t buf[8];
    GetPrivateProfileStringW(sec, key, L"\x01", buf, 8, s_path.c_str());
    return buf[0] != 1;
}

std::wstring Str(const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    std::wstring buf(8192, L'\0');
    DWORD n = GetPrivateProfileStringW(sec, key, def, buf.data(), (DWORD)buf.size(), s_path.c_str());
    buf.resize(n);
    return buf;
}

int Int(const wchar_t* sec, const wchar_t* key, int def) { return (int)GetPrivateProfileIntW(sec, key, def, s_path.c_str()); }

float Float(const wchar_t* sec, const wchar_t* key, float def) {
    std::wstring s = Str(sec, key, L"");
    if (s.empty()) return def;
    for (auto& ch : s) if (ch == L',') ch = L'.';
    return (float)_wtof(s.c_str());
}

void Set(const wchar_t* sec, const wchar_t* key, const std::wstring& v) { WritePrivateProfileStringW(sec, key, v.c_str(), s_path.c_str()); }
void SetInt(const wchar_t* sec, const wchar_t* key, int v) { Set(sec, key, std::to_wstring(v)); }
void SetFloat(const wchar_t* sec, const wchar_t* key, float v) {
    wchar_t b[32];
    swprintf_s(b, L"%.2f", v);
    Set(sec, key, b);
}

std::vector<std::wstring> List(const wchar_t* sec, const wchar_t* key) {
    std::vector<std::wstring> out;
    std::wstring s = Str(sec, key, L"");
    size_t p = 0;
    while (p <= s.size()) {
        size_t q = s.find(L'|', p);
        if (q == std::wstring::npos) q = s.size();
        std::wstring part = s.substr(p, q - p);
        if (!part.empty()) out.push_back(part);
        p = q + 1;
    }
    return out;
}

void SetList(const wchar_t* sec, const wchar_t* key, const std::vector<std::wstring>& v) {
    std::wstring s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += L'|';
        s += v[i];
    }
    Set(sec, key, s);
}
}  // namespace cfg
