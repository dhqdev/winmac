#include "welcome.h"
#include "ui.h"
#include "wifiui.h"
#include "art.h"
#include "config.h"
#include "install.h"
#include "update.h"
#include "version.h"

namespace welcome {
namespace {
enum { PHello, PLook, PWifi, PChoices, PInstall, PDone, PCount };
enum { T_ANIM = 20, T_WIFI = 21 };

class Welcome : public ui::Window {
public:
    bool installMode_ = true;
    int page_ = PHello;
    bool quitOnClose = true;
    float tAnim_ = 0;   // para prévias estáticas

    void Init() {
        th = ui::Current();
        opt_.theme = cfg::Int(L"Appearance", L"Theme", gfx::DarkMode() ? 1 : 0);
        opt_.accent = cfg::Int(L"Appearance", L"Accent", 0);
        opt_.wallpaper = std::max(0, cfg::Int(L"Screen", L"WallpaperPreset", 0));
        for (int i = 0; i < (int)art::Wallpapers().size(); ++i) thumbs_.push_back(art::RenderWallpaper(i, 192, 108));
        if (installMode_ && install::IsInstalled()) {
            std::wstring v = cfg::Str(L"State", L"InstalledVersion");
            upgrade_ = !v.empty();
            installedVer_ = v;
        }
        start_ = GetTickCount();
    }
    void Start() { SetTimer(Hwnd(), T_ANIM, 40, nullptr); SetTimer(Hwnd(), T_WIFI, 2500, nullptr); }

protected:
    install::Options opt_;
    std::vector<ComPtr<IWICBitmap>> thumbs_;
    std::unique_ptr<WifiPanel> wifi_;
    float wifiH_ = 200;
    int step_ = 0, steps_ = 7;
    std::wstring stepText_, error_;
    bool busy_ = false, upgrade_ = false, updatedOnly_ = false;
    std::wstring installedVer_;
    DWORD start_ = 0, pageStart_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    float T() const { return tAnim_ > 0 ? tAnim_ : (GetTickCount() - start_) / 1000.f; }

    void Go(int p) {
        page_ = p;
        pageStart_ = GetTickCount();
        Focus(0);
        if (p == PWifi && !wifi_) wifi_ = std::make_unique<WifiPanel>();
        Invalidate();
    }

    void Title(float y, const std::wstring& t, const std::wstring& sub) {
        Label(t, 24, D2D1::RectF(40 * s, y, W - 40 * s, y + 36 * s), th.text, DWRITE_FONT_WEIGHT_BOLD);
        Paragraph(sub, 13.5f, D2D1::RectF(40 * s, y + 40 * s, W - 40 * s, y + 90 * s), th.sub);
    }

    void Dots() {
        int n = 5;
        float cx = W / 2.f, y = H - 34 * s;
        for (int i = 0; i < n; ++i) {
            float x = cx + (i - (n - 1) / 2.f) * 16 * s;
            bool cur = i == std::min(page_, 4);
            gfx::Fill(c, gfx::RR(x - (cur ? 9 : 3.5f) * s, y - 3.5f * s, x + (cur ? 9 : 3.5f) * s, y + 3.5f * s, 3.5f * s), cur ? th.accent : th.faint);
        }
    }

    bool Next(const std::wstring& text, bool enabled = true) {
        return Button(800, D2D1::RectF(W - 40 * s - 150 * s, H - 52 * s, W - 40 * s, H - 18 * s), text, true, enabled);
    }
    bool Back() { return Button(801, D2D1::RectF(40 * s, H - 52 * s, 40 * s + 110 * s, H - 18 * s), L"Voltar"); }

    void DoInstall() {
        busy_ = true;
        error_.clear();
        Go(PInstall);
        auto o = opt_;
        HWND h = Hwnd();
        auto alive = alive_;
        bool inst = installMode_;
        std::thread([this, o, h, alive, inst]() {
            std::wstring err;
            bool ok;
            auto prog = [this, h, alive](int i, int n, const std::wstring& t) {
                RunAsync(h, [this, alive, i, n, t]() { if (*alive) { step_ = i; steps_ = n; stepText_ = t; Invalidate(); } });
            };
            if (inst) ok = install::Install(o, prog, &err);
            else ok = ApplyOnly(o, prog);
            RunAsync(h, [this, alive, ok, err]() {
                if (!*alive) return;
                busy_ = false;
                if (ok) Go(PDone);
                else { error_ = err; Invalidate(); }
            });
        }).detach();
    }

