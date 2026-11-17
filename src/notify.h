#pragma once
#include "common.h"

// Notificação de vidro no canto superior direito (como as do macOS). Clique executa onClick.
namespace notify {
void Show(const std::wstring& title, const std::wstring& body, std::function<void()> onClick = nullptr, int ms = 7000);
}
