#pragma once
#include "common.h"

namespace apps {
struct WinInfo {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    std::wstring exe;      // caminho do executável em minúsculas
    std::wstring aumid;    // AppUserModelID (janela ou processo), pode ser vazio
    bool explicitAumid = false;
};

bool IsTaskWindow(HWND h);
const WinInfo& Describe(HWND h);                 // com cache por HWND
std::vector<HWND> TaskWindows();             // ordem z (de cima para baixo)
void ForgetWindow(HWND h);
std::wstring ProcessPath(DWORD pid);
std::wstring ProcessAumid(DWORD pid);
std::wstring ResolveTarget(const std::wstring& appId);   // exe (minúsculo) de um item do AppsFolder
std::wstring DisplayName(const std::wstring& appId);
std::wstring FileDescription(const std::wstring& exe);
std::wstring ParsingName(const std::wstring& appId);     // "shell:AppsFolder\..." ou o próprio caminho
ComPtr<IWICBitmapSource> AppIcon(const std::wstring& appId, const std::wstring& exe, HWND hwnd, int px);
bool Launch(const std::wstring& appId, const std::wstring& args = L"");
void Activate(HWND h);
void Minimize(HWND h);
void Close(HWND h);
std::wstring Title(HWND h);
HWND ForegroundApp();                             // janela de app em primeiro plano (ignora as do WinMac)
void SendKeys(std::initializer_list<WORD> keys);  // pressiona e solta em ordem inversa
}
