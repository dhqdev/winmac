#pragma once
#include "ui.h"
#include "wifi.h"

// Lista de redes Wi-Fi com conexão e senha, desenhada dentro de qualquer janela do kit ui.
class WifiPanel {
public:
    WifiPanel();
    static bool demo;   // prévias do README: redes fictícias (não expõe as redes reais)
    ~WifiPanel();
    // Desenha em area; retorna a altura usada. compact = sem cabeçalho grande (menu da barra).
    float Draw(ui::Window& w, D2D1_RECT_F area, bool compact = false);
    bool Refresh(bool scan = false);    // atualiza a lista (chame num timer); true se mudou
    std::function<void(const std::wstring&)> onConnected;   // avisado ao conectar

private:
    std::vector<wifi::Net> nets_;
    bool radio_ = true, hasAdapter_ = true;
    std::wstring open_;          // rede com o campo de senha aberto
    std::wstring pass_;
    std::wstring busy_;          // rede conectando
    std::wstring status_;
    bool statusErr_ = false;
    DWORD lastScan_ = 0;
    std::shared_ptr<bool> alive_;
    void StartConnect(ui::Window& w, const wifi::Net& n, const std::wstring& pass);
};
