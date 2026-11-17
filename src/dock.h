#pragma once
#include "common.h"
#include "gfx.h"

class Dock {
public:
    bool Create();
    void Destroy();
    void OnShellHook(WPARAM code, LPARAM lp, LRESULT* res);
    void ScheduleRefresh(int ms = 60);
    void Relayout();
    void Reload();                     // configuração mudou (Ajustes)
    void SetHidden(bool hidden);       // tela cheia
    HWND Content() const { return content_; }
    HWND Glass() const { return glass_; }
    bool Owns(HWND h) const { return h && (h == content_ || h == glass_); }
    int ReservedHeight() const { return reserve_; }
    void Redraw() { Render(); }
    bool IconFor(HWND h, RECT* screenRect, ComPtr<IWICBitmapSource>* icon);   // ícone do app dessa janela no dock
    double Bench(int frames);         // mede o tempo médio de um quadro com ampliação ativa (ms)

    struct Item {
        enum Kind { App, Launchpad, Folder, Trash, Settings } kind = App;
        std::wstring appId, target, name;
        std::wstring special;   // "::finder": guardado assim na configuração
        bool pinned = false;
        ComPtr<IWICBitmapSource> icon;
        std::vector<HWND> wins;
        float appear = 0.f;
        bool dying = false;
        DWORD bounceStart = 0;
        int bounceCount = 0;
        float x = 0, w = 0, size = 0;   // layout atual (px na janela de conteúdo)
        float iconX = 0, iconY = 0;
        bool sepBefore = false;
    };

private:
    static LRESULT CALLBACK ContentProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMsg(UINT, WPARAM, LPARAM);
    void Refresh();
    void LoadPinned();
    void SavePinned();
    void Layout(float dt);
    void Render();
    void Step();
    void StartAnim();
    int HitItem(POINT p) const;
    void Click(int idx, bool middle);
    void ContextMenu(int idx, POINT screen);
    bool Matches(const Item& it, HWND h) const;
    bool Busy() const;
    void UpdateRecycle();
    void DrawLaunchpad(ID2D1DeviceContext* c, D2D1_RECT_F r);
    void DrawTrash(ID2D1DeviceContext* c, D2D1_RECT_F r, bool full);
    void DrawFolder(ID2D1DeviceContext* c, D2D1_RECT_F r, const std::wstring& id);

    HWND glass_ = nullptr, content_ = nullptr;
    Surface glassSurf_, surf_;
    std::vector<std::unique_ptr<Item>> items_;
    float s_ = 1, B_ = 60, gap_ = 6, padX_ = 10, padY_ = 9, panelH_ = 78, margin_ = 8, mag_ = 1.6f;
    bool magOn_ = true;
    int monX_ = 0, monY_ = 0, monW_ = 0, monH_ = 0;
    int cw_ = 0, ch_ = 0, cx_ = 0, cy_ = 0;
    int reserve_ = 0;
    float amp_ = 0, ampTarget_ = 0;
    float mouseX_ = -1;
    bool hovering_ = false, tracking_ = false, animating_ = false, hidden_ = false;
    int hoverIdx_ = -1;
    int pressIdx_ = -1;
    POINT pressPt_{};
    bool dragging_ = false;
    int dragIdx_ = -1, dropIdx_ = -1;
    float dragX_ = 0, dragY_ = 0;
    DWORD lastTick_ = 0;
    D2D1_RECT_F panel_{};
    std::vector<std::pair<float, float>> sepXs_;   // x do separador, opacidade
    RECT glassRect_{};
    // ocultar automaticamente
    bool autoHide_ = true;
    float slide_ = 1.f, slideTarget_ = 1.f;   // 1 = visível, 0 = escondido abaixo da tela
    HWND trigger_ = nullptr;
    DWORD hideAt_ = 0;
public:
    void Reveal();
    void UpdateAutoHide();
private:
    float OffY() const;
    void PlaceTrigger();
    std::wstring selfExe_;
    bool recycleFull_ = false;
    ULONG recycleNotify_ = 0;
    HWINEVENTHOOK hooks_[3] = {};
};

extern Dock g_dock;
