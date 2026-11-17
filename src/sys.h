#pragma once
#include "common.h"

// Estado do sistema para a barra de menu e a Central de Controle.
namespace sys {
struct Battery { bool present = false; int pct = 100; bool charging = false; bool plugged = true; };
Battery GetBattery();
struct Wifi { bool connected = false; bool wireless = false; int quality = 0; std::wstring ssid; };
Wifi GetWifi();
float GetVolume(bool* muted);
void SetVolume(float v);
void SetMute(bool m);
int GetBrightness();                // -1 se não suportado
void SetBrightness(int pct);
void SetDarkMode(bool dark);
}