    // Já instalado (boas-vindas pelo Ajustes): só aplica as escolhas.
    static bool ApplyOnly(const install::Options& o, const std::function<void(int, int, const std::wstring&)>& prog) {
        prog(1, 4, L"Aplicando aparência…");
        cfg::SetInt(L"Appearance", L"Accent", o.accent);
        install::ApplyTheme(o.theme);
        if (o.useWallpaper) install::ApplyWallpaper(o.wallpaper);
        prog(2, 4, L"Ajustando janelas e Dock…");
        cfg::SetInt(L"Windows", L"TrafficLights", o.traffic);
        cfg::SetInt(L"Windows", L"Switcher", o.switcher);
        cfg::SetInt(L"Dock", L"AutoHide", o.autohide);
        cfg::SetInt(L"Widgets", L"Enabled", o.widgets);
        install::ApplyCursors(o.cursors);
        prog(3, 4, o.debloat ? L"Removendo apps pagos e propagandas…" : L"Quase lá…");
        if (o.debloat) {
            install::DisableAds();
            std::vector<std::wstring> rm;
            for (auto& a : install::PaidApps())
                if (wcscmp(a.name, L"Spotify") != 0 && install::IsPackageInstalled(a.family)) rm.push_back(a.family);
            install::RemovePackages(rm);
        }
        NotifyShell(RL_ALL);
        prog(4, 4, L"Pronto!");
        return true;
    }

    void DoUpgrade() {
        busy_ = true;
        updatedOnly_ = true;
        Go(PInstall);
        stepText_ = L"Atualizando o WinMac…";
        HWND h = Hwnd();
        auto alive = alive_;
        std::thread([this, h, alive]() {
            int r = install::ApplyUpdate(install::Dir());
            RunAsync(h, [this, alive, r]() {
                if (!*alive) return;
                busy_ = false;
                if (r == 0) Go(PDone);
                else error_ = L"Não foi possível atualizar.";
                Invalidate();
            });
        }).detach();
    }

    void PageHello() {
        float t = T();
        art::Pose p;
        p.t = t;
        p.wave = std::max(0.f, std::sin(t * 1.3f)) * 0.9f + 0.1f;
        p.face = art::Face::Happy;
        // auréola de cor atrás do Pingo
        float px = W * 0.30f, py = H * 0.46f;
        auto halo = gfx::Radial(c, D2D1::Point2F(px, py), 170 * s, 170 * s, { { 0.f, gfx::Rgba(120, 190, 255, 0.35f) }, { 1.f, gfx::Rgba(120, 190, 255, 0) } });
        c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, py), 170 * s, 170 * s), halo.Get());
        art::Pingo(c, px, py, 200 * s, p);
        float x = W * 0.56f;
        Label(L"Olá! Eu sou o Pingo.", 28, D2D1::RectF(x, H * 0.24f, W - 30 * s, H * 0.24f + 44 * s), th.text, DWRITE_FONT_WEIGHT_BOLD);
        Label(L"Bem-vindo ao WinMac 💧", 17, D2D1::RectF(x, H * 0.24f + 46 * s, W - 30 * s, H * 0.24f + 74 * s), th.accent, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        Paragraph(L"Vou deixar o seu Windows com cara de Mac: Dock, barra de menus, Alt+Tab de vidro, widgets e botões coloridos nas janelas. Leva menos de um minuto e dá para desfazer quando quiser.",
                  13.5f, D2D1::RectF(x, H * 0.24f + 86 * s, W - 40 * s, H * 0.24f + 200 * s), th.sub);
        if (upgrade_) {
            std::wstring msg = L"O WinMac " + installedVer_ + L" já está instalado.";
            Label(msg, 12.5f, D2D1::RectF(x, H - 104 * s, W - 30 * s, H - 84 * s), th.sub);
            if (Button(803, D2D1::RectF(W - 40 * s - 190 * s, H - 52 * s, W - 40 * s, H - 18 * s), L"Atualizar para " WINMAC_VERSION, true)) DoUpgrade();
            if (Button(804, D2D1::RectF(W - 40 * s - 340 * s, H - 52 * s, W - 40 * s - 200 * s, H - 18 * s), L"Refazer escolhas")) Go(PLook);
        } else if (Next(L"Começar")) Go(PLook);
    }

