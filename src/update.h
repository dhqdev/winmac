#pragma once
#include "common.h"

// Atualizações direto das Releases do GitHub (dhqdev/winmac).
namespace update {
struct Info {
    bool ok = false;            // a consulta funcionou
    bool newer = false;         // existe versão mais nova que esta
    std::wstring version;       // ex.: 1.2.0
    std::wstring notes;         // texto da release
    std::wstring url;           // download do .exe
    std::wstring page;          // página da release
    std::string sha256;         // resumo publicado pelo GitHub (pode vir vazio)
    uint64_t size = 0;
    std::wstring error;
};
int Compare(const std::wstring& a, const std::wstring& b);   // -1, 0, 1
Info Check();                                                // bloqueia (rode numa thread)
void CheckAsync(HWND notify);                                // posta WM_MD_UPDATE com new Info*
// Baixa, confere e inicia o instalador da nova versão (que fecha este WinMac e se instala por cima).
// Retorna mensagem de erro, ou vazio se a nova versão já foi iniciada.
std::wstring DownloadAndRun(const Info& in, const std::function<void(float)>& progress, const std::atomic<bool>* cancel);
void SaveAvailable(const Info& in);                          // guarda no config.ini (badge na barra)
}
