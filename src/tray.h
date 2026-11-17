#pragma once
#include "common.h"

// Espelha os ícones da área de notificação do Windows (a barra de tarefas fica escondida)
// e repassa os cliques para o Explorer, que entrega ao app do jeito certo.
namespace tray {
struct Icon {
    HWND owner = nullptr;
    UINT uid = 0;
    HICON hicon = nullptr;
    std::wstring tip;
    int index = 0;
    bool overflow = false;
    ComPtr<IWICBitmapSource> bmp;
};
bool Refresh();                         // true se algo mudou
const std::vector<Icon>& Icons();
void Click(const Icon& ic, bool right, bool dbl);
}
