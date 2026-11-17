#pragma once
#include "common.h"

// App "Ajustes do Sistema" do WinMac (roda como processo separado: WinMac.exe --ajustes [página]).
namespace settings {
int Run(const std::wstring& page);
void RenderPreview(const std::wstring& page, const std::wstring& png);   // imagem para o README
}
