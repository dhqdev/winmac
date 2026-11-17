#include "wifiui.h"
#include "art.h"

bool WifiPanel::demo = false;

static std::vector<wifi::Net> DemoNets() {
    std::vector<wifi::Net> v;
    const wchar_t* n[] = { L"Casa do Pingo", L"Casa do Pingo 5G", L"Café Vidro Líquido", L"Biblioteca", L"Vizinho" };
    int q[] = { 92, 80, 64, 40, 22 };
    for (int i = 0; i < 5; ++i) {
        wifi::Net x;
        x.ssid = n[i];
        x.quality = q[i];
        x.secured = i != 3;
        x.connected = i == 0;
        x.hasProfile = i < 2;
        v.push_back(x);
    }
    return v;
}

WifiPanel::WifiPanel() : alive_(std::make_shared<bool>(true)) {
    if (demo) { nets_ = DemoNets(); return; }
    hasAdapter_ = wifi::HasAdapter();
    radio_ = hasAdapter_ && wifi::RadioOn();
    if (hasAdapter_) wifi::Scan();
    lastScan_ = GetTickCount();
    nets_ = wifi::List();
}

WifiPanel::~WifiPanel() { *alive_ = false; }

bool WifiPanel::Refresh(bool scan) {
    if (demo || !hasAdapter_) return false;
    if (scan || GetTickCount() - lastScan_ > 12000) {
        wifi::Scan();
        lastScan_ = GetTickCount();
    }
    bool r = wifi::RadioOn();
    auto n = wifi::List();
    bool changed = r != radio_ || n.size() != nets_.size();
    for (size_t i = 0; !changed && i < n.size(); ++i)
        changed = n[i].ssid != nets_[i].ssid || n[i].connected != nets_[i].connected || std::abs(n[i].quality - nets_[i].quality) > 8;
    radio_ = r;
    nets_ = std::move(n);
    return changed;
}

void WifiPanel::StartConnect(ui::Window& w, const wifi::Net& n, const std::wstring& pass) {
    busy_ = n.ssid;
    status_ = L"Conectando a " + n.ssid + L"…";
    statusErr_ = false;
    HWND h = w.Hwnd();
    auto alive = alive_;
    ui::Window* win = &w;
    std::thread([this, n, pass, h, alive, win]() {
        auto r = wifi::Connect(n, pass);
        RunAsync(h, [this, n, r, alive, win]() {
            if (!*alive) return;
            busy_.clear();
            if (r == wifi::Result::Ok) {
                status_ = L"Conectado a " + n.ssid;
                statusErr_ = false;
                open_.clear();
                pass_.clear();
                if (onConnected) onConnected(n.ssid);
            } else {
                status_ = wifi::Describe(r);
                statusErr_ = true;
                if (r == wifi::Result::Unsupported) OpenPath(L"ms-availablenetworks:");
                else if (n.secured) open_ = n.ssid;
            }
            Refresh();
            win->Invalidate();
        });
    }).detach();
}

