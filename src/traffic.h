#pragma once
#include "common.h"

// Botões estilo Mac (vermelho, amarelo, verde) por cima dos botões de fechar/minimizar/maximizar das janelas.
// O Windows 10 não permite trocar esses botões sem alterar arquivos do sistema; aqui uma camada pequena cobre a
// área dos botões de cada janela (mesma cor da barra de título) e faz o mesmo trabalho.
namespace traffic {
void Init();
void Shutdown();
void Reload();
void RenderPreview(const std::wstring& png);
void Diagnose(const std::wstring& out);   // lista onde os botões de cada janela foram achados
}
