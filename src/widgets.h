#pragma once
#include "common.h"
#include "gfx.h"

// Widgets de mesa no estilo macOS: data, calendário do mês, relógio e clima (Open-Meteo, sem chave).
class Widgets {
public:
    bool Create();
    void Destroy();
    void Relayout();
    void RedrawAll();
    void OnWeather(void* data);
    void OnForeground(HWND fg);
    bool Owns(HWND h) const;
    void ScanClean();
    void Preview(const std::wstring& dir);   // imagens dos widgets novos (README/testes)
    void RedrawKind(int kind);

    struct W {
        int kind = 0;
        HWND hwnd = nullptr;
        Surface surf;
        int x = 0, y = 0, w = 0, h = 0;
        float lx = 0, ly = 0;   // posição em unidades lógicas (salva na config)
        bool dragging = false, moved = false;
        D2D1_RECT_F btn{};      // botão dentro do widget (Limpeza)
        POINT grab{};
    };

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void Draw(W& w);
    void Place(W& w);
    void FetchWeather();
    void KeepAtDesktop(bool raise);
    std::vector<std::unique_ptr<W>> ws_;
public:
    HWND timerWnd_ = nullptr;
private:
    float s_ = 1;
};

extern Widgets g_widgets;
