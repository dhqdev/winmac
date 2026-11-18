#pragma once
#include "common.h"

// Limpeza do sistema: encontra o que dá para apagar com segurança (temporários, caches de navegador,
// relatórios de erro, cache de sombreadores, Lixeira...) e limpa. Só mexe em arquivos do seu usuário.
namespace cleaner {
struct Item {
    std::wstring id, name, detail;
    uint64_t bytes = 0;
    bool recycle = false;     // Lixeira (o Windows pede confirmação)
};
// Varre tudo (pode levar alguns segundos: rode numa thread).
std::vector<Item> Scan();
// Limpa os itens pedidos; retorna bytes liberados. Arquivos em uso são pulados.
uint64_t Clean(const std::vector<std::wstring>& ids, HWND owner);
uint64_t Total(const std::vector<Item>& items);
}