    void ThemeCard(D2D1_RECT_F r, int kind, const wchar_t* name) {
        bool sel = opt_.theme == kind;
        if (Clicked(820 + kind, r)) opt_.theme = kind;
        float ph = r.bottom - r.top - 26 * s;
        auto pr = gfx::RR(r.left, r.top, r.right, r.top + ph, 10 * s);
        auto mini = [&](bool dk) {
            auto g = gfx::Gradient(c, D2D1::Point2F(r.left, r.top), D2D1::Point2F(r.right, r.top + ph),
                                   { { 0.f, dk ? gfx::Rgba(24, 30, 64) : gfx::Rgba(200, 225, 255) }, { 1.f, dk ? gfx::Rgba(80, 40, 110) : gfx::Rgba(255, 215, 235) } });
            c->FillRectangle(D2D1::RectF(r.left, r.top, r.right, r.top + ph), g.Get());
            float w = r.right - r.left;
            gfx::FillRect(c, D2D1::RectF(r.left, r.top, r.right, r.top + 9 * s), dk ? gfx::Black(0.35f) : gfx::White(0.6f));
            auto win = gfx::RR(r.left + w * 0.16f, r.top + 18 * s, r.left + w * 0.8f, r.top + ph * 0.66f, 5 * s);
            gfx::Fill(c, win, dk ? gfx::Rgba(40, 40, 46, 0.95f) : gfx::Rgba(250, 250, 252, 0.95f));
            for (int i = 0; i < 3; ++i) gfx::Circle(c, win.rect.left + (6 + i * 7) * s, win.rect.top + 6 * s, 2.3f * s, i == 0 ? gfx::Rgba(255, 95, 87) : i == 1 ? gfx::Rgba(254, 188, 46) : gfx::Rgba(40, 200, 64));
            gfx::Fill(c, gfx::RR(r.left + w * 0.24f, r.top + ph - 16 * s, r.left + w * 0.76f, r.top + ph - 6 * s, 3.5f * s), dk ? gfx::White(0.25f) : gfx::White(0.75f));
        };
        ComPtr<ID2D1RoundedRectangleGeometry> g;
        gfx::d2d->CreateRoundedRectangleGeometry(pr, &g);
        c->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g.Get()), nullptr);
        if (kind == 2) {
            float mx = (r.left + r.right) / 2;
            c->PushAxisAlignedClip(D2D1::RectF(r.left, r.top, mx, r.top + ph), D2D1_ANTIALIAS_MODE_ALIASED);
            mini(false);
            c->PopAxisAlignedClip();
            c->PushAxisAlignedClip(D2D1::RectF(mx, r.top, r.right, r.top + ph), D2D1_ANTIALIAS_MODE_ALIASED);
            mini(true);
            c->PopAxisAlignedClip();
        } else mini(kind == 1);
        c->PopLayer();
        gfx::Stroke(c, pr, sel ? th.accent : th.groupStroke, sel ? 3 * s : 1.f);
        Label(name, 13, D2D1::RectF(r.left, r.top + ph + 4 * s, r.right, r.bottom), sel ? th.text : th.sub, sel ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void PageLook() {
        Title(36 * s, L"Escolha o visual", L"Você pode mudar tudo isso depois em Ajustes do Sistema.");
        float y = 116 * s, L = 40 * s, R = W - 40 * s;
        float cw = (R - L - 2 * 18 * s) / 3;
        const wchar_t* names[3] = { L"Claro", L"Escuro", L"Automático" };
        for (int i = 0; i < 3; ++i) ThemeCard(D2D1::RectF(L + i * (cw + 18 * s), y, L + i * (cw + 18 * s) + cw, y + 130 * s), i, names[i]);
        y += 146 * s;
        Label(L"Cor de destaque", 13, D2D1::RectF(L, y, R, y + 28 * s), th.text, DWRITE_FONT_WEIGHT_MEDIUM);
        for (int i = 0; i < gfx::kAccentCount; ++i) {
            float cx = L + 150 * s + i * 30 * s, cy = y + 14 * s;
            if (Clicked(840 + i, D2D1::RectF(cx - 12 * s, cy - 12 * s, cx + 12 * s, cy + 12 * s))) {
                opt_.accent = i;
                cfg::SetInt(L"Appearance", L"Accent", i);
                gfx::ReloadTheme();
            }
            gfx::Circle(c, cx, cy, 10 * s, gfx::AccentOf(i));
            if (i == opt_.accent) {
                gfx::Circle(c, cx, cy, 3.5f * s, gfx::White(1));
                c->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 12.5f * s, 12.5f * s), gfx::Brush(c, gfx::AccentOf(i)).Get(), 1.5f * s);
            }
        }
        y += 40 * s;
        Label(L"Papel de parede", 13, D2D1::RectF(L, y, R, y + 24 * s), th.text, DWRITE_FONT_WEIGHT_MEDIUM);
        y += 28 * s;
        int n = (int)thumbs_.size();
        float gap = 12 * s, tw = (R - L - (n - 1) * gap) / std::max(1, n), thh = tw * 9 / 16;
        for (int i = 0; i < n; ++i) {
            D2D1_RECT_F r = D2D1::RectF(L + i * (tw + gap), y, L + i * (tw + gap) + tw, y + thh);
            if (Clicked(860 + i, r)) { opt_.wallpaper = i; opt_.useWallpaper = true; }
            auto rr = gfx::RR(r.left, r.top, r.right, r.bottom, 7 * s);
            ComPtr<ID2D1Bitmap> bm;
            if (thumbs_[i] && SUCCEEDED(c->CreateBitmapFromWicBitmap(thumbs_[i].Get(), nullptr, &bm))) {
                ComPtr<ID2D1RoundedRectangleGeometry> g;
                gfx::d2d->CreateRoundedRectangleGeometry(rr, &g);
                c->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g.Get()), nullptr);
                c->DrawBitmap(bm.Get(), r, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
                c->PopLayer();
            }
            bool sel = opt_.useWallpaper && opt_.wallpaper == i;
            gfx::Stroke(c, rr, sel ? th.accent : th.groupStroke, sel ? 3 * s : 1.f);
        }
        y += thh + 10 * s;
        bool keep = !opt_.useWallpaper;
        if (Clicked(870, D2D1::RectF(L, y, L + 260 * s, y + 22 * s))) opt_.useWallpaper = !opt_.useWallpaper;
        gfx::Fill(c, gfx::RR(L, y + 3 * s, L + 16 * s, y + 19 * s, 4 * s), keep ? th.accent : th.field);
        gfx::Stroke(c, gfx::RR(L, y + 3 * s, L + 16 * s, y + 19 * s, 4 * s), keep ? th.accent : th.faint, 1.f);
        if (keep) gfx::Glyph(c, L"\xE73E", 11 * s, D2D1::RectF(L, y + 3 * s, L + 16 * s, y + 19 * s), gfx::White(1));
        Label(L"Manter meu papel de parede atual", 12.5f, D2D1::RectF(L + 24 * s, y, R, y + 22 * s), th.sub);
        if (Back()) Go(PHello);
        if (Next(L"Continuar")) Go(PWifi);
    }

    void PageWifi() {
        Title(36 * s, L"Conecte-se à internet", L"Para o clima nos widgets e para receber atualizações do WinMac. Se preferir, pode pular.");
        float L = 40 * s, R = W - 40 * s, y = 118 * s;
        auto rr = gfx::RR(L, y, R, y + std::min(wifiH_, H - y - 70 * s), 12 * s);
        gfx::Fill(c, rr, th.group);
        gfx::Stroke(c, rr, th.groupStroke, 1.f);
        c->PushAxisAlignedClip(rr.rect, D2D1_ANTIALIAS_MODE_ALIASED);
        float h = wifi_ ? wifi_->Draw(*this, D2D1::RectF(L + 16 * s, y + 4 * s, R - 16 * s, y + 900 * s)) + 8 * s : 100 * s;
        c->PopAxisAlignedClip();
        if (std::fabs(h - wifiH_) > 1) { wifiH_ = h; KeepAnimating(); }
        if (Back()) Go(PLook);
        if (Next(L"Continuar")) Go(PChoices);
    }

    void Choice(float x, float y, float w, const std::wstring& title, const std::wstring& sub, bool& v, int id, const wchar_t* glyph, D2D1_COLOR_F col) {
        float h = 58 * s;
        auto rr = gfx::RR(x, y, x + w, y + h, 10 * s);
        gfx::Fill(c, rr, th.group);
        gfx::Stroke(c, rr, th.groupStroke, 1.f);
        art::SectionIcon(c, D2D1::RectF(x + 12 * s, y + 15 * s, x + 40 * s, y + 43 * s), col, glyph);
        Label(title, 13, D2D1::RectF(x + 50 * s, y + 9 * s, x + w - 60 * s, y + 30 * s), th.text, DWRITE_FONT_WEIGHT_MEDIUM);
        Label(sub, 11.5f, D2D1::RectF(x + 50 * s, y + 29 * s, x + w - 60 * s, y + 49 * s), th.sub);
        Toggle(id, x + w - 12 * s, y + h / 2, v);
    }

    void PageChoices() {
        Title(36 * s, L"Do seu jeito", L"Tudo pode ser mudado depois em Ajustes do Sistema.");
        float L = 40 * s, R = W - 40 * s, y = 110 * s, gap = 12 * s, cw = (R - L - gap) / 2, rh = 58 * s + gap;
        Choice(L, y, cw, L"Botões estilo Mac", L"Vermelho, amarelo e verde", opt_.traffic, 900, L"\xE7C4", gfx::Rgba(255, 95, 87));
        Choice(L + cw + gap, y, cw, L"Alt+Tab do WinMac", L"Prévias de vidro", opt_.switcher, 901, L"\xE8A9", gfx::Rgba(94, 92, 230));
        y += rh;
        Choice(L, y, cw, L"Widgets na mesa", L"Data, relógio e clima", opt_.widgets, 902, L"\xE909", gfx::Rgba(40, 180, 230));
        Choice(L + cw + gap, y, cw, L"Esconder o Dock", L"Aparece ao encostar embaixo", opt_.autohide, 903, L"\xE71D", gfx::Rgba(60, 60, 66));
        y += rh;
        Choice(L, y, cw, L"Cursores estilo Mac", L"Seta arredondada", opt_.cursors, 904, L"\xE8B0", gfx::Rgba(142, 142, 147));
        Choice(L + cw + gap, y, cw, L"Tirar apps pagos", L"Mantém Word, Excel e PowerPoint", opt_.debloat, 905, L"\xE74D", gfx::Rgba(255, 69, 58));
        y += rh + 4 * s;
        Paragraph(L"\"Tirar apps pagos\" remove do seu usuário apps de assinatura e propaganda (Xbox, Clipchamp, Candy Crush, Disney+…) e desliga as sugestões do Windows. Dá para reinstalar pela Microsoft Store.",
                  12, D2D1::RectF(L, y, R, y + 50 * s), th.faint);
        if (Back()) Go(PWifi);
        if (Next(installMode_ ? L"Instalar" : L"Aplicar")) DoInstall();
    }

    void PageInstall() {
        float t = T();
        art::Pose p;
        p.t = t;
        p.hop = std::fabs(std::sin(t * 3.2f)) * 0.5f;
        p.face = art::Face::Excited;
        art::Pingo(c, W / 2.f, H * 0.36f, 150 * s, p);
        Label(error_.empty() ? (updatedOnly_ ? L"Atualizando…" : L"Instalando o WinMac…") : L"Algo deu errado", 20, D2D1::RectF(0, H * 0.58f, (float)W, H * 0.58f + 32 * s),
              th.text, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        float bw = 360 * s, bx = W / 2.f - bw / 2, by = H * 0.58f + 46 * s;
        if (error_.empty()) {
            Progress(D2D1::RectF(bx, by, bx + bw, by + 6 * s), steps_ ? (float)step_ / steps_ : 0.f);
            Label(stepText_, 12.5f, D2D1::RectF(0, by + 14 * s, (float)W, by + 36 * s), th.sub, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            Paragraph(error_, 13, D2D1::RectF(bx, by, bx + bw, by + 60 * s), th.sub, DWRITE_TEXT_ALIGNMENT_CENTER);
            if (Next(L"Tentar de novo")) DoInstall();
            if (Back()) Go(PChoices);
        }
    }

    void PageDone() {
        float t = T();
        art::Confetti(c, D2D1::RectF(0, 0, (float)W, (float)H), (GetTickCount() - pageStart_) / 1000.f + (tAnim_ > 0 ? 2.f : 0.f));
        art::Pose p;
        p.t = t;
        p.wave = 1.f;
        p.face = art::Face::Wink;
        art::Pingo(c, W * 0.27f, H * 0.47f, 190 * s, p);
        float x = W * 0.5f;
        Label(updatedOnly_ ? L"Atualizado! 🎉" : L"Tudo pronto! 🎉", 28, D2D1::RectF(x, H * 0.18f, W - 30 * s, H * 0.18f + 44 * s), th.text, DWRITE_FONT_WEIGHT_BOLD);
        float y = H * 0.18f + 56 * s;
        struct Tip { const wchar_t* g; const wchar_t* t; };
        Tip tips[] = { { L"\xE8A9", L"Alt+Tab troca de janela com prévias de vidro" },
                       { L"\xE713", L"Clique na gotinha no canto da barra para abrir os Ajustes" },
                       { L"\xE721", L"A lupa na barra abre a busca de apps" },
                       { L"\xE895", L"Atualizações chegam sozinhas, direto do GitHub" } };
        for (auto& tp : tips) {
            art::SectionIcon(c, D2D1::RectF(x, y + 4 * s, x + 26 * s, y + 30 * s), th.accent, tp.g);
            float ph = ParagraphHeight(tp.t, 13, W - 40 * s - (x + 38 * s));
            Paragraph(tp.t, 13, D2D1::RectF(x + 38 * s, y + 17 * s - std::min(ph, 34 * s) / 2, W - 40 * s, y + 60 * s), th.text);
            y += std::max(40 * s, ph + 18 * s);
        }
        if (Next(L"Começar a usar")) Close();
    }

    void Paint() override {
        dragH_ = (float)H;
        TrafficLights(20 * s, 22 * s);
        float k = Anim(999, (float)page_, 0.25f);
        float frac = 1.f - std::min(1.f, std::fabs(k - page_));
        float e = 1 - std::pow(1 - frac, 3.f);
        c->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), std::max(0.05f, e)), nullptr);
        D2D1_MATRIX_3X2_F old;
        c->GetTransform(&old);
        c->SetTransform(D2D1::Matrix3x2F::Translation((1 - e) * 26 * s, 0) * old);
        switch (page_) {
        case PHello: PageHello(); break;
        case PLook: PageLook(); break;
        case PWifi: PageWifi(); break;
        case PChoices: PageChoices(); break;
        case PInstall: PageInstall(); break;
        case PDone: PageDone(); break;
        }
        c->SetTransform(old);
        c->PopLayer();
        if (page_ < PInstall) Dots();
    }

    void OnTimer(UINT_PTR id) override {
        if (id == T_ANIM) {
            // só redesenha sem parar nas telas com o Pingo animado e com a janela em foco (economiza CPU)
            if (Active() && (page_ == PHello || page_ == PInstall || page_ == PDone)) Invalidate();
        }
        else if (id == T_WIFI && page_ == PWifi && wifi_ && wifi_->Refresh()) Invalidate();
    }
    void OnCloseRequest() override {
        if (busy_) return;   // não fecha no meio da instalação
        Close();
    }
    void OnClosed() override {
        *alive_ = false;
        if (quitOnClose) PostQuitMessage(0);
    }

