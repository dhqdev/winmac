// WinMac — dock, barra de menu e widgets no estilo macOS para Windows 10.
// Código nativo (Win32 + Direct2D) para ficar leve: nada roda em loop quando a tela está parada.
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shellscalingapi.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <memory>
#include <functional>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>
#include <atomic>

using Microsoft::WRL::ComPtr;

extern HINSTANCE g_inst;
extern HWND g_ctrl;            // janela de controle (mensagens globais)

// Mensagens internas
#define WM_MD_SHELLHOOK_FWD (WM_APP + 10)
#define WM_MD_WEATHER       (WM_APP + 11)
#define WM_MD_LAUNCHPAD     (WM_APP + 12)
#define WM_MD_RELAYOUT      (WM_APP + 13)
#define WM_MD_FULLSCREEN    (WM_APP + 14)
#define WM_MD_APPBAR        (WM_APP + 15)
#define WM_MD_ICONS_READY   (WM_APP + 16)
#define WM_MD_RECYCLE       (WM_APP + 17)
#define WM_MD_RELOAD        (WM_APP + 20)   // Ajustes mudou a configuração (wParam = o que mudou)
#define WM_MD_UPDATE        (WM_APP + 21)   // resultado da verificação de atualização (lParam = update::Info*)
#define WM_MD_SWITCH        (WM_APP + 22)   // Alt+Tab (wParam = ação)
#define WM_MD_ASYNC         (WM_APP + 23)   // executa std::function<void()>* na thread da interface
#define WM_MD_WIFI          (WM_APP + 24)   // resultado de conexão Wi-Fi

// O que mudou (WM_MD_RELOAD)
enum : WPARAM { RL_ALL = 0, RL_THEME = 1, RL_DOCK = 2, RL_MENUBAR = 3, RL_WIDGETS = 4, RL_WINDOWS = 5, RL_WALLPAPER = 6 };

inline float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline std::wstring Lower(std::wstring s) { std::transform(s.begin(), s.end(), s.begin(), ::towlower); return s; }
inline bool StartsWithI(const std::wstring& s, const std::wstring& p) { return s.size() >= p.size() && _wcsnicmp(s.c_str(), p.c_str(), p.size()) == 0; }

std::wstring AppDataDir();     // %LOCALAPPDATA%\WinMac
std::wstring ExeDir();
void Log(const wchar_t* fmt, ...);
void QuitWinMac();
void ToggleLaunchpad();
void RefreshAll();             // tema mudou etc.
bool IsOwnWindow(HWND h);
// Reserva de espaço na borda da tela (AppBar), para janelas maximizadas não ficarem por baixo.
void AppBarReserve(HWND h, UINT edge, int thickness);
void AppBarRemove(HWND h);
void OpenPath(const std::wstring& path);   // abre pasta/arquivo/URI
void RestartWinMac();
std::wstring SelfPath();                   // caminho deste executável
void OpenAjustes(const std::wstring& page = L"");   // abre o app Ajustes (outro processo)
void NotifyShell(WPARAM what);             // avisa o WinMac (barra/dock) que a configuração mudou
void RunAsync(HWND target, std::function<void()> fn);   // roda fn na thread dona de target
std::wstring Utf8ToW(const std::string& s);
std::string WToUtf8(const std::wstring& s);
