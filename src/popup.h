#pragma once
#include "common.h"

// Menu suspenso de vidro (estilo macOS), usado pela barra de menu e pelo dock.
struct PMItem {
    std::wstring text, shortcut;
    int id = 0;
    bool enabled = true, checked = false, sep = false, bold = false;
    static PMItem Sep() { PMItem i; i.sep = true; return i; }
};

enum { kMenuCancel = 0, kMenuSwitch = -1, kMenuLeft = -2, kMenuRight = -3 };

// Bloqueia até escolher. Retorna o id escolhido, 0 se cancelado, ou kMenuSwitch/kMenuLeft/kMenuRight.
// onOutsideMove: chamado com a posição do mouse fora do menu; retornar true fecha com kMenuSwitch.
// anchorBottom: y é a borda de baixo (o menu abre para cima, como no dock).
int PopupMenu(const std::vector<PMItem>& items, int x, int y, std::function<bool(POINT)> onOutsideMove = nullptr,
              bool anchorBottom = false, int minWidthPx = 0);
bool PopupMenuOpen();
