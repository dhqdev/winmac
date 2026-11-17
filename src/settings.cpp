#include "settings.h"
#include "ui.h"
#include "wifiui.h"
#include "art.h"
#include "config.h"
#include "install.h"
#include "update.h"
#include "net.h"
#include "sys.h"
#include "version.h"
#include <shobjidl.h>

namespace settings {
namespace {
enum Page { PWifi, PBluetooth, PGeral, PUpdate, PAparencia, PPapel, PDock, PBarra, PJanelas, PArquivos, PApps, PSom, PSobre, PCount };
struct PageInfo { const wchar_t* key; const wchar_t* title; const wchar_t* glyph; int r, g, b; };
const PageInfo kPages[PCount] = {
    { L"wifi", L"Wi-Fi", L"\xE701", 10, 132, 255 },
    { L"bluetooth", L"Bluetooth", L"\xE702", 10, 132, 255 },
    { L"geral", L"Geral", L"\xE713", 142, 142, 147 },
    { L"update", L"Atualização de Software", L"\xE895", 52, 120, 246 },
    { L"aparencia", L"Aparência", L"\xE771", 40, 40, 44 },
    { L"papel", L"Papel de Parede", L"\xE8B9", 40, 180, 230 },
    { L"dock", L"Mesa e Dock", L"\xE71D", 40, 40, 44 },
    { L"barra", L"Barra de Menus", L"\xE700", 40, 40, 44 },
    { L"janelas", L"Janelas e Alt+Tab", L"\xE7C4", 94, 92, 230 },
    { L"arquivos", L"Arquivos (Explorador)", L"\xE8B7", 52, 150, 230 },
    { L"apps", L"Apps e Limpeza", L"\xE74D", 255, 69, 58 },
    { L"som", L"Som e Tela", L"\xE767", 255, 55, 95 },
    { L"sobre", L"Sobre o WinMac", L"\xE946", 142, 142, 147 },
};
const int kGroups[] = { PBluetooth, PUpdate, PJanelas, PSom };   // linha extra depois destes
const UINT WM_GOTO = WM_APP + 40;
enum { T_REFRESH = 10, T_ANIMPAGE = 11 };

struct City { std::wstring name, region; double lat, lon; };

DWORD RegDw(const wchar_t* key, const wchar_t* name, DWORD def) {
    DWORD v = def, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS) return def;
    return v;
}
void SetRegDw(const wchar_t* key, const wchar_t* name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(k);
    }
}
const wchar_t* kAdv = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
const wchar_t* kExp = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer";

void RefreshExplorer() {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    DWORD_PTR r;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ShellState", SMTO_ABORTIFHUNG, 300, &r);
}

std::wstring HklmStr(const wchar_t* key, const wchar_t* name) {
    wchar_t buf[256] = {};
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS) return L"";
    return buf;
}

class Ajustes : public ui::Window {
public:
    Page page_ = PGeral;
    bool quitOnClose = true;

    void Go(Page p) {
        page_ = p;
        focusPage();
        Invalidate();
    }

    void focusPage() {
        Focus(0);
        if (page_ == PUpdate && !checking_ && !upd_.ok && upd_.error.empty()) StartCheck();
        if (page_ == PApps) ScanApps();
        if (page_ == PPapel && thumbs_.empty())
            for (int i = 0; i < (int)art::Wallpapers().size(); ++i) thumbs_.push_back(art::RenderWallpaper(i, 288, 162));
        if (page_ == PSom) {
            vol_ = sys::GetVolume(&muted_);
            bright_ = (float)sys::GetBrightness();
        }
        if (page_ == PSobre) SetTimer(Hwnd(), T_ANIMPAGE, 33, nullptr);
        else KillTimer(Hwnd(), T_ANIMPAGE);
    }

protected:
    // ---------- estado ----------
    float sideW_ = 0;
    float scroll_[PCount] = {}, contentH_[PCount] = {};
    WifiPanel wifi_;
    float wifiH_ = 200;
    update::Info upd_;
    bool checking_ = false, downloading_ = false;
    float dlProg_ = 0;
    std::wstring updMsg_;
    std::atomic<bool> cancel_{ false };
    std::wstring cityQuery_;
    std::vector<City> cities_;
    bool searching_ = false;
    std::wstring cityMsg_;
    std::vector<int> paid_;
    bool removing_ = false;
    std::wstring appsMsg_;
    std::vector<ComPtr<IWICBitmap>> thumbs_;
    bool wallBusy_ = false;
    float vol_ = 0.5f, bright_ = -1;
    bool muted_ = false;
    WPARAM pendingNotify_ = (WPARAM)-1;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    // grupos
    float L_ = 0, R_ = 0, ry_ = 0;
    bool meas_ = false;
    int rowN_ = 0, id_ = 0;

    int Id() { return ++id_; }

    void Notify(WPARAM what, bool now = false) {
        gfx::ReloadTheme();
        if (now || !MouseDown()) NotifyShell(what);
        else pendingNotify_ = what;
    }

    void Async(std::function<void()> work, std::function<void()> done) {
        HWND h = Hwnd();
        auto alive = alive_;
        std::thread([work, done, h, alive]() {
            work();
            RunAsync(h, [done, alive]() { if (*alive) done(); });
        }).detach();
    }

