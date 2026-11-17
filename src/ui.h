#pragma once
#include "common.h"
#include "gfx.h"

// Kit de interface do WinMac: janela de vidro (desfoque do DWM + sombra + cantos arredondados + botões semáforo)
// e controles desenhados no modo "imediato" (cada quadro desenha tudo e informa cliques).
namespace ui {
struct Theme {
    bool dark;
    D2D1_COLOR_F text, sub, faint, sep, panel, group, groupStroke, field, hover, sidebar, accent;
};
Theme Current();

class Window {
public:
    virtual ~Window();
    // lw/lh em unidades lógicas (96 dpi). appWindow = aparece no Dock e no Alt+Tab.
    bool Create(const std::wstring& title, float lw, float lh, bool appWindow, bool topmost = false);
    void Show();
    void Close();
    void Invalidate();
    void Repaint();
    void Resize(float lw, float lh);
    void SetPanelPos(int x, int y);
    bool SaveFrame(const std::wstring& png);    // desenha e salva o quadro (prévias do README)          // canto do painel (sem a margem da sombra), em px de tela
    HWND Hwnd() const { return hwnd_; }
    bool Alive() const { return hwnd_ != nullptr; }
    bool Active() const { return activeWin_; }

    float s = 1;           // escala de DPI
    int W = 0, H = 0;      // painel em px
    int pad = 0;           // margem para a sombra
    float radius = 14;     // raio lógico dos cantos
    Theme th{};

    // ---- controles (chamar dentro de Paint; retângulos em px do painel) ----
    ID2D1DeviceContext* c = nullptr;
    bool Hover(int id, D2D1_RECT_F r);
    bool Clicked(int id, D2D1_RECT_F r);
    bool Button(int id, D2D1_RECT_F r, const std::wstring& text, bool primary = false, bool enabled = true);
    bool LinkButton(int id, D2D1_RECT_F r, const std::wstring& text);
    bool Toggle(int id, float right, float cy, bool& v);   // interruptor; right = borda direita
    bool Slider(int id, D2D1_RECT_F r, float& v);           // v em 0..1
    bool Segmented(int id, D2D1_RECT_F r, const std::vector<std::wstring>& opts, int& sel);
    bool TextField(int id, D2D1_RECT_F r, std::wstring& v, const std::wstring& placeholder, bool password = false, bool* enter = nullptr);
    bool Row(int id, D2D1_RECT_F r, float radius = 6);      // linha clicável com destaque ao passar o mouse
    void Spinner(float cx, float cy, float r, D2D1_COLOR_F col);
    void Progress(D2D1_RECT_F r, float frac);
    void TrafficLights(float x, float cy, bool canZoom = false);
    float Anim(int id, float target, float dur = 0.12f);
    void Label(const std::wstring& t, float px, D2D1_RECT_F r, D2D1_COLOR_F col, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_REGULAR,
               DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT pa = DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    void Paragraph(const std::wstring& t, float px, D2D1_RECT_F r, D2D1_COLOR_F col, DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING);
    float ParagraphHeight(const std::wstring& t, float px, float width);
    // rolagem: tudo entre BeginScroll/EndScroll é deslocado e recortado
    void BeginScroll(D2D1_RECT_F view, float& offset, float contentH);
    void EndScroll();
    void Focus(int id) { focus_ = id; }
    int FocusId() const { return focus_; }
    void KeepAnimating() { again_ = true; }
    float Dt() const { return dt_; }
    float Mx() const { return mx_; }
    float My() const { return my_; }
    bool MouseDown() const { return down_; }

protected:
    virtual void Paint() = 0;
    virtual void OnTimer(UINT_PTR) {}
    virtual void OnClosed() {}
    virtual bool OnKey(UINT) { return false; }
    virtual void OnCloseRequest() { Close(); }
    virtual void OnActiveChanged(bool) {}
    virtual bool OnCustom(UINT, WPARAM, LPARAM) { return false; }   // mensagens WM_APP+ próprias
    float dragH_ = 0;      // altura (px) da faixa do topo usada para arrastar a janela

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMsg(UINT, WPARAM, LPARAM);
    void UpdateHot();
    void Place(int x, int y);
    D2D1_RECT_F Clip(D2D1_RECT_F r) const;

    HWND hwnd_ = nullptr, glass_ = nullptr;
    Surface surf_, glassSurf_;
    int x_ = 0, y_ = 0;    // canto da janela de conteúdo (inclui pad)
    float mx_ = -1e6f, my_ = -1e6f, wheel_ = 0;
    bool down_ = false, pressed_ = false, released_ = false, dbl_ = false;
    std::wstring typed_;
    std::vector<UINT> keys_;
    int hot_ = 0, active_ = 0, focus_ = 0;
    bool again_ = false, pending_ = false, activeWin_ = true, minimized_ = false, tracking_ = false;
    std::vector<std::pair<int, D2D1_RECT_F>> hits_, lastHits_;
    std::map<int, float> anim_;
    DWORD lastFrame_ = 0, caretTick_ = 0;
    float dt_ = 0;
    bool dragging_ = false;
    POINT dragPt_{};
    int dragX_ = 0, dragY_ = 0;
    struct ScrollCtx { D2D1_RECT_F view; float off; D2D1_MATRIX_3X2_F old; D2D1_RECT_F oldClip; float oldDy; };
    std::vector<ScrollCtx> scroll_;
    D2D1_RECT_F clip_{ -1e6f, -1e6f, 1e6f, 1e6f };
    float dy_ = 0;
    std::map<int, float*> scrollTargets_;
};
}  // namespace ui
