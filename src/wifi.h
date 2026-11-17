#pragma once
#include "common.h"

// Wi-Fi pelo serviço WLAN do Windows: lista redes, conecta (criando o perfil com a senha) e desconecta.
namespace wifi {
struct Net {
    std::wstring ssid;
    int quality = 0;            // 0..100
    bool secured = false;
    bool connected = false;
    bool hasProfile = false;    // o Windows já conhece esta rede (conecta sem pedir senha)
    std::wstring profile;
    int auth = 0, cipher = 0;   // DOT11_AUTH_ALGORITHM / DOT11_CIPHER_ALGORITHM
    bool enterprise = false;    // WPA-Enterprise: precisa do painel do Windows
};
bool HasAdapter();
bool RadioOn();
bool SetRadio(bool on);
void Scan();
std::vector<Net> List();
enum class Result { Ok, WrongPassword, Failed, Unsupported };
// Bloqueia até conectar ou desistir (~15 s). Rode numa thread.
Result Connect(const Net& n, const std::wstring& password);
void Disconnect();
std::wstring Describe(Result r);
}
