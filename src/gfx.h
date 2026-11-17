#pragma once
#include "common.h"

namespace gfx {
extern ComPtr<ID2D1Factory1> d2d;
extern ComPtr<IDWriteFactory> dw;
extern ComPtr<IWICImagingFactory> wic;
extern std::wstring s_dumpDir;

bool Init();
void SetDumpDir(const std::wstring& dir);   // depuração: salva cada quadro como PNG
float Scale();                      // DPI do monitor principal / 96
bool DarkMode();                     // tema do sistema (barra/dock) escuro?
void ReloadTheme();
D2D1_COLOR_F Accent();               // cor de destaque (Ajustes > Aparência)
D2D1_COLOR_F AccentOf(int idx);      // paleta: 0 azul, 1 roxo, 2 rosa, 3 vermelho, 4 laranja, 5 amarelo, 6 verde, 7 grafite
const int kAccentCount = 8;
float GlassStrength();               // intensidade do brilho do vidro (Ajustes)
bool ReduceTransparency();           // Ajustes > Aparência > Reduzir transparência
D2D1_COLOR_F Panel(bool dark, float alphaDark, float alphaLight);   // fundo de painel respeitando a transparência

// Blur do DWM (SetWindowCompositionAttribute). state 3 = blur gaussiano, 4 = acrylic
void SetBlur(HWND h, bool on, int state = 3, DWORD abgr = 0);
void SetRoundRegion(HWND h, int x, int y, int w, int hgt, int radius);

inline D2D1_COLOR_F Rgba(int r, int g, int b, float a = 1.f) { return D2D1::ColorF(r / 255.f, g / 255.f, b / 255.f, a); }
inline D2D1_COLOR_F White(float a) { return D2D1::ColorF(1, 1, 1, a); }
inline D2D1_COLOR_F Black(float a) { return D2D1::ColorF(0, 0, 0, a); }

// Texto
const wchar_t* UiFamily();           // "Inter" se instalada, senão "Segoe UI"
IDWriteTextFormat* Font(float px, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_REGULAR, const wchar_t* family = nullptr);
D2D1_SIZE_F Measure(const std::wstring& s, IDWriteTextFormat* f, float maxW = 100000.f);
void Text(ID2D1DeviceContext* c, const std::wstring& s, IDWriteTextFormat* f, D2D1_RECT_F r, D2D1_COLOR_F col,
          DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING,
          DWRITE_PARAGRAPH_ALIGNMENT pa = DWRITE_PARAGRAPH_ALIGNMENT_CENTER, bool ellipsis = true);
void Emoji(ID2D1DeviceContext* c, const std::wstring& s, float px, D2D1_RECT_F r);

// Imagens
ComPtr<IWICBitmapSource> LoadShellImage(const std::wstring& parsingName, int px, bool iconOnly = true);
ComPtr<IWICBitmapSource> FromHIcon(HICON h);
ComPtr<IWICBitmapSource> Downscale(IWICBitmapSource* src, int px);
ComPtr<IWICBitmapSource> LoadImageFile(const std::wstring& path, int maxW, int maxH);

// Pintura "liquid glass"
void Shadow(ID2D1DeviceContext* c, const D2D1_ROUNDED_RECT& rr, float size, float alpha, float dy);
void GlassOverlay(ID2D1DeviceContext* c, const D2D1_ROUNDED_RECT& rr, bool dark, float strength = 1.f);
void Fill(ID2D1DeviceContext* c, const D2D1_ROUNDED_RECT& rr, D2D1_COLOR_F col);
ComPtr<ID2D1LinearGradientBrush> Gradient(ID2D1DeviceContext* c, D2D1_POINT_2F a, D2D1_POINT_2F b, std::initializer_list<D2D1_GRADIENT_STOP> stops);
ComPtr<ID2D1RadialGradientBrush> Radial(ID2D1DeviceContext* c, D2D1_POINT_2F center, float rx, float ry, std::initializer_list<D2D1_GRADIENT_STOP> stops);
void Glyph(ID2D1DeviceContext* c, const wchar_t* g, float px, D2D1_RECT_F r, D2D1_COLOR_F col);   // ícone da fonte Segoe MDL2 Assets
// Desenha fora da tela num bitmap WIC (prévias, ícones, papel de parede). Salva PNG/JPG se path não for vazio.
ComPtr<IWICBitmap> Offscreen(int w, int h, const std::function<void(ID2D1DeviceContext*)>& draw);
bool SaveImage(IWICBitmapSource* b, const std::wstring& path);   // .png ou .jpg pela extensão
void FillRect(ID2D1DeviceContext* c, const D2D1_RECT_F& r, D2D1_COLOR_F col);
void Stroke(ID2D1DeviceContext* c, const D2D1_ROUNDED_RECT& rr, D2D1_COLOR_F col, float w);
void Circle(ID2D1DeviceContext* c, float x, float y, float r, D2D1_COLOR_F col);
void Line(ID2D1DeviceContext* c, float x1, float y1, float x2, float y2, D2D1_COLOR_F col, float w, bool round = true);
ComPtr<ID2D1SolidColorBrush> Brush(ID2D1DeviceContext* c, D2D1_COLOR_F col);
inline D2D1_ROUNDED_RECT RR(float l, float t, float r, float b, float rad) { return D2D1::RoundedRect(D2D1::RectF(l, t, r, b), rad, rad); }
}

// Superfície de uma janela layered: desenha com Direct2D num DIB e publica com UpdateLayeredWindow.
class Surface {
public:
    void Attach(HWND h) { hwnd_ = h; }
    ID2D1DeviceContext* Begin(int w, int h);
    void End(int x, int y, BYTE alpha = 255);
    void SetAlpha(int x, int y, BYTE alpha);       // reaplica a última imagem com outra opacidade
    ID2D1Bitmap* Bitmap(IWICBitmapSource* src);    // cache por fonte WIC
    void DrawBitmap(IWICBitmapSource* src, D2D1_RECT_F dst, float opacity = 1.f, bool hq = true);
    void Reset();
    bool SavePng(const std::wstring& path);        // salva o último quadro (prévias/depuração)
    int W() const { return w_; }
    int H() const { return h_; }
    ~Surface() { Reset(); }
private:
    bool Ensure(int w, int h);
    HWND hwnd_ = nullptr;
    HDC mem_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ old_ = nullptr;
    void* bits_ = nullptr;
    int w_ = 0, h_ = 0;
    ComPtr<ID2D1DCRenderTarget> rt_;
    ComPtr<ID2D1DeviceContext> dc_;
    std::unordered_map<IWICBitmapSource*, ComPtr<ID2D1Bitmap>> cache_;
    std::vector<ComPtr<IWICBitmapSource>> keep_;   // mantém as fontes vivas enquanto estão no cache
};
