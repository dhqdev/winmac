#pragma once
#include "common.h"

// Launchpad: grade de apps em tela cheia sobre o fundo desfocado, com busca e páginas.
namespace launchpad {
void Toggle();
void Preload();       // carrega a lista de apps e ícones aos poucos, sem travar
bool IsOpen();
}
