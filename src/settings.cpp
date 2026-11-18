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
#include "sysx.h"
#include <shobjidl.h>

namespace settings {
namespace {
enum Page { PWifi, PBluetooth, PGeral, PUpdate, PAparencia, PPapel, PDock, PBarra, PJanelas, PNotif, PSom, PTela, PEnergia, PMouse,
            PArmaz, PArquivos, PApps, PSobre, PCount };
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
    { L"notificacoes", L"Notificações", L"\xE7E7", 255, 69, 58 },
    { L"som", L"Som", L"\xE767", 255, 55, 95 },
    { L"tela", L"Tela", L"\xE7F4", 10, 132, 255 },
    { L"energia", L"Bateria e Energia", L"\xE83F", 48, 190, 90 },
    { L"mouse", L"Mouse e Teclado", L"\xE962", 120, 120, 128 },
    { L"armazenamento", L"Armazenamento", L"\xEDA2", 120, 120, 128 },
    { L"arquivos", L"Arquivos (Explorador)", L"\xE8B7", 52, 150, 230 },
    { L"apps", L"Apps e Limpeza", L"\xE74D", 255, 69, 58 },
    { L"sobre", L"Sobre o WinMac", L"\xE946", 142, 142, 147 },
};
const int kGroups[] = { PBluetooth, PUpdate, PJanelas, PMouse, PApps };   // espaço depois destes
const UINT WM_GOTO = WM_APP + 40;
enum { T_REFRESH = 10, T_ANIMPAGE = 11 };

struct City { std::wstring name, region; double lat, lon; };

DWORD RegDw(const wchar_t* key, const wchar_t* name, DWORD def) {
    DWORD v = def, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS) return def;
    return v;
}
void SetRegDw(const wchar_t* key, const wchar_t* name, DWORD v) { sysx::SetDword(HKEY_CURRENT_USER, key, name, v); }
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
        if (p != page_) pageStart_ = GetTickCount();
        page_ = p;
        focusPage();
        Invalidate();
    }

    void focusPage() {
        Focus(0);
        if (page_ == PUpdate && !checking_ && !upd_.ok && upd_.error.empty()) StartCheck();
        if (page_ == PApps) ScanApps();
        if (page_ == PSom || page_ == PTela) {
            vol_ = sys::GetVolume(&muted_);
            bright_ = (float)sys::GetBrightness();
            outs_ = sysx::OutputDevices();
        }
        if (page_ == PBluetooth || page_ == PWifi) ReadRadio();
        if (page_ == PEnergia) power_ = sysx::GetPower();
        if (page_ == PArmaz) {
            drives_ = sysx::Drives();
            winmacSize_ = sysx::FolderSize(install::Dir());
            macdeskSize_ = MacDeskLeftover() ? sysx::FolderSize(MacDeskDir()) : 0;
        }
        UpdateAnimTimer();
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
    DWORD pageStart_ = 0;
    float sideScroll_ = 0, sideH_ = 0;
    int bt_ = -2;   // -2 lendo, -1 sem Bluetooth, 0 desligado, 1 ligado
    bool btBusy_ = false;
    sysx::Power power_;
    std::vector<sysx::AudioDev> outs_;
    std::vector<sysx::Drive> drives_;
    uint64_t winmacSize_ = 0, macdeskSize_ = 0;
    bool cleaning_ = false;
    std::wstring storageMsg_;

    static std::wstring MacDeskDir() {
        wchar_t b[MAX_PATH];
        ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\MacDesk", b, MAX_PATH);
        return b;
    }
    // sobras do MacDesk antigo: só apaga se nada do Windows ainda aponta para lá (cursores, papel de parede)
    static bool MacDeskLeftover() {
        std::wstring d = MacDeskDir();
        if (GetFileAttributesW(d.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
        wchar_t wp[MAX_PATH] = {};
        SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, wp, 0);
        if (StrStrIW(wp, L"\\MacDesk\\")) return false;
        wchar_t cur[MAX_PATH] = {};
        DWORD sz = sizeof(cur);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", L"Arrow", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, cur, &sz) == ERROR_SUCCESS &&
            StrStrIW(cur, L"\\MacDesk\\"))
            return false;
        return !FindWindowW(L"MacDesk.Ctrl", nullptr);
    }
    void ReadRadio() {
        auto r = std::make_shared<int>(-1);
        Async([r]() { *r = sysx::RadioState(sysx::Radio::Bluetooth); }, [this, r]() { bt_ = *r; Invalidate(); });
    }
    void UpdateAnimTimer() {
        if (page_ == PSobre && Active()) SetTimer(Hwnd(), T_ANIMPAGE, 33, nullptr);
        else KillTimer(Hwnd(), T_ANIMPAGE);
    }
    void OnActiveChanged(bool) override { UpdateAnimTimer(); }
    bool Danger(int id, D2D1_RECT_F r, const std::wstring& t) {
        auto keep = th.accent;
        th.accent = gfx::Rgba(255, 59, 48);
        bool f = Button(id, r, t, true);
        th.accent = keep;
        return f;
    }
    bool wallBusy_ = false;
    std::wstring wallMsg_;
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
        D2D1_RECT_F view = D2D1::RectF(0, 46 * s, sideW_, (float)H - 4 * s);
        BeginScroll(view, sideScroll_, sideH_);
        float y = view.top + 4 * s;
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
        y += 64 * s;
        bool upAvail = !cfg::Str(L"Update", L"Available").empty() || upd_.newer;
        float rowH = 29 * s;
        // posição de cada item (para o destaque deslizar até o item escolhido)
        std::vector<float> ys(PCount);
        float yy = y;
        for (int i = 0; i < PCount; ++i) {
            ys[i] = yy;
            yy += rowH + 1 * s;
            for (int g : kGroups)
                if (g == i) yy += 9 * s;
        }
        float hy = Anim(50, ys[page_], 0.16f);
        gfx::Fill(c, gfx::RR(10 * s, hy, sideW_ - 10 * s, hy + rowH, 7 * s), Active() ? th.accent : (th.dark ? gfx::White(0.12f) : gfx::Black(0.08f)));
        for (int i = 0; i < PCount; ++i) {
            D2D1_RECT_F r = D2D1::RectF(10 * s, ys[i], sideW_ - 10 * s, ys[i] + rowH);
            bool sel = i == page_;
            if (!sel && Row(100 + i, r, 7)) Go((Page)i);
            if (sel) Hover(100 + i, r);
            auto& pi = kPages[i];
            art::SectionIcon(c, D2D1::RectF(r.left + 8 * s, r.top + 4 * s, r.left + 29 * s, r.top + 25 * s), gfx::Rgba(pi.r, pi.g, pi.b), pi.glyph);
            bool onHl = std::fabs(hy - ys[i]) < rowH * 0.5f;
            Label(pi.title, 13, D2D1::RectF(r.left + 37 * s, r.top, r.right - 22 * s, r.bottom), onHl && Active() ? gfx::White(1) : th.text);
            if (i == PUpdate && upAvail) gfx::Circle(c, r.right - 14 * s, (r.top + r.bottom) / 2, 4.5f * s, gfx::Rgba(255, 69, 58));
        }
        float h = yy - view.top + 8 * s;
        if (std::fabs(h - sideH_) > 1) { sideH_ = h; KeepAnimating(); }
        EndScroll();
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
        // troca de página: o conteúdo entra deslizando de leve e aparecendo (estilo macOS)
        float pt = pageStart_ ? Clamp01((GetTickCount() - pageStart_) / 240.f) : 1.f;
        float pe = 1.f - std::pow(1.f - pt, 3.f);
        if (pt < 1.f) KeepAnimating();
        c->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), std::max(0.02f, pe)), nullptr);
        D2D1_MATRIX_3X2_F pold;
        c->GetTransform(&pold);
        c->SetTransform(D2D1::Matrix3x2F::Translation((1.f - pe) * 18 * s, 0) * pold);
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
        case PTela: PageTela(y); break;
        case PNotif: PageNotif(y); break;
        case PEnergia: PageEnergia(y); break;
        case PMouse: PageMouse(y); break;
        case PArmaz: PageArmaz(y); break;
        case PSobre: PageSobre(y, view); break;
        default: break;
        }
        float h = y - view.top + 10 * s;
        if (std::fabs(h - contentH_[page_]) > 1) { contentH_[page_] = h; KeepAnimating(); }
        EndScroll();
        c->SetTransform(pold);
        c->PopLayer();
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
            if (bt_ == -2) { RowInfo(L"Bluetooth", L"Verificando…"); return; }
            if (bt_ == -1) { RowInfo(L"Bluetooth", L"Este computador não tem Bluetooth"); return; }
            bool on = bt_ == 1;
            if (RowToggle(L"Bluetooth", btBusy_ ? L"Mudando…" : (on ? L"Ligado — visível para fones e mouses" : L"Desligado"), on) && !btBusy_) {
                btBusy_ = true;
                bt_ = on ? 1 : 0;
                auto ok = std::make_shared<bool>(false);
                Async([on, ok]() { *ok = sysx::SetRadioState(sysx::Radio::Bluetooth, on); }, [this]() { btBusy_ = false; ReadRadio(); });
            }
        });
        Group(y, [&] {
            if (RowButton(L"Parear um dispositivo novo", L"Fones, caixas de som, mouses e teclados", L"Parear…")) OpenPath(L"ms-settings:bluetooth");
        });
        Note(y, L"O pareamento usa a janela do próprio Windows, que é a única que tem permissão para isso.");
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
        {
            float h = 84 * s;
            auto rr = gfx::RR(L_, y, R_, y + h, 10 * s);
            gfx::Fill(c, rr, th.group);
            gfx::Stroke(c, rr, th.groupStroke, 1.f);
            Label(L"Desinstalar o WinMac", 13.5f, D2D1::RectF(L_ + 14 * s, y + 12 * s, R_ - 190 * s, y + 34 * s), th.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            Paragraph(L"Volta tudo como era antes: barra de tarefas, cursores, tema, papel de parede e ajustes do Windows.", 11.5f,
                      D2D1::RectF(L_ + 14 * s, y + 36 * s, R_ - 190 * s, y + 78 * s), th.sub);
            if (Danger(Id(), D2D1::RectF(R_ - 176 * s, y + h / 2 - 15 * s, R_ - 14 * s, y + h / 2 + 15 * s), L"Desinstalar…"))
                ShellExecuteW(nullptr, L"open", install::IsInstalled() ? install::InstalledExe().c_str() : SelfPath().c_str(), L"--uninstall", nullptr, SW_SHOWNORMAL);
            y += h + 18 * s;
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
        // uma miniatura por quadro: a página abre na hora e as imagens vão aparecendo
        if (thumbs_.size() < ws.size()) {
            thumbs_.push_back(art::RenderWallpaper((int)thumbs_.size(), 288, 162));
            KeepAnimating();
        }
        int per = 3;
        float gap = 16 * s, tw = (R_ - L_ - (per - 1) * gap) / per, thh = tw * 9 / 16;
        for (int i = 0; i < (int)ws.size(); ++i) {
            float x = L_ + (i % per) * (tw + gap), yy = y + (i / per) * (thh + 34 * s);
            D2D1_RECT_F r = D2D1::RectF(x, yy, x + tw, yy + thh);
            bool hov = Hover(1700 + i, r);
            if (Clicked(1700 + i, r) && !wallBusy_) {
                wallBusy_ = true;
                wallMsg_.clear();
                cfg::SetInt(L"Screen", L"Enforce", 1);
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
            bool spot = cur == -2;
            if (RowButton(L"Destaque do Windows", spot ? L"Ativo: uma foto nova da Microsoft todo dia" : L"Fotos novas da Microsoft todo dia", spot ? L"Ativo" : L"Usar", !spot, !spot)) {
                cfg::SetInt(L"Screen", L"Enforce", 0);
                cfg::SetInt(L"Screen", L"WallpaperPreset", -2);
                sysx::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\DesktopSpotlight\\Settings", L"EnabledState", 1);
                sysx::SetDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Wallpapers", L"BackgroundType", 3);
                DWORD_PTR r;
                SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETDESKWALLPAPER, 0, SMTO_ABORTIFHUNG, 300, &r);
                wallMsg_ = L"Destaque do Windows ligado. Se a foto não trocar em alguns segundos, escolha \"Destaque do Windows\" na lista \"Tela de fundo\" do Windows.";
                NotifyShell(RL_WALLPAPER);
            }
            if (RowButton(L"Usar uma foto sua", L"JPG, PNG ou BMP", L"Escolher…")) PickWallpaperFile();
            std::wstring prev = cfg::Str(L"Backup", L"Wallpaper");
            if (!prev.empty() && GetFileAttributesW(prev.c_str()) != INVALID_FILE_ATTRIBUTES &&
                RowButton(L"Papel de parede que eu tinha antes", PathFindFileNameW(prev.c_str()), L"Voltar")) {
                install::ApplyWallpaperFile(prev);
                cfg::SetInt(L"Screen", L"WallpaperPreset", -1);
                NotifyShell(RL_WALLPAPER);
            }
            bool keep = cfg::Int(L"Screen", L"Enforce", 1) != 0;
            if (RowToggle(L"Impedir que o Windows troque o papel de parede", L"Desliga o \"Windows Spotlight\" se ele voltar sozinho", keep)) cfg::SetInt(L"Screen", L"Enforce", keep);
        });
        if (!wallMsg_.empty()) Note(y, wallMsg_);
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
            ANIMATIONINFO ai{ sizeof(ai) };
            SystemParametersInfoW(SPI_GETANIMATION, sizeof(ai), &ai, 0);
            bool an = ai.iMinAnimate != 0;
            if (RowToggle(L"Animações ao minimizar e maximizar", L"As animações suaves do próprio Windows", an)) {
                ai.iMinAnimate = an;
                SystemParametersInfoW(SPI_SETANIMATION, sizeof(ai), &ai, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            }
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
        Caption(y, L"SAÍDA DE SOM");
        Group(y, [&] {
            if (outs_.empty()) { RowInfo(L"Saída", L"Nenhum dispositivo de som"); return; }
            for (auto& d : outs_) {
                float t;
                if (!RowCustom(40 * s, &t)) continue;
                if (Row(Id(), D2D1::RectF(L_ + 4 * s, t + 2 * s, R_ - 4 * s, t + 38 * s)) && !d.isDefault) {
                    sysx::SetDefaultOutput(d.id);
                    outs_ = sysx::OutputDevices();
                    vol_ = sys::GetVolume(&muted_);
                }
                gfx::Glyph(c, L"\xE7F5", 14 * s, D2D1::RectF(L_ + 12 * s, t, L_ + 36 * s, t + 40 * s), th.sub);
                Label(d.name, 13, D2D1::RectF(L_ + 42 * s, t, R_ - 40 * s, t + 40 * s), th.text, d.isDefault ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_REGULAR);
                if (d.isDefault) gfx::Glyph(c, L"\xE73E", 13 * s, D2D1::RectF(R_ - 40 * s, t, R_ - 14 * s, t + 40 * s), th.accent);
            }
        });
        Group(y, [&] {
            float v = muted_ ? 0 : vol_;
            if (RowSlider(L"Volume", v, std::to_wstring((int)std::round(v * 100)) + L"%")) {
                vol_ = v;
                muted_ = false;
                sys::SetVolume(v);
            }
            bool m = muted_;
            if (RowToggle(L"Sem som", L"", m)) { muted_ = m; sys::SetMute(m); }
        });
        if (LinkButton(Id(), D2D1::RectF(L_ + 4 * s, y - 6 * s, R_, y + 18 * s), L"Mais opções de som do Windows…")) OpenPath(L"ms-settings:sound");
        y += 26 * s;
    }

    void PageTela(float& y) {
        Group(y, [&] {
            if (bright_ >= 0) {
                float b = bright_ / 100.f;
                if (RowSlider(L"Brilho", b, std::to_wstring((int)std::round(b * 100)) + L"%")) {
                    bright_ = b * 100;
                    static DWORD last = 0;
                    if (GetTickCount() - last > 80 || !MouseDown()) { sys::SetBrightness((int)bright_); last = GetTickCount(); }
                }
            } else RowInfo(L"Brilho", L"Ajuste nos botões do monitor");
            RowInfo(L"Resolução", sysx::Resolution());
        });
        Group(y, [&] {
            int theme = cfg::Int(L"Appearance", L"Theme", gfx::DarkMode() ? 1 : 0);
            if (RowSegment(L"Aparência", { L"Claro", L"Escuro", L"Automático" }, theme)) { install::ApplyTheme(theme); Notify(RL_THEME, true); }
        });
        if (LinkButton(Id(), D2D1::RectF(L_ + 4 * s, y - 6 * s, R_, y + 18 * s), L"Resolução, escala e luz noturna do Windows…")) OpenPath(L"ms-settings:display");
        y += 26 * s;
    }

    void PageNotif(float& y) {
        Group(y, [&] {
            bool on = sysx::ToastsEnabled();
            if (RowToggle(L"Notificações dos apps", on ? L"Ligadas" : L"Não Perturbe: nenhum app mostra avisos", on)) sysx::SetToasts(on);
            const wchar_t* cdm = L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager";
            bool tips = sysx::GetDword(HKEY_CURRENT_USER, cdm, L"SubscribedContent-338389Enabled", 1) != 0;
            if (RowToggle(L"Dicas e sugestões do Windows", L"Desligue para menos avisos e propaganda", tips)) {
                sysx::SetDword(HKEY_CURRENT_USER, cdm, L"SubscribedContent-338389Enabled", tips);
                sysx::SetDword(HKEY_CURRENT_USER, cdm, L"SoftLandingEnabled", tips);
                sysx::SetDword(HKEY_CURRENT_USER, cdm, L"SystemPaneSuggestionsEnabled", tips);
            }
            bool upd = cfg::Int(L"Update", L"Auto", 1) != 0;
            if (RowToggle(L"Avisos de atualização do WinMac", L"", upd)) cfg::SetInt(L"Update", L"Auto", upd);
        });
        Note(y, L"\"Não Perturbe\" também fica na Central de Controle, no canto direito da barra de menus.");
    }

    void PageEnergia(float& y) {
        auto& pw = power_;
        float h = 92 * s;
        auto rr = gfx::RR(L_, y, R_, y + h, 12 * s);
        gfx::Fill(c, rr, th.group);
        gfx::Stroke(c, rr, th.groupStroke, 1.f);
        if (pw.hasBattery) {
            Label(std::to_wstring(pw.pct) + L"%", 30, D2D1::RectF(L_ + 18 * s, y + 8 * s, L_ + 160 * s, y + 52 * s), th.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            std::wstring st = pw.charging ? L"Carregando" : pw.plugged ? L"Na tomada" : L"Na bateria";
            if (!pw.plugged && pw.secondsLeft > 0) st += L" · " + std::to_wstring(pw.secondsLeft / 3600) + L" h " + std::to_wstring((pw.secondsLeft / 60) % 60) + L" min restantes";
            if (pw.saver) st += L" · Economia ligada";
            Label(st, 12.5f, D2D1::RectF(L_ + 160 * s, y + 18 * s, R_ - 18 * s, y + 42 * s), th.sub);
            D2D1_RECT_F bar = D2D1::RectF(L_ + 18 * s, y + 60 * s, R_ - 18 * s, y + 70 * s);
            gfx::Fill(c, gfx::RR(bar.left, bar.top, bar.right, bar.bottom, 5 * s), th.dark ? gfx::White(0.12f) : gfx::Black(0.08f));
            float fx = bar.left + (bar.right - bar.left) * pw.pct / 100.f;
            gfx::Fill(c, gfx::RR(bar.left, bar.top, std::max(fx, bar.left + 10 * s), bar.bottom, 5 * s), pw.pct <= 20 && !pw.charging ? gfx::Rgba(255, 69, 58) : gfx::Rgba(52, 199, 89));
        } else {
            Label(L"Na tomada", 20, D2D1::RectF(L_ + 18 * s, y + 14 * s, R_, y + 46 * s), th.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            Label(L"Este computador não usa bateria.", 12.5f, D2D1::RectF(L_ + 18 * s, y + 48 * s, R_, y + 72 * s), th.sub);
        }
        y += h + 18 * s;
        Group(y, [&] {
            int mode = sysx::PowerMode();
            if (RowSegment(L"Modo de energia", { L"Economia", L"Equilibrado", L"Desempenho" }, mode)) sysx::SetPowerMode(mode);
            if (pw.hasBattery) {
                bool sv = sysx::SaverThreshold() >= 100;
                if (RowToggle(L"Economia de bateria sempre que estiver na bateria", L"Menos brilho e atividade em segundo plano", sv)) {
                    if (sv) { cfg::SetInt(L"Power", L"SaverPrev", std::max(5, sysx::SaverThreshold())); sysx::SetSaverThreshold(100); }
                    else sysx::SetSaverThreshold(cfg::Int(L"Power", L"SaverPrev", 20) >= 100 ? 20 : cfg::Int(L"Power", L"SaverPrev", 20));
                    power_ = sysx::GetPower();
                }
                int thr = sysx::SaverThreshold();
                int sel = thr <= 10 ? 0 : thr <= 20 ? 1 : thr <= 30 ? 2 : 3;
                if (thr < 100 && RowSegment(L"Ligar a economia sozinha em", { L"10%", L"20%", L"30%", L"50%" }, sel)) {
                    const int v[] = { 10, 20, 30, 50 };
                    sysx::SetSaverThreshold(v[sel]);
                }
            }
        });
        static const int offs[] = { 2, 5, 10, 15, 0 }, sleeps[] = { 5, 15, 30, 60, 0 };
        auto idx = [](const int* arr, int v) { for (int i = 0; i < 5; ++i) if (arr[i] == v) return i; return v == 0 ? 4 : 1; };
        Caption(y, pw.hasBattery ? L"NA BATERIA" : L"TELA E SUSPENSÃO");
        auto block = [&](bool ac) {
            Group(y, [&] {
                int a = idx(offs, sysx::ScreenOffMinutes(ac));
                if (RowSegment(L"Desligar a tela depois de", { L"2 min", L"5 min", L"10 min", L"15 min", L"Nunca" }, a)) sysx::SetScreenOffMinutes(ac, offs[a]);
                int b = idx(sleeps, sysx::SleepMinutes(ac));
                if (RowSegment(L"Suspender depois de", { L"5 min", L"15 min", L"30 min", L"1 h", L"Nunca" }, b)) sysx::SetSleepMinutes(ac, sleeps[b]);
            });
        };
        if (pw.hasBattery) {
            block(false);
            Caption(y, L"NA TOMADA");
        }
        block(true);
    }

    void PageMouse(float& y) {
        Caption(y, L"MOUSE E TRACKPAD");
        Group(y, [&] {
            float sp = (sysx::MouseSpeed() - 1) / 19.f;
            if (RowSlider(L"Velocidade do ponteiro", sp, std::to_wstring(1 + (int)std::round(sp * 19)))) sysx::SetMouseSpeed(1 + (int)std::round(sp * 19));
            int wl = sysx::WheelLines();
            int sel = wl <= 1 ? 0 : wl <= 3 ? 1 : wl <= 5 ? 2 : 3;
            if (RowSegment(L"Linhas por giro da rodinha", { L"1", L"3", L"5", L"8" }, sel)) {
                const int v[] = { 1, 3, 5, 8 };
                sysx::SetWheelLines(v[sel]);
            }
            int dc = sysx::DoubleClickMs();
            int ds = dc <= 380 ? 0 : dc <= 600 ? 1 : 2;
            if (RowSegment(L"Velocidade do clique duplo", { L"Rápido", L"Normal", L"Devagar" }, ds)) {
                const int v[] = { 320, 500, 700 };
                sysx::SetDoubleClickMs(v[ds]);
            }
            bool sw = sysx::ButtonsSwapped();
            if (RowToggle(L"Modo canhoto", L"Troca os botões esquerdo e direito", sw)) sysx::SetButtonsSwapped(sw);
        });
        Caption(y, L"ATALHOS DO WINMAC");
        Group(y, [&] {
            RowInfo(L"Trocar de janela", L"Alt + Tab");
            RowInfo(L"Fechar janela no Alt+Tab", L"Alt + Tab, depois Q");
            RowInfo(L"Abrir a busca de apps", L"Lupa na barra de menus");
            RowInfo(L"Emoji e símbolos", L"Win + .");
        });
    }

    void PageArmaz(float& y) {
        for (auto& d : drives_) {
            float h = 78 * s;
            auto rr = gfx::RR(L_, y, R_, y + h, 12 * s);
            gfx::Fill(c, rr, th.group);
            gfx::Stroke(c, rr, th.groupStroke, 1.f);
            Label(d.label + L" (" + d.root.substr(0, 2) + L")", 13.5f, D2D1::RectF(L_ + 16 * s, y + 10 * s, R_ - 16 * s, y + 32 * s), th.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            Label(sysx::Bytes(d.free) + L" livres de " + sysx::Bytes(d.total), 12, D2D1::RectF(L_ + 16 * s, y + 10 * s, R_ - 16 * s, y + 32 * s), th.sub,
                  DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_TRAILING);
            D2D1_RECT_F bar = D2D1::RectF(L_ + 16 * s, y + 44 * s, R_ - 16 * s, y + 56 * s);
            gfx::Fill(c, gfx::RR(bar.left, bar.top, bar.right, bar.bottom, 6 * s), th.dark ? gfx::White(0.12f) : gfx::Black(0.08f));
            float used = d.total ? (float)((double)(d.total - d.free) / d.total) : 0;
            gfx::Fill(c, gfx::RR(bar.left, bar.top, bar.left + (bar.right - bar.left) * used, bar.bottom, 6 * s), used > 0.9f ? gfx::Rgba(255, 69, 58) : th.accent);
            y += h + 12 * s;
        }
        y += 6 * s;
        Group(y, [&] {
            RowInfo(L"Espaço usado pelo WinMac", sysx::Bytes(winmacSize_));
            if (RowButton(L"Limpar arquivos temporários", L"Apaga o que os apps deixaram na pasta temporária há mais de um dia", cleaning_ ? L"Limpando…" : L"Limpar", false, !cleaning_)) {
                cleaning_ = true;
                auto freed = std::make_shared<uint64_t>(0);
                Async([freed]() { *freed = sysx::CleanTemp(); }, [this, freed]() {
                    cleaning_ = false;
                    storageMsg_ = L"Pronto! " + sysx::Bytes(*freed) + L" liberados.";
                    drives_ = sysx::Drives();
                    Invalidate();
                });
            }
            if (RowButton(L"Esvaziar a Lixeira", L"", L"Esvaziar…")) {
                SHEmptyRecycleBinW(Hwnd(), nullptr, 0);
                drives_ = sysx::Drives();
            }
            if (macdeskSize_ && RowButton(L"Apagar sobras do MacDesk antigo", sysx::Bytes(macdeskSize_) + L" que não são mais usados", L"Apagar")) {
                std::wstring d = MacDeskDir() + std::wstring(1, L'\0') + std::wstring(1, L'\0');
                SHFILEOPSTRUCTW op{};
                op.wFunc = FO_DELETE;
                op.pFrom = d.c_str();
                op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOF_ALLOWUNDO;
                SHFileOperationW(&op);
                macdeskSize_ = 0;
                storageMsg_ = L"Sobras do MacDesk enviadas para a Lixeira.";
            }
        });
        if (!storageMsg_.empty()) Note(y, storageMsg_);
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
        if (Danger(Id(), D2D1::RectF(cx - 90 * s, y - 8 * s, cx + 90 * s, y + 22 * s), L"Desinstalar o WinMac…"))
            ShellExecuteW(nullptr, L"open", install::IsInstalled() ? install::InstalledExe().c_str() : SelfPath().c_str(), L"--uninstall", nullptr, SW_SHOWNORMAL);
        y += 42 * s;
        Label(L"Feito com 💙 por dhqdev · Código aberto (MIT)", 12, D2D1::RectF(L_, y, R_, y + 20 * s), th.faint, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
        y += 30 * s;
        (void)view;
    }

    void OnTimer(UINT_PTR id) override {
        if (id == T_REFRESH) {
            if (page_ == PWifi && wifi_.Refresh()) Invalidate();
            if (page_ == PEnergia) { power_ = sysx::GetPower(); Invalidate(); }
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
    if (!a.Create(L"Ajustes do Sistema", 900, 640, true)) return 1;
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
    if (!a.Create(L"Ajustes (prévia)", 900, 640, false)) return;
    a.page_ = PageOf(pageKey);
    a.focusPage();
    a.SaveFrame(png);
}
}  // namespace settings
