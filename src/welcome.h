#pragma once
#include "common.h"

// Boas-vindas com o Pingo. Também é o instalador: rodando o WinMac-Setup.exe baixado, instala no fim.
namespace welcome {
int Run(bool installMode);
void RenderPreview(int page, const std::wstring& png);
}
