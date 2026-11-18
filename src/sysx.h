#pragma once
#include "common.h"

// Funções do sistema usadas pelo app Ajustes (sem precisar abrir as Configurações do Windows).
// Tudo o que muda no Windows passa por aqui com backup do valor original, para a desinstalação voltar tudo.
namespace sysx {
// ---- backup e restauração ----
void SetDword(HKEY root, const wchar_t* key, const wchar_t* name, DWORD v);      // grava guardando o original
void SetString(HKEY root, const wchar_t* key, const wchar_t* name, const std::wstring& v);
DWORD GetDword(HKEY root, const wchar_t* key, const wchar_t* name, DWORD def);
void RestoreAll();                                                               // desinstalação

// ---- energia ----
struct Power { bool hasBattery = false; int pct = 100; bool charging = false, plugged = true, saver = false; int secondsLeft = -1; };
Power GetPower();
int PowerMode();               // 0 melhor bateria, 1 equilibrado, 2 melhor desempenho
bool SetPowerMode(int mode);
int ScreenOffMinutes(bool ac); // 0 = nunca
void SetScreenOffMinutes(bool ac, int minutes);
int SleepMinutes(bool ac);
void SetSleepMinutes(bool ac, int minutes);
int SaverThreshold();          // % em que a economia de bateria liga sozinha
void SetSaverThreshold(int pct);

// ---- som ----
struct AudioDev { std::wstring id, name; bool isDefault = false; };
std::vector<AudioDev> OutputDevices();
bool SetDefaultOutput(const std::wstring& id);

// ---- rádios (Wi-Fi e Bluetooth), pelo Windows.Devices.Radios ----
enum class Radio { Wifi, Bluetooth };
int RadioState(Radio r);       // -1 não existe, 0 desligado, 1 ligado (bloqueia; use numa thread)
bool SetRadioState(Radio r, bool on);

// ---- notificações ----
bool ToastsEnabled();
void SetToasts(bool on);       // "Não perturbe" do WinMac

// ---- mouse ----
int MouseSpeed();              // 1..20
void SetMouseSpeed(int v);
int WheelLines();
void SetWheelLines(int v);
bool ButtonsSwapped();
void SetButtonsSwapped(bool v);
int DoubleClickMs();
void SetDoubleClickMs(int ms);

// ---- armazenamento ----
struct Drive { std::wstring root, label; uint64_t total = 0, free = 0; };
std::vector<Drive> Drives();
uint64_t FolderSize(const std::wstring& dir);
uint64_t CleanTemp();          // apaga arquivos temporários com mais de 1 dia; retorna bytes liberados
std::wstring Bytes(uint64_t b);

// ---- tela ----
std::wstring Resolution();     // ex.: 1920 × 1080 · 125%

// ---- teclado e idioma ----
int KeyboardDelay();           // 0 (curto) .. 3 (longo)
void SetKeyboardDelay(int v);
int KeyboardSpeed();           // 0 .. 31
void SetKeyboardSpeed(int v);
std::vector<std::wstring> KeyboardLayouts();
std::wstring UiLanguage();

// ---- data e hora ----
std::wstring TimeZoneName();
bool Clock24h();
void SetClock24h(bool on);

// ---- privacidade (câmera, microfone, localização) ----
bool ConsentAllowed(const wchar_t* capability);   // "webcam", "microphone", "location"
void SetConsent(const wchar_t* capability, bool allow);

// ---- apps ao iniciar ----
struct StartupApp { std::wstring name, command; bool enabled = true; };
std::vector<StartupApp> StartupApps();
void SetStartupEnabled(const std::wstring& name, bool on);

// ---- impressoras ----
struct Printer { std::wstring name; bool isDefault = false; };
std::vector<Printer> Printers();
bool SetDefaultPrinterName(const std::wstring& name);

// ---- segurança ----
struct Antivirus { std::wstring name; bool enabled = false, upToDate = false; };
std::vector<Antivirus> AntivirusProducts();   // bloqueia (WMI): use numa thread

// ---- rede ----
struct Adapter { std::wstring name, kind, ip; uint64_t speed = 0; bool up = false; };
std::vector<Adapter> Adapters();
}