    // ---------- linhas de grupo ----------
    void Group(float& y, const std::function<void()>& body) {
        meas_ = true;
        ry_ = y;
        rowN_ = 0;
        body();
        float h = ry_ - y;
        meas_ = false;
        auto rr = gfx::RR(L_, y, R_, y + h, 10 * s);
        gfx::Fill(c, rr, th.group);
        gfx::Stroke(c, rr, th.groupStroke, 1.f);
        ry_ = y;
        rowN_ = 0;
        body();
        y = ry_ + 18 * s;
    }
    bool RowBegin(float h, float* top) {
        float t = ry_;
        ry_ += h;
        if (meas_) return false;
        if (rowN_++ > 0) gfx::Line(c, L_ + 14 * s, t + 0.5f, R_ - 14 * s, t + 0.5f, th.sep, 1.f, false);
        *top = t;
        return true;
    }
    void RowText(float t, float h, const std::wstring& label, const std::wstring& sub, float right) {
        if (sub.empty()) Label(label, 13, D2D1::RectF(L_ + 14 * s, t, right, t + h), th.text);
        else {
            Label(label, 13, D2D1::RectF(L_ + 14 * s, t + 8 * s, right, t + h / 2 + 2 * s), th.text, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_FAR);
            Label(sub, 11.5f, D2D1::RectF(L_ + 14 * s, t + h / 2 + 2 * s, right, t + h - 6 * s), th.sub, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    }
    bool RowToggle(const std::wstring& label, const std::wstring& sub, bool& v) {
        float h = sub.empty() ? 44 * s : 56 * s, t;
        if (!RowBegin(h, &t)) return false;
        RowText(t, h, label, sub, R_ - 70 * s);
        return Toggle(Id(), R_ - 14 * s, t + h / 2, v);
    }
    bool RowSlider(const std::wstring& label, float& v, const std::wstring& value, bool enabled = true) {
        float h = 50 * s, t;
        if (!RowBegin(h, &t)) return false;
        float mid = L_ + (R_ - L_) * 0.42f;
        Label(label, 13, D2D1::RectF(L_ + 14 * s, t, mid, t + h), enabled ? th.text : th.faint);
        Label(value, 12, D2D1::RectF(R_ - 70 * s, t, R_ - 14 * s, t + h), th.sub, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (!enabled) return false;
        return Slider(Id(), D2D1::RectF(mid, t, R_ - 80 * s, t + h), v);
    }
    bool RowSegment(const std::wstring& label, const std::vector<std::wstring>& opts, int& sel) {
        float h = 48 * s, t;
        if (!RowBegin(h, &t)) return false;
        float segW = std::min((R_ - L_) * 0.55f, opts.size() * 110 * s);
        Label(label, 13, D2D1::RectF(L_ + 14 * s, t, R_ - segW - 20 * s, t + h), th.text);
        return Segmented(Id(), D2D1::RectF(R_ - 14 * s - segW, t + 10 * s, R_ - 14 * s, t + h - 10 * s), opts, sel);
    }
    bool RowButton(const std::wstring& label, const std::wstring& sub, const std::wstring& btn, bool primary = false, bool enabled = true) {
        float h = sub.empty() ? 46 * s : 58 * s, t;
        if (!RowBegin(h, &t)) return false;
        float bw = gfx::Measure(btn, gfx::Font(13 * s, DWRITE_FONT_WEIGHT_MEDIUM)).width + 28 * s;
        RowText(t, h, label, sub, R_ - bw - 24 * s);
        return Button(Id(), D2D1::RectF(R_ - 14 * s - bw, t + h / 2 - 13 * s, R_ - 14 * s, t + h / 2 + 13 * s), btn, primary, enabled);
    }
    void RowInfo(const std::wstring& label, const std::wstring& value) {
        float h = 40 * s, t;
        if (!RowBegin(h, &t)) return;
        Label(label, 13, D2D1::RectF(L_ + 14 * s, t, L_ + (R_ - L_) * 0.4f, t + h), th.text);
        Label(value, 13, D2D1::RectF(L_ + (R_ - L_) * 0.4f, t, R_ - 14 * s, t + h), th.sub, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    bool RowCustom(float h, float* top) { return RowBegin(h, top); }
    void Note(float& y, const std::wstring& t) {
        float h = ParagraphHeight(t, 12, R_ - L_ - 8 * s);
        Paragraph(t, 12, D2D1::RectF(L_ + 4 * s, y, R_ - 4 * s, y + h + 4 * s), th.sub);
        y += h + 14 * s;
    }
    void Caption(float& y, const std::wstring& t) {
        Label(t, 12, D2D1::RectF(L_ + 6 * s, y, R_, y + 20 * s), th.sub, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        y += 24 * s;
    }

    // ---------- ações ----------
    void StartCheck() {
        checking_ = true;
        updMsg_.clear();
        auto res = std::make_shared<update::Info>();
        Async([res]() { *res = update::Check(); }, [this, res]() {
            checking_ = false;
            upd_ = *res;
            update::SaveAvailable(upd_);
            NotifyShell(RL_MENUBAR);
            Invalidate();
        });
    }
    void StartDownload() {
        downloading_ = true;
        dlProg_ = 0;
        cancel_ = false;
        updMsg_ = L"Baixando…";
        auto info = upd_;
        auto err = std::make_shared<std::wstring>();
        HWND h = Hwnd();
        auto alive = alive_;
        Async([info, err, h, alive, this]() {
            *err = update::DownloadAndRun(info, [h, alive, this](float f) {
                RunAsync(h, [alive, this, f]() { if (*alive) { dlProg_ = f; Invalidate(); } });
            }, &cancel_);
        }, [this, err]() {
            downloading_ = false;
            if (err->empty()) {
                updMsg_ = L"Instalando a nova versão… o WinMac vai reiniciar.";
                SetTimer(Hwnd(), 99, 1500, nullptr);
            } else updMsg_ = *err;
            Invalidate();
        });
    }
    void ScanApps() {
        paid_.clear();
        auto& all = install::PaidApps();
        for (int i = 0; i < (int)all.size(); ++i)
            if (install::IsPackageInstalled(all[i].family)) paid_.push_back(i);
    }
    void Remove(std::vector<std::wstring> fams) {
        removing_ = true;
        appsMsg_ = L"Removendo… isso pode levar um minutinho.";
        Async([fams]() { install::RemovePackages(fams); }, [this]() {
            removing_ = false;
            ScanApps();
            appsMsg_ = paid_.empty() ? L"Pronto! Nada mais para remover." : L"Alguns apps não puderam ser removidos.";
            Invalidate();
        });
    }
    void SearchCity() {
        if (cityQuery_.size() < 2 || searching_) return;
        searching_ = true;
        cityMsg_.clear();
        auto q = cityQuery_;
        auto out = std::make_shared<std::vector<City>>();
        Async([q, out]() {
            std::string body = net::Get(L"https://geocoding-api.open-meteo.com/v1/search?count=6&language=pt&format=json&name=" + net::UrlEncode(q));
            net::JVal j;
            if (!net::ParseJson(body, j)) return;
            if (auto* r = j.Get("results"))
                for (auto& it : r->a) {
                    City cty;
                    if (auto* n = it.Get("name")) cty.name = n->W();
                    std::wstring a1 = it.Get("admin1") ? it.Get("admin1")->W() : L"", co = it.Get("country") ? it.Get("country")->W() : L"";
                    cty.region = a1.empty() ? co : a1 + L", " + co;
                    cty.lat = it.Get("latitude") ? it.Get("latitude")->Number() : 0;
                    cty.lon = it.Get("longitude") ? it.Get("longitude")->Number() : 0;
                    out->push_back(cty);
                }
        }, [this, out]() {
            searching_ = false;
            cities_ = *out;
            if (cities_.empty()) cityMsg_ = L"Nenhuma cidade encontrada.";
            Invalidate();
        });
    }
    void PickWallpaperFile() {
        ComPtr<IFileOpenDialog> d;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return;
        COMDLG_FILTERSPEC f[] = { { L"Imagens", L"*.jpg;*.jpeg;*.png;*.bmp;*.webp" } };
        d->SetFileTypes(1, f);
        d->SetTitle(L"Escolha um papel de parede");
        if (FAILED(d->Show(Hwnd()))) return;
        ComPtr<IShellItem> it;
        PWSTR p = nullptr;
        if (SUCCEEDED(d->GetResult(&it)) && SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
            install::ApplyWallpaperFile(p);
            cfg::SetInt(L"Screen", L"WallpaperPreset", -1);
            CoTaskMemFree(p);
            NotifyShell(RL_WALLPAPER);
        }
    }

    // ---------- desenho ----------
    void Sidebar() {
        float y = 50 * s;
        // cartão do topo
        D2D1_RECT_F card = D2D1::RectF(12 * s, y, sideW_ - 12 * s, y + 56 * s);
        if (Row(90, card, 8)) Go(PSobre);
        art::Pose p;
        p.t = 0.9f;
        p.shadow = false;
        p.arms = false;
        gfx::Circle(c, card.left + 26 * s, y + 28 * s, 20 * s, th.dark ? gfx::White(0.10f) : gfx::White(0.8f));
        art::Pingo(c, card.left + 26 * s, y + 28 * s, 30 * s, p);
        Label(L"WinMac", 14, D2D1::RectF(card.left + 54 * s, y + 8 * s, card.right, y + 30 * s), th.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        Label(L"Versão " WINMAC_VERSION, 11.5f, D2D1::RectF(card.left + 54 * s, y + 28 * s, card.right, y + 48 * s), th.sub);
        y += 66 * s;
        bool upAvail = !cfg::Str(L"Update", L"Available").empty() || upd_.newer;
        for (int i = 0; i < PCount; ++i) {
            D2D1_RECT_F r = D2D1::RectF(10 * s, y, sideW_ - 10 * s, y + 30 * s);
            bool sel = i == page_;
            if (sel) gfx::Fill(c, gfx::RR(r.left, r.top, r.right, r.bottom, 7 * s), Active() ? th.accent : (th.dark ? gfx::White(0.12f) : gfx::Black(0.08f)));
            else if (Row(100 + i, r, 7)) Go((Page)i);
            if (sel) Hover(100 + i, r);
            auto& pi = kPages[i];
            art::SectionIcon(c, D2D1::RectF(r.left + 8 * s, r.top + 4 * s, r.left + 30 * s, r.top + 26 * s), gfx::Rgba(pi.r, pi.g, pi.b), pi.glyph);
            Label(pi.title, 13, D2D1::RectF(r.left + 38 * s, r.top, r.right - 24 * s, r.bottom), sel && Active() ? gfx::White(1) : th.text);
            if (i == PUpdate && upAvail) gfx::Circle(c, r.right - 14 * s, (r.top + r.bottom) / 2, 4.5f * s, gfx::Rgba(255, 69, 58));
            y += 31 * s;
            for (int g : kGroups)
                if (g == i) y += 10 * s;
        }
    }

    void Paint() override {
        sideW_ = 236 * s;
        dragH_ = 52 * s;
        // barra lateral um pouco mais escura, recortada nos cantos do painel
        ComPtr<ID2D1RoundedRectangleGeometry> clip;
        gfx::d2d->CreateRoundedRectangleGeometry(gfx::RR(0, 0, (float)W, (float)H, radius * s), &clip);
        c->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
        gfx::FillRect(c, D2D1::RectF(0, 0, sideW_, (float)H), th.sidebar);
        c->PopLayer();
        gfx::Line(c, sideW_ + 0.5f, 0, sideW_ + 0.5f, (float)H, th.sep, 1.f, false);
        TrafficLights(20 * s, 26 * s);
        Sidebar();
        // conteúdo
        L_ = sideW_ + 28 * s;
        R_ = W - 28 * s;
        Label(kPages[page_].title, 17, D2D1::RectF(L_, 10 * s, R_, 46 * s), th.text, DWRITE_FONT_WEIGHT_BOLD);
        D2D1_RECT_F view = D2D1::RectF(sideW_ + 1, 52 * s, (float)W - 2 * s, (float)H - 6 * s);
        BeginScroll(view, scroll_[page_], contentH_[page_]);
        float y = view.top + 6 * s;
        id_ = 1000;
        switch (page_) {
        case PWifi: PageWifi(y); break;
        case PBluetooth: PageBluetooth(y); break;
        case PGeral: PageGeral(y); break;
        case PUpdate: PageUpdate(y); break;
        case PAparencia: PageAparencia(y); break;
        case PPapel: PagePapel(y); break;
        case PDock: PageDock(y); break;
        case PBarra: PageBarra(y); break;
        case PJanelas: PageJanelas(y); break;
        case PArquivos: PageArquivos(y); break;
        case PApps: PageApps(y); break;
        case PSom: PageSom(y); break;
        case PSobre: PageSobre(y, view); break;
        default: break;
        }
        float h = y - view.top + 10 * s;
        if (std::fabs(h - contentH_[page_]) > 1) { contentH_[page_] = h; KeepAnimating(); }
        EndScroll();
        if (pendingNotify_ != (WPARAM)-1 && !MouseDown()) {
            NotifyShell(pendingNotify_);
            pendingNotify_ = (WPARAM)-1;
        }
    }

    void PageWifi(float& y) {
        auto rr = gfx::RR(L_, y, R_, y + wifiH_, 10 * s);
        gfx::Fill(c, rr, th.group);
        gfx::Stroke(c, rr, th.groupStroke, 1.f);
        float h = wifi_.Draw(*this, D2D1::RectF(L_ + 14 * s, y + 4 * s, R_ - 14 * s, y + 2000 * s)) + 8 * s;
        if (std::fabs(h - wifiH_) > 1) { wifiH_ = h; KeepAnimating(); }
        y += wifiH_ + 16 * s;
        Note(y, L"Dica: o ícone de Wi-Fi na barra de menus também mostra esta lista.");
    }

    void PageBluetooth(float& y) {
        Group(y, [&] {
            if (RowButton(L"Bluetooth e dispositivos", L"Parear fones, mouses e teclados", L"Abrir…")) OpenPath(L"ms-settings:bluetooth");
        });
        Note(y, L"O Windows não permite que outros apps liguem o Bluetooth diretamente; o painel do Windows abre já na página certa.");
    }

    void PageGeral(float& y) {
        static std::wstring pc, winver, cpu, mem;
        if (pc.empty()) {
            wchar_t b[256];
            DWORD n = 256;
            if (GetComputerNameW(b, &n)) pc = b;
            const wchar_t* k = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
            winver = HklmStr(k, L"ProductName") + L" " + HklmStr(k, L"DisplayVersion") + L" (build " + HklmStr(k, L"CurrentBuild") + L")";
            cpu = HklmStr(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
            while (!cpu.empty() && cpu.back() == L' ') cpu.pop_back();
            MEMORYSTATUSEX ms{ sizeof(ms) };
            GlobalMemoryStatusEx(&ms);
            mem = std::to_wstring((int)std::round(ms.ullTotalPhys / 1073741824.0)) + L" GB";
        }
        Caption(y, L"SOBRE ESTE COMPUTADOR");
        Group(y, [&] {
            RowInfo(L"Nome", pc);
            RowInfo(L"Sistema", winver);
            RowInfo(L"Processador", cpu);
            RowInfo(L"Memória", mem);
            RowInfo(L"WinMac", L"Versão " WINMAC_VERSION);
        });
        Group(y, [&] {
            bool a = install::Autostart();
            if (RowToggle(L"Abrir o WinMac ao ligar o computador", L"", a)) install::SetAutostart(a);
            if (RowButton(L"Boas-vindas", L"Rever a apresentação do Pingo e as escolhas iniciais", L"Mostrar"))
                ShellExecuteW(nullptr, L"open", SelfPath().c_str(), L"--welcome", nullptr, SW_SHOWNORMAL);
            if (RowButton(L"Pasta do WinMac", L"Configuração e arquivos", L"Abrir")) OpenPath(install::Dir());
            if (RowButton(L"Reiniciar o WinMac", L"Recarrega Dock, barra e widgets", L"Reiniciar")) NotifyShell(RL_ALL);
        });
        Group(y, [&] {
            if (RowButton(L"Desinstalar o WinMac", L"Volta a barra de tarefas, cursores e papel de parede anteriores", L"Desinstalar…"))
                ShellExecuteW(nullptr, L"open", install::InstalledExe().c_str(), L"--uninstall", nullptr, SW_SHOWNORMAL);
        });
    }

    void PageUpdate(float& y) {
        float h = 110 * s;
        auto rr = gfx::RR(L_, y, R_, y + h, 12 * s);
        gfx::Fill(c, rr, th.group);
        gfx::Stroke(c, rr, th.groupStroke, 1.f);
        art::AppIcon(c, D2D1::RectF(L_ + 20 * s, y + 21 * s, L_ + 88 * s, y + 89 * s));
        float tx = L_ + 106 * s;
        std::wstring status;
        bool avail = upd_.newer;
        if (checking_) status = L"Procurando atualizações…";
        else if (!upd_.error.empty()) status = upd_.error;
        else if (avail) status = L"Nova versão disponível: WinMac " + upd_.version;
        else if (upd_.ok) status = L"O WinMac está atualizado.";
        Label(L"WinMac " WINMAC_VERSION, 16, D2D1::RectF(tx, y + 18 * s, R_ - 20 * s, y + 42 * s), th.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        Label(status, 12.5f, D2D1::RectF(tx, y + 42 * s, R_ - 20 * s, y + 62 * s), avail ? th.accent : th.sub);
        if (checking_) Spinner(R_ - 34 * s, y + 30 * s, 8 * s, th.sub);
        float by = y + 70 * s;
        if (downloading_) {
            Progress(D2D1::RectF(tx, by + 8 * s, R_ - 130 * s, by + 14 * s), dlProg_);
            if (Button(Id(), D2D1::RectF(R_ - 116 * s, by, R_ - 20 * s, by + 26 * s), L"Cancelar")) cancel_ = true;
        } else if (avail) {
            if (Button(Id(), D2D1::RectF(tx, by, tx + 150 * s, by + 28 * s), L"Atualizar agora", true)) StartDownload();
            if (LinkButton(Id(), D2D1::RectF(tx + 166 * s, by, tx + 300 * s, by + 28 * s), L"Ver no GitHub")) OpenPath(upd_.page);
        } else {
            if (Button(Id(), D2D1::RectF(tx, by, tx + 150 * s, by + 28 * s), L"Verificar agora", false, !checking_)) StartCheck();
        }
        y += h + 12 * s;
        if (!updMsg_.empty()) Note(y, updMsg_);
        if (avail && !upd_.notes.empty()) {
            Caption(y, L"NOVIDADES");
            float ph = ParagraphHeight(upd_.notes, 12.5f, R_ - L_ - 28 * s);
            ph = std::min(ph, 260 * s);
            auto nr = gfx::RR(L_, y, R_, y + ph + 24 * s, 10 * s);
            gfx::Fill(c, nr, th.group);
            c->PushAxisAlignedClip(D2D1::RectF(L_, y, R_, y + ph + 20 * s), D2D1_ANTIALIAS_MODE_ALIASED);
            Paragraph(upd_.notes, 12.5f, D2D1::RectF(L_ + 14 * s, y + 12 * s, R_ - 14 * s, y + ph + 12 * s), th.text);
            c->PopAxisAlignedClip();
            y += ph + 40 * s;
        }
        Group(y, [&] {
            bool a = cfg::Int(L"Update", L"Auto", 1) != 0;
            if (RowToggle(L"Procurar atualizações automaticamente", L"Avisa com uma notificação quando sair versão nova", a)) cfg::SetInt(L"Update", L"Auto", a);
        });
        Note(y, L"As atualizações vêm das Releases do GitHub (github.com/" WINMAC_REPO L"). O arquivo baixado é conferido pelo resumo SHA-256 publicado pelo GitHub antes de instalar.");
    }

    void ThemeCard(D2D1_RECT_F r, int kind, bool sel, const wchar_t* name, int id) {
        if (Clicked(id, r)) { install::ApplyTheme(kind); Notify(RL_THEME, true); }
        float ph = r.bottom - r.top - 24 * s;
        auto pr = gfx::RR(r.left, r.top, r.right, r.top + ph, 8 * s);
        auto mini = [&](D2D1_RECT_F a, bool dk) {
            auto g = gfx::Gradient(c, D2D1::Point2F(a.left, a.top), D2D1::Point2F(a.right, a.bottom),
                                   { { 0.f, dk ? gfx::Rgba(24, 30, 64) : gfx::Rgba(200, 225, 255) }, { 1.f, dk ? gfx::Rgba(80, 40, 110) : gfx::Rgba(255, 215, 235) } });
            c->FillRectangle(a, g.Get());
            float w = r.right - r.left;
            gfx::FillRect(c, D2D1::RectF(a.left, a.top, a.right, a.top + 8 * s), dk ? gfx::Black(0.35f) : gfx::White(0.55f));
            auto win = gfx::RR(r.left + w * 0.18f, a.top + 16 * s, r.left + w * 0.78f, a.top + ph * 0.62f, 4 * s);
            gfx::Fill(c, win, dk ? gfx::Rgba(40, 40, 46, 0.95f) : gfx::Rgba(250, 250, 252, 0.95f));
            for (int i = 0; i < 3; ++i) gfx::Circle(c, win.rect.left + (5 + i * 6) * s, win.rect.top + 5 * s, 2 * s, i == 0 ? gfx::Rgba(255, 95, 87) : i == 1 ? gfx::Rgba(254, 188, 46) : gfx::Rgba(40, 200, 64));
            gfx::Fill(c, gfx::RR(r.left + w * 0.25f, a.top + ph - 14 * s, r.left + w * 0.75f, a.top + ph - 5 * s, 3 * s), dk ? gfx::White(0.25f) : gfx::White(0.7f));
        };
        ComPtr<ID2D1RoundedRectangleGeometry> g;
        gfx::d2d->CreateRoundedRectangleGeometry(pr, &g);
        c->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g.Get()), nullptr);
        if (kind == 2) {
            float mx = (r.left + r.right) / 2;
            c->PushAxisAlignedClip(D2D1::RectF(r.left, r.top, mx, r.top + ph), D2D1_ANTIALIAS_MODE_ALIASED);
            mini(D2D1::RectF(r.left, r.top, r.right, r.top + ph), false);
            c->PopAxisAlignedClip();
            c->PushAxisAlignedClip(D2D1::RectF(mx, r.top, r.right, r.top + ph), D2D1_ANTIALIAS_MODE_ALIASED);
            mini(D2D1::RectF(r.left, r.top, r.right, r.top + ph), true);
            c->PopAxisAlignedClip();
        } else mini(D2D1::RectF(r.left, r.top, r.right, r.top + ph), kind == 1);
        c->PopLayer();
        gfx::Stroke(c, pr, sel ? th.accent : th.groupStroke, sel ? 3 * s : 1.f);
        Label(name, 12.5f, D2D1::RectF(r.left, r.top + ph + 2 * s, r.right, r.bottom), sel ? th.text : th.sub, sel ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void PageAparencia(float& y) {
        int theme = cfg::Int(L"Appearance", L"Theme", gfx::DarkMode() ? 1 : 0);
        Group(y, [&] {
            float t;
            if (!RowCustom(150 * s, &t)) return;
            Label(L"Aparência", 13, D2D1::RectF(L_ + 14 * s, t + 6 * s, R_, t + 30 * s), th.text);
            float cw = (R_ - L_ - 28 * s - 2 * 16 * s) / 3;
            const wchar_t* names[3] = { L"Claro", L"Escuro", L"Automático" };
            int order[3] = { 0, 1, 2 };
            for (int i = 0; i < 3; ++i) {
                float x = L_ + 14 * s + i * (cw + 16 * s);
                ThemeCard(D2D1::RectF(x, t + 34 * s, x + cw, t + 142 * s), order[i], theme == order[i], names[i], 1500 + i);
            }
        });
        Group(y, [&] {
            float t;
            if (RowCustom(50 * s, &t)) {
                Label(L"Cor de destaque", 13, D2D1::RectF(L_ + 14 * s, t, R_, t + 50 * s), th.text);
                int cur = cfg::Int(L"Appearance", L"Accent", 0);
                for (int i = 0; i < gfx::kAccentCount; ++i) {
                    float cx = R_ - 14 * s - 10 * s - (gfx::kAccentCount - 1 - i) * 28 * s, cy = t + 25 * s;
                    if (Clicked(1600 + i, D2D1::RectF(cx - 12 * s, cy - 12 * s, cx + 12 * s, cy + 12 * s))) {
                        cfg::SetInt(L"Appearance", L"Accent", i);
                        Notify(RL_THEME, true);
                    }
                    gfx::Circle(c, cx, cy, 10 * s, gfx::AccentOf(i));
                    if (i == cur) {
                        gfx::Circle(c, cx, cy, 3.5f * s, gfx::White(1));
                        c->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 12.5f * s, 12.5f * s), gfx::Brush(c, gfx::AccentOf(i)).Get(), 1.5f * s);
                    }
                }
            }
            float g = (cfg::Int(L"Appearance", L"Glass", 100) - 20) / 140.f;
            if (RowSlider(L"Brilho do vidro líquido", g, std::to_wstring((int)std::round(20 + g * 140)) + L"%")) {
                cfg::SetInt(L"Appearance", L"Glass", (int)std::round(20 + g * 140));
                Notify(RL_THEME);
            }
            bool rt = cfg::Int(L"Appearance", L"ReduceTransparency", 0) != 0;
            if (RowToggle(L"Reduzir transparência", L"Fundos mais sólidos e fáceis de ler", rt)) {
                cfg::SetInt(L"Appearance", L"ReduceTransparency", rt);
                Notify(RL_THEME, true);
            }
        });
        Group(y, [&] {
            bool mc = cfg::Int(L"Appearance", L"MacCursors", 0) != 0;
            if (RowToggle(L"Cursores estilo Mac", L"Seta e mãozinha arredondadas", mc)) install::ApplyCursors(mc);
            bool rc = cfg::Int(L"Screen", L"RoundedCorners", 1) != 0;
            if (RowToggle(L"Cantos da tela arredondados", L"Como a tela de um MacBook", rc)) {
                cfg::SetInt(L"Screen", L"RoundedCorners", rc);
                Notify(RL_ALL, true);
            }
        });
    }

    void PagePapel(float& y) {
        int cur = cfg::Int(L"Screen", L"WallpaperPreset", -1);
        auto& ws = art::Wallpapers();
        int per = 3;
        float gap = 16 * s, tw = (R_ - L_ - (per - 1) * gap) / per, thh = tw * 9 / 16;
        for (int i = 0; i < (int)ws.size(); ++i) {
            float x = L_ + (i % per) * (tw + gap), yy = y + (i / per) * (thh + 34 * s);
            D2D1_RECT_F r = D2D1::RectF(x, yy, x + tw, yy + thh);
            bool hov = Hover(1700 + i, r);
            if (Clicked(1700 + i, r) && !wallBusy_) {
                wallBusy_ = true;
                int idx = i;
                Async([idx]() { install::ApplyWallpaper(idx); }, [this]() { wallBusy_ = false; NotifyShell(RL_WALLPAPER); Invalidate(); });
                cfg::SetInt(L"Screen", L"WallpaperPreset", i);
            }
            auto rr = gfx::RR(r.left, r.top, r.right, r.bottom, 8 * s);
            if (i < (int)thumbs_.size() && thumbs_[i]) {
                ComPtr<ID2D1Bitmap> bm;
                if (SUCCEEDED(c->CreateBitmapFromWicBitmap(thumbs_[i].Get(), nullptr, &bm))) {
                    ComPtr<ID2D1RoundedRectangleGeometry> g;
                    gfx::d2d->CreateRoundedRectangleGeometry(rr, &g);
                    c->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g.Get()), nullptr);
                    c->DrawBitmap(bm.Get(), r, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
                    c->PopLayer();
                }
            }
            bool sel = i == cur;
            gfx::Stroke(c, rr, sel ? th.accent : (hov ? th.sub : th.groupStroke), sel ? 3 * s : 1.f);
            if (sel && wallBusy_) Spinner((r.left + r.right) / 2, (r.top + r.bottom) / 2, 10 * s, gfx::White(0.9f));
            Label(ws[i].name, 12.5f, D2D1::RectF(r.left, r.bottom + 4 * s, r.right, r.bottom + 26 * s), sel ? th.text : th.sub, sel ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        y += ((int)ws.size() + per - 1) / per * (thh + 34 * s) + 8 * s;
        Group(y, [&] {
            if (RowButton(L"Usar uma foto sua", L"JPG, PNG ou BMP", L"Escolher…")) PickWallpaperFile();
            bool keep = cfg::Int(L"Screen", L"Enforce", 1) != 0;
            if (RowToggle(L"Impedir que o Windows troque o papel de parede", L"Desliga o \"Windows Spotlight\" se ele voltar sozinho", keep)) cfg::SetInt(L"Screen", L"Enforce", keep);
        });
        Note(y, L"Os papéis de parede do WinMac são desenhados pelo próprio app, no tamanho exato da sua tela.");
    }

    void PageDock(float& y) {
        Caption(y, L"DOCK");
        Group(y, [&] {
            float sz = (cfg::Int(L"Dock", L"IconSize", 50) - 32) / 48.f;
            if (RowSlider(L"Tamanho", sz, std::to_wstring(32 + (int)std::round(sz * 48)) + L" px")) {
                cfg::SetInt(L"Dock", L"IconSize", 32 + (int)std::round(sz * 48));
                Notify(RL_DOCK);
            }
            bool mag = cfg::Int(L"Dock", L"Magnification", 1) != 0;
            if (RowToggle(L"Ampliação", L"Os ícones crescem quando o mouse passa", mag)) { cfg::SetInt(L"Dock", L"Magnification", mag); Notify(RL_DOCK, true); }
            float ms = (cfg::Float(L"Dock", L"MagnifyScale", 1.6f) - 1.2f) / 1.0f;
            if (RowSlider(L"Ampliação máxima", ms, std::to_wstring((int)std::round((1.2f + ms) * 100)) + L"%", mag)) {
                cfg::SetFloat(L"Dock", L"MagnifyScale", 1.2f + ms);
                Notify(RL_DOCK);
            }
            bool ah = cfg::Int(L"Dock", L"AutoHide", 0) != 0;
            if (RowToggle(L"Ocultar e mostrar o Dock automaticamente", L"Aparece quando o mouse encosta na borda de baixo", ah)) { cfg::SetInt(L"Dock", L"AutoHide", ah); Notify(RL_DOCK, true); }
            bool rec = cfg::Int(L"Dock", L"ShowRecent", 1) != 0;
            if (RowToggle(L"Mostrar apps abertos que não estão fixos", L"", rec)) { cfg::SetInt(L"Dock", L"ShowRecent", rec); Notify(RL_DOCK, true); }
            auto folders = cfg::List(L"Dock", L"Folders");
            bool dl = std::find(folders.begin(), folders.end(), L"shell:Downloads") != folders.end();
            if (RowToggle(L"Pasta Downloads no Dock", L"", dl)) {
                folders.erase(std::remove(folders.begin(), folders.end(), L"shell:Downloads"), folders.end());
                if (dl) folders.insert(folders.begin(), L"shell:Downloads");
                cfg::SetList(L"Dock", L"Folders", folders);
                Notify(RL_DOCK, true);
            }
        });
        Caption(y, L"MESA");
        Group(y, [&] {
            bool wg = cfg::Int(L"Widgets", L"Enabled", 1) != 0;
            if (RowToggle(L"Widgets na mesa", L"Data, calendário, relógio e clima", wg)) { cfg::SetInt(L"Widgets", L"Enabled", wg); Notify(RL_WIDGETS, true); }
            std::wstring city = cfg::Str(L"Weather", L"City");
            RowInfo(L"Cidade do clima", city.empty() ? L"Não definida" : city);
            float t;
            if (RowCustom(52 * s, &t)) {
                bool enter = false;
                TextField(7010, D2D1::RectF(L_ + 14 * s, t + 10 * s, R_ - 120 * s, t + 42 * s), cityQuery_, L"Buscar cidade (ex.: Campinas)", false, &enter);
                if ((Button(Id(), D2D1::RectF(R_ - 108 * s, t + 11 * s, R_ - 14 * s, t + 41 * s), searching_ ? L"Buscando…" : L"Buscar", true, !searching_) || enter)) SearchCity();
            }
            for (size_t i = 0; i < cities_.size(); ++i) {
                if (RowCustom(40 * s, &t)) {
                    D2D1_RECT_F r = D2D1::RectF(L_ + 4 * s, t + 2 * s, R_ - 4 * s, t + 38 * s);
                    if (Row(Id(), r)) {
                        auto& ct = cities_[i];
                        wchar_t la[32], lo[32];
                        swprintf_s(la, L"%.4f", ct.lat);
                        swprintf_s(lo, L"%.4f", ct.lon);
                        cfg::Set(L"Weather", L"City", ct.name);
                        cfg::Set(L"Weather", L"Lat", la);
                        cfg::Set(L"Weather", L"Lon", lo);
                        cities_.clear();
                        cityQuery_.clear();
                        cityMsg_ = L"Clima atualizado para " + ct.name + L".";
                        Notify(RL_WIDGETS, true);
                        break;
                    }
                    Label(cities_[i].name, 13, D2D1::RectF(L_ + 16 * s, t, R_ * 0.6f, t + 40 * s), th.text, DWRITE_FONT_WEIGHT_MEDIUM);
                    Label(cities_[i].region, 12, D2D1::RectF(L_ + 16 * s, t, R_ - 16 * s, t + 40 * s), th.sub, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_TRAILING);
                }
            }
        });
        if (!cityMsg_.empty()) Note(y, cityMsg_);
        Note(y, L"Para fixar ou tirar apps do Dock: arraste o ícone, ou clique com o botão direito > Manter no Dock.");
    }

    void PageBarra(float& y) {
        Group(y, [&] {
            bool tray = cfg::Int(L"MenuBar", L"ShowTray", 1) != 0;
            if (RowToggle(L"Ícones de apps em segundo plano", L"Os ícones da bandeja do Windows (antivírus, nuvem…)", tray)) { cfg::SetInt(L"MenuBar", L"ShowTray", tray); Notify(RL_MENUBAR, true); }
            bool pct = cfg::Int(L"MenuBar", L"BatteryPercent", 1) != 0;
            if (RowToggle(L"Mostrar porcentagem da bateria", L"", pct)) { cfg::SetInt(L"MenuBar", L"BatteryPercent", pct); Notify(RL_MENUBAR, true); }
            int clk = cfg::Int(L"MenuBar", L"ClockDate", 1) ? 0 : 1;
            if (RowSegment(L"Relógio", { L"Dia e hora", L"Só a hora" }, clk)) { cfg::SetInt(L"MenuBar", L"ClockDate", clk == 0); Notify(RL_MENUBAR, true); }
            int hh = cfg::Int(L"MenuBar", L"Height", 28);
            int sel = hh <= 25 ? 0 : hh <= 29 ? 1 : 2;
            if (RowSegment(L"Altura da barra", { L"Compacta", L"Padrão", L"Grande" }, sel)) {
                cfg::SetInt(L"MenuBar", L"Height", sel == 0 ? 24 : sel == 1 ? 28 : 32);
                Notify(RL_ALL, true);
            }
        });
        Note(y, L"Os menus Arquivo, Editar, Visualizar e Janela funcionam em qualquer app: eles enviam os atalhos de teclado para o app da frente.");
    }

    void PageJanelas(float& y) {
        Caption(y, L"BOTÕES DAS JANELAS");
        Group(y, [&] {
            bool tl = cfg::Int(L"Windows", L"TrafficLights", 1) != 0;
            if (RowToggle(L"Botões estilo Mac", L"Fechar, minimizar e ampliar em vermelho, amarelo e verde", tl)) { cfg::SetInt(L"Windows", L"TrafficLights", tl); Notify(RL_WINDOWS, true); }
            bool an = cfg::Int(L"Effects", L"OpenClose", 1) != 0;
            if (RowToggle(L"Animação ao abrir e fechar janelas", L"", an)) { cfg::SetInt(L"Effects", L"OpenClose", an); Notify(RL_WINDOWS, true); }
        });
        Caption(y, L"ALT+TAB");
        Group(y, [&] {
            bool sw = cfg::Int(L"Windows", L"Switcher", 1) != 0;
            if (RowToggle(L"Alt+Tab do WinMac", L"Painel de vidro para trocar de janela", sw)) { cfg::SetInt(L"Windows", L"Switcher", sw); Notify(RL_WINDOWS, true); }
            bool pv = cfg::Int(L"Windows", L"SwitcherPreviews", 1) != 0;
            if (RowToggle(L"Prévias ao vivo das janelas", L"Desligado: só os ícones, como no Mac", pv)) { cfg::SetInt(L"Windows", L"SwitcherPreviews", pv); Notify(RL_WINDOWS, true); }
        });
        Note(y, L"No Alt+Tab: Tab avança, Shift+Tab volta, setas também funcionam, Q ou W fecha a janela selecionada e Esc cancela.");
        Note(y, L"Os cantos das janelas dos outros apps ficam como o Windows 10 desenha — arredondá-los exigiria modificar arquivos do sistema. Tudo o que o WinMac desenha é arredondado.");
    }

    void PageArquivos(float& y) {
        Group(y, [&] {
            bool ext = RegDw(kAdv, L"HideFileExt", 1) == 0;
            if (RowToggle(L"Mostrar extensões dos arquivos", L"Ex.: foto.jpg em vez de foto", ext)) { SetRegDw(kAdv, L"HideFileExt", ext ? 0 : 1); RefreshExplorer(); }
            bool hid = RegDw(kAdv, L"Hidden", 2) == 1;
            if (RowToggle(L"Mostrar arquivos ocultos", L"", hid)) { SetRegDw(kAdv, L"Hidden", hid ? 1 : 2); RefreshExplorer(); }
            int lt = RegDw(kAdv, L"LaunchTo", 2) == 1 ? 0 : 1;
            if (RowSegment(L"Novas janelas abrem em", { L"Este Computador", L"Acesso rápido" }, lt)) SetRegDw(kAdv, L"LaunchTo", lt == 0 ? 1 : 2);
            bool rec = RegDw(kExp, L"ShowRecent", 1) != 0;
            if (RowToggle(L"Arquivos recentes no Acesso rápido", L"Desligue para mais privacidade", rec)) {
                SetRegDw(kExp, L"ShowRecent", rec);
                SetRegDw(kExp, L"ShowFrequent", rec);
                RefreshExplorer();
            }
            bool chk = RegDw(kAdv, L"AutoCheckSelect", 0) != 0;
            if (RowToggle(L"Caixas de seleção nos arquivos", L"", chk)) { SetRegDw(kAdv, L"AutoCheckSelect", chk); RefreshExplorer(); }
        });
        Group(y, [&] {
            bool files = cfg::Int(L"Dock", L"UseFiles", 1) != 0;
            if (RowToggle(L"Abrir pastas do Dock no app Files", L"Se o Files estiver instalado (abas e colunas, parecido com o Finder)", files)) cfg::SetInt(L"Dock", L"UseFiles", files);
            if (RowButton(L"Explorador de Arquivos", L"", L"Abrir")) ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
        });
        Note(y, L"Algumas mudanças aparecem depois de atualizar a janela do Explorador (F5).");
    }

    void PageApps(float& y) {
        Note(y, L"Remove do seu usuário os apps de assinatura, pagos e de propaganda que vêm com o Windows. Word, Excel, PowerPoint, Outlook e o resto do Office instalado continuam — e você pode reinstalar qualquer app pela Microsoft Store.");
        auto& all = install::PaidApps();
        if (paid_.empty()) {
            Group(y, [&] { RowInfo(L"Tudo limpo", L"Nenhum app pago ou de propaganda encontrado ✨"); });
        } else {
            Group(y, [&] {
                for (int i : paid_) {
                    if (RowButton(all[i].name, all[i].why, L"Remover", false, !removing_)) Remove({ all[i].family });
                }
            });
            if (Button(Id(), D2D1::RectF(R_ - 170 * s, y - 6 * s, R_, y + 22 * s), L"Remover todos", true, !removing_)) {
                std::vector<std::wstring> f;
                for (int i : paid_) f.push_back(all[i].family);
                Remove(f);
            }
            y += 36 * s;
        }
        if (removing_) {
            Spinner(L_ + 12 * s, y + 10 * s, 7 * s, th.sub);
            Label(appsMsg_, 12.5f, D2D1::RectF(L_ + 28 * s, y, R_, y + 20 * s), th.sub);
            y += 30 * s;
        } else if (!appsMsg_.empty()) Note(y, appsMsg_);
        Group(y, [&] {
            bool ads = RegDw(L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SubscribedContentEnabled", 1) == 0;
            bool v = ads;
            if (RowToggle(L"Bloquear sugestões e propagandas do Windows", L"Menu Iniciar, tela de bloqueio e dicas", v) && v) DisableAdsNow();
        });
    }
    void DisableAdsNow() { install::DisableAds(); }

    void PageSom(float& y) {
        Group(y, [&] {
            float v = muted_ ? 0 : vol_;
            if (RowSlider(L"Volume", v, std::to_wstring((int)std::round(v * 100)) + L"%")) {
                vol_ = v;
                muted_ = false;
                sys::SetVolume(v);
            }
            bool m = muted_;
            if (RowToggle(L"Sem som", L"", m)) { muted_ = m; sys::SetMute(m); }
            if (bright_ >= 0) {
                float b = bright_ / 100.f;
                if (RowSlider(L"Brilho da tela", b, std::to_wstring((int)std::round(b * 100)) + L"%")) {
                    bright_ = b * 100;
                    static DWORD last = 0;
                    if (GetTickCount() - last > 80 || !MouseDown()) { sys::SetBrightness((int)bright_); last = GetTickCount(); }
                }
            } else RowInfo(L"Brilho da tela", L"Ajuste no monitor");
        });
        Group(y, [&] {
            if (RowButton(L"Som do Windows", L"Saída, entrada e dispositivos", L"Abrir…")) OpenPath(L"ms-settings:sound");
            if (RowButton(L"Tela do Windows", L"Resolução, escala e luz noturna", L"Abrir…")) OpenPath(L"ms-settings:display");
        });
    }

    void PageSobre(float& y, D2D1_RECT_F view) {
        float cx = (L_ + R_) / 2;
        art::Pose p;
        p.t = GetTickCount() / 1000.f;
        p.wave = 0.6f + 0.4f * std::sin(p.t * 0.7f);
        p.face = art::Face::Wink;
        art::Pingo(c, cx, y + 92 * s, 150 * s, p);
        y += 190 * s;
        Label(L"WinMac", 26, D2D1::RectF(L_, y, R_, y + 38 * s), th.text, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        y += 38 * s;
        Label(L"Versão " WINMAC_VERSION L" · com o Pingo, a gotinha de vidro líquido", 13, D2D1::RectF(L_, y, R_, y + 22 * s), th.sub, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
        y += 34 * s;
        Paragraph(L"Um jeitinho de Mac no seu Windows 10 e 11: Dock, barra de menus, Alt+Tab, widgets e vidro líquido — leve, nativo e sem propaganda.", 13,
                  D2D1::RectF(L_ + 30 * s, y, R_ - 30 * s, y + 60 * s), th.text, DWRITE_TEXT_ALIGNMENT_CENTER);
        y += 60 * s;
        float bw = 170 * s;
        if (Button(Id(), D2D1::RectF(cx - bw - 6 * s, y, cx - 6 * s, y + 30 * s), L"Projeto no GitHub", true)) OpenPath(L"https://github.com/" WINMAC_REPO);
        if (Button(Id(), D2D1::RectF(cx + 6 * s, y, cx + bw + 6 * s, y + 30 * s), L"Licenças")) OpenPath(L"https://github.com/" WINMAC_REPO L"/blob/main/THIRD-PARTY.md");
        y += 50 * s;
        Label(L"Feito com 💙 por dhqdev · Código aberto (MIT)", 12, D2D1::RectF(L_, y, R_, y + 20 * s), th.faint, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
        y += 30 * s;
        (void)view;
    }

    void OnTimer(UINT_PTR id) override {
        if (id == T_REFRESH) {
            if (page_ == PWifi && wifi_.Refresh()) Invalidate();
        } else if (id == T_ANIMPAGE) {
            if (page_ == PSobre) Invalidate();
        } else if (id == 99) {
            KillTimer(Hwnd(), 99);
            Close();
        }
    }
    bool OnCustom(UINT m, WPARAM w, LPARAM) override {
        if (m == WM_GOTO) {
            if (w < PCount) Go((Page)w);
            if (IsIconic(Hwnd())) ShowWindow(Hwnd(), SW_RESTORE);
            SetForegroundWindow(Hwnd());
            return true;
        }
        return false;
    }
    bool OnKey(UINT vk) override {
        if (vk == VK_ESCAPE && FocusId() == 0) return false;
        if (FocusId() == 0 && (vk == VK_UP || vk == VK_DOWN)) {
            int p = page_ + (vk == VK_DOWN ? 1 : -1);
            if (p >= 0 && p < PCount) Go((Page)p);
            return true;
        }
        return false;
    }
    void OnClosed() override {
        *alive_ = false;
        if (quitOnClose) PostQuitMessage(0);
    }

public:
    void Start() { SetTimer(Hwnd(), T_REFRESH, 2500, nullptr); }
};

Page PageOf(const std::wstring& key) {
    for (int i = 0; i < PCount; ++i)
        if (_wcsicmp(kPages[i].key, key.c_str()) == 0) return (Page)i;
    return PGeral;
}
}  // namespace

int Run(const std::wstring& pageKey) {
    Page p = PageOf(pageKey);
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\WinMac.Ajustes");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        for (int i = 0; i < 30; ++i) {
            HWND w = FindWindowW(L"WinMac.UiWindow", L"Ajustes do Sistema");
            if (w) {
                DWORD pid = 0;
                GetWindowThreadProcessId(w, &pid);
                AllowSetForegroundWindow(pid);
                PostMessageW(w, WM_GOTO, (WPARAM)p, 0);
                break;
            }
            Sleep(100);
        }
        if (mtx) CloseHandle(mtx);
        return 0;
    }
    SetCurrentProcessExplicitAppUserModelID(L"WinMac.Ajustes");
    Ajustes a;
    if (!a.Create(L"Ajustes do Sistema", 880, 600, true)) return 1;
    a.page_ = p;
    a.focusPage();
    a.Start();
    a.Show();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
    return 0;
}

void RenderPreview(const std::wstring& pageKey, const std::wstring& png) {
    Ajustes a;
    a.quitOnClose = false;
    if (!a.Create(L"Ajustes (prévia)", 880, 600, false)) return;
    a.page_ = PageOf(pageKey);
    a.focusPage();
    a.SaveFrame(png);
}
}  // namespace settings
