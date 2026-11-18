#pragma once
#include "common.h"

// Instalação por usuário (sem administrador) em %LOCALAPPDATA%\WinMac, atualização e desinstalação.
namespace install {
std::wstring Dir();
std::wstring InstalledExe();
bool IsInstalledCopy();          // este processo é o WinMac.exe instalado?
bool IsInstalled();

struct Options {
    int theme = 1;               // 0 claro, 1 escuro, 2 automático
    int accent = 0;
    int wallpaper = 0;           // papel de parede gerado (art::Wallpapers)
    bool useWallpaper = true;
    bool traffic = false;        // botões semáforo nas janelas (padrão: os do próprio Windows)
    bool switcher = true;        // Alt+Tab do WinMac
    bool autohide = false;       // Dock se esconde
    bool widgets = true;
    bool cursors = true;         // cursores estilo Mac
    bool debloat = true;         // remove apps pagos/de assinatura e propagandas
    bool taskbarTop = true;      // barra do Windows (escondida) no topo: menus da bandeja abrem perto da barra
};
// progress(passo, total, texto). Retorna false com err preenchido se algo essencial falhou.
bool Install(const Options& o, const std::function<void(int, int, const std::wstring&)>& progress, std::wstring* err);
int ApplyUpdate(const std::wstring& dir);    // modo --apply-update
int Uninstall();                             // modo --uninstall
void StopShell(DWORD timeoutMs = 6000);
void LaunchShell();
void Migrate();                              // ajustes de config entre versões (roda no início do shell)

// usados também pelo app Ajustes
void ApplyCursors(bool mac);
bool ApplyWallpaper(int preset);
bool ApplyWallpaperFile(const std::wstring& path);
bool ApplyPhotoOfDay(bool force);   // foto do dia do Bing (como o Destaque do Windows); bloqueia: rode numa thread
void SetAutostart(bool on);
bool Autostart();
void ApplyTheme(int theme);                  // 0 claro, 1 escuro, 2 automático (pelo horário)
void DisableAds();

struct PaidApp { const wchar_t* family; const wchar_t* name; const wchar_t* why; };
const std::vector<PaidApp>& PaidApps();
bool IsPackageInstalled(const wchar_t* family);
bool RemovePackages(const std::vector<std::wstring>& families);   // bloqueia; rode numa thread
}
