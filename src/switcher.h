#pragma once
#include "common.h"

// Alt+Tab do WinMac: painel de vidro com prévias ao vivo das janelas (miniaturas do DWM) e ícones.
namespace switcher {
void Init();
void Shutdown();
void Reload();
// Prévia para documentação: desenha o painel com janelas de exemplo e salva PNG.
void RenderPreview(const std::wstring& png);
void DebugShow(bool show);   // teste: mostra o painel sem teclado
}