public:
    void PreviewAt(int page, float t) {
        page_ = page;
        tAnim_ = t;
        if (page == PWifi && !wifi_) wifi_ = std::make_unique<WifiPanel>();
        if (page == PInstall) { step_ = 4; steps_ = 7; stepText_ = L"Ajustando o Windows…"; }
    }
};
}  // namespace

int Run(bool installMode) {
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\WinMac.Welcome");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND w = FindWindowW(L"WinMac.UiWindow", L"Bem-vindo ao WinMac")) SetForegroundWindow(w);
        if (mtx) CloseHandle(mtx);
        return 0;
    }
    SetCurrentProcessExplicitAppUserModelID(L"WinMac.Welcome");
    Welcome w;
    w.installMode_ = installMode;
    if (!w.Create(L"Bem-vindo ao WinMac", 780, 540, true)) return 1;
    w.Init();
    w.Start();
    w.Show();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
    return 0;
}

void RenderPreview(int page, const std::wstring& png) {
    Welcome w;
    w.quitOnClose = false;
    w.installMode_ = true;
    if (!w.Create(L"Bem-vindo (prévia)", 780, 540, false)) return;
    w.Init();
    w.PreviewAt(page, 1.1f);
    w.SaveFrame(png);
}
}  // namespace welcome
