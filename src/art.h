#pragma once
#include "common.h"
#include "gfx.h"

// Arte do WinMac desenhada por código (vetorial, nítida em qualquer escala e sem arquivos extras):
// o mascote Pingo, o logo, ícones e os papéis de parede.
namespace art {
enum class Face { Happy, Wink, Excited, Sleepy };
struct Pose {
    float t = 0;          // tempo em segundos (respira, pisca)
    float wave = 0;       // 0..1: acena com a mãozinha direita
    float hop = 0;        // 0..1: pulinho
    Face face = Face::Happy;
    bool shadow = true;
    bool arms = true;
};
// Desenha o Pingo centrado em (cx, cy); size é a altura total da gotinha.
void Pingo(ID2D1DeviceContext* c, float cx, float cy, float size, const Pose& pose);
// Logo monocromático (gotinha com olhinhos) para a barra de menus.
void Logo(ID2D1DeviceContext* c, float cx, float cy, float size, D2D1_COLOR_F col);
// Ícone do app (quadrado arredondado em degradê com o Pingo).
void AppIcon(ID2D1DeviceContext* c, D2D1_RECT_F r);
// Ícone do Ajustes (engrenagem) no estilo dos ícones do Dock.
void SettingsIcon(ID2D1DeviceContext* c, D2D1_RECT_F r);
// Ícone de seção (quadradinho colorido com um símbolo), usado na barra lateral do Ajustes.
void SectionIcon(ID2D1DeviceContext* c, D2D1_RECT_F r, D2D1_COLOR_F bg, const wchar_t* glyph);
// Confete para a tela de "Tudo pronto!" (t em segundos desde o início).
void Confetti(ID2D1DeviceContext* c, D2D1_RECT_F area, float t);
// Ícone de sinal Wi-Fi (0..100) desenhado com arcos.
void WifiBars(ID2D1DeviceContext* c, float cx, float cy, float s, int quality, D2D1_COLOR_F col);

// Papéis de parede gerados (originais do WinMac)
struct Wallpaper { const wchar_t* name; bool dark; };
const std::vector<Wallpaper>& Wallpapers();
ComPtr<IWICBitmap> RenderWallpaper(int preset, int w, int h);
bool SaveWallpaper(int preset, int w, int h, const std::wstring& path);   // .jpg

// Exporta PNGs (ícone em vários tamanhos e o Pingo) — usado para gerar o .ico e as imagens do README.
void ExportArt(const std::wstring& dir);
}
