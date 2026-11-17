#pragma once
#include "common.h"
#include "gfx.h"

class MenuBar {
public:
    bool Create();
    void Destroy();
    void Relayout();
    void Reload() { UpdateStatus(); Relayout(); }
    void SetHidden(bool hidden);
    void OnForeground();
    void Render();
    HWND Hwnd() const { return hwnd_; }
    int Height() const { return h_; }

private:
    enum Kind { Logo, AppName, Menu, TrayIcon, Battery, Wifi, Search, Control, Clock };
    struct Elem { Kind kind; int index; D2D1_RECT_F r; std::wstring label; };
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMsg(UINT, WPARAM, LPARAM);
    void Layout(ID2D1DeviceContext* c);
    int HitElem(POINT p) const;
    void OpenMenu(int elemIdx);
    std::vector<struct PMItem> BuildMenu(const Elem& e);
    void RunCommand(int id);
    void ControlCenter();
    void UpdateStatus();
    std::wstring AppTitle(HWND h);

    HWND hwnd_ = nullptr;
    Surface surf_;
    int x_ = 0, y_ = 0, w_ = 0, h_ = 0;
    float s_ = 1;
    std::vector<Elem> elems_;
    int hover_ = -1, open_ = -1;
    bool tracking_ = false, hidden_ = false;
    HWND target_ = nullptr;     // app em primeiro plano (alvo dos menus)
    std::wstring appName_ = L"WinMac";
    HWINEVENTHOOK fgHook_ = nullptr;
    // status
    int batPct_ = 100;
    bool batPresent_ = false, batCharging_ = false;
    bool wifiOn_ = false, wifiWireless_ = false;
    int wifiQ_ = 0;
    std::wstring ssid_;
    std::wstring clock_;
};

extern MenuBar g_menubar;