float WifiPanel::Draw(ui::Window& w, D2D1_RECT_F a, bool compact) {
    auto& th = w.th;
    float s = w.s, y = a.top;
    float L = a.left, R = a.right;
    if (!hasAdapter_) {
        w.Label(L"Este computador não tem Wi-Fi.", 13, D2D1::RectF(L, y, R, y + 28 * s), th.sub);
        return 28 * s;
    }
    // cabeçalho: Wi-Fi + interruptor
    float hh = compact ? 34 * s : 44 * s;
    w.Label(L"Wi-Fi", compact ? 13.5f : 14.f, D2D1::RectF(L, y, R - 60 * s, y + hh), th.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    bool on = radio_;
    if (w.Toggle(6001, R, y + hh / 2, on)) {
        if (!wifi::SetRadio(on)) OpenPath(L"ms-settings:network-wifi");
        Sleep(150);
        Refresh(true);
    }
    y += hh;
    if (!status_.empty()) {
        w.Label(status_, 12, D2D1::RectF(L, y - 6 * s, R, y + 14 * s), statusErr_ ? gfx::Rgba(255, 90, 80) : th.sub);
        y += 18 * s;
    }
    if (!radio_) {
        w.Label(L"O Wi-Fi está desligado.", 12.5f, D2D1::RectF(L, y, R, y + 26 * s), th.sub);
        return y + 26 * s - a.top;
    }
    gfx::Line(w.c, L, y + 0.5f, R, y + 0.5f, th.sep, 1.f, false);
    y += 6 * s;
    if (nets_.empty()) {
        w.Spinner(L + 10 * s, y + 14 * s, 7 * s, th.sub);
        w.Label(L"Procurando redes…", 12.5f, D2D1::RectF(L + 26 * s, y, R, y + 28 * s), th.sub);
        return y + 28 * s - a.top;
    }
    int idx = 0;
    for (auto& n : nets_) {
        if (++idx > 14) break;
        float rh = 34 * s;
        D2D1_RECT_F row = D2D1::RectF(L - 6 * s, y, R + 6 * s, y + rh);
        int id = 6100 + idx;
        bool expanded = open_ == n.ssid && !n.connected;
        if (w.Row(id, row)) {
            if (n.connected) open_ = open_ == n.ssid ? L"" : n.ssid;
            else if (!busy_.empty()) {}
            else if (!n.secured || n.hasProfile) StartConnect(w, n, L"");
            else if (n.enterprise) OpenPath(L"ms-availablenetworks:");
            else { open_ = n.ssid; pass_.clear(); w.Focus(7001); }
        }
        // ícone de sinal num círculo (azul quando conectado)
        float ix = L + 14 * s, iy = y + rh / 2;
        if (n.connected) gfx::Circle(w.c, ix, iy, 13 * s, th.accent);
        else gfx::Circle(w.c, ix, iy, 13 * s, th.dark ? gfx::White(0.10f) : gfx::Black(0.06f));
        art::WifiBars(w.c, ix, iy + 5 * s, s * 0.85f, n.quality, n.connected ? gfx::White(1) : th.text);
        w.Label(n.ssid, 13, D2D1::RectF(L + 36 * s, y, R - 50 * s, y + rh), th.text, n.connected ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_REGULAR);
        if (busy_ == n.ssid) w.Spinner(R - 12 * s, iy, 7 * s, th.sub);
        else if (n.secured) gfx::Glyph(w.c, L"\xE72E", 12 * s, D2D1::RectF(R - 24 * s, y, R, y + rh), th.sub);
        y += rh;
        if (n.connected && open_ == n.ssid) {
            if (w.Button(6200, D2D1::RectF(L + 36 * s, y + 2 * s, L + 150 * s, y + 30 * s), L"Desconectar")) {
                wifi::Disconnect();
                open_.clear();
                status_.clear();
                Sleep(300);
                Refresh();
            }
            y += 38 * s;
        }
        if (expanded) {
            bool enter = false;
            w.TextField(7001, D2D1::RectF(L + 36 * s, y + 2 * s, R - 6 * s, y + 32 * s), pass_, L"Senha de " + n.ssid, true, &enter);
            y += 40 * s;
            bool go = w.Button(6201, D2D1::RectF(R - 110 * s, y, R - 6 * s, y + 28 * s), L"Conectar", true, pass_.size() >= 8 && busy_.empty());
            if (w.Button(6202, D2D1::RectF(R - 210 * s, y, R - 118 * s, y + 28 * s), L"Cancelar")) { open_.clear(); pass_.clear(); }
            if ((go || (enter && pass_.size() >= 8)) && busy_.empty()) StartConnect(w, n, pass_);
            y += 38 * s;
        }
    }
    y += 4 * s;
    gfx::Line(w.c, L, y + 0.5f, R, y + 0.5f, th.sep, 1.f, false);
    y += 6 * s;
    if (w.LinkButton(6300, D2D1::RectF(L, y, R, y + 24 * s), L"Configurações de rede do Windows…")) OpenPath(L"ms-settings:network-wifi");
    y += 26 * s;
    return y - a.top;
}
