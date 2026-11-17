#include "dock.h"
#include "apps.h"
#include "config.h"
#include "popup.h"
#include "art.h"

Dock g_dock;

namespace {
const wchar_t* kGlassCls = L"WinMac.DockGlass";
const wchar_t* kContentCls = L"WinMac.Dock";
enum { TIMER_ANIM = 1, TIMER_REFRESH = 2, TIMER_SAFETY = 3, TIMER_EDGE = 4 };
const float kBouncePeriod = 0.62f;

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD ev, HWND h, LONG obj, LONG child, DWORD, DWORD) {
    if (!h || obj != OBJID_WINDOW || child != CHILDID_SELF) return;
    if (ev == EVENT_SYSTEM_FOREGROUND) { g_dock.ScheduleRefresh(40); g_dock.UpdateAutoHide(); return; }
    if (GetAncestor(h, GA_PARENT) != GetDesktopWindow() || IsOwnWindow(h)) return;
    g_dock.ScheduleRefresh(90);
}

int Section(const Dock::Item& it) {
    if (it.kind == Dock::Item::Folder || it.kind == Dock::Item::Trash) return 2;
    if (it.kind == Dock::Item::App && !it.pinned) return 1;
    return 0;
}

bool SafeMinRect(LPARAM lp, HWND* outHwnd, const RECT* setRc) {
    __try {
        SHELLHOOKINFO* p = (SHELLHOOKINFO*)lp;
        if (outHwnd) *outHwnd = p->hwnd;
        if (setRc) p->rc = *setRc;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}  // namespace

bool Dock::Create() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = g_inst;
    wc.lpszClassName = kGlassCls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    wc.lpfnWndProc = ContentProc;
    wc.lpszClassName = kContentCls;
    RegisterClassW(&wc);

    DWORD ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
    glass_ = CreateWindowExW(ex, kGlassCls, L"WinMac Dock Glass", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_inst, nullptr);
    content_ = CreateWindowExW(ex, kContentCls, L"WinMac Dock", WS_POPUP, 0, 0, 1, 1, glass_, nullptr, g_inst, this);
    if (!glass_ || !content_) return false;
    SetWindowLongPtrW(content_, GWLP_USERDATA, (LONG_PTR)this);
    glassSurf_.Attach(glass_);
    surf_.Attach(content_);
    gfx::SetBlur(glass_, true);

    LoadPinned();
    Relayout();
    Refresh();
    for (auto& it : items_) it->appear = 1.f;

    DWORD fl = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    hooks_[0] = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, WinEventProc, 0, 0, fl);
    hooks_[1] = SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, WinEventProc, 0, 0, fl);
    hooks_[2] = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc, 0, 0, fl);
    SetTimer(content_, TIMER_SAFETY, 4000, nullptr);

    PIDLIST_ABSOLUTE pidl = nullptr;
    if (SUCCEEDED(SHGetSpecialFolderLocation(nullptr, CSIDL_BITBUCKET, &pidl))) {
        SHChangeNotifyEntry e{ pidl, TRUE };
        recycleNotify_ = SHChangeNotifyRegister(content_, SHCNRF_ShellLevel | SHCNRF_InterruptLevel, SHCNE_ALLEVENTS, WM_MD_RECYCLE, 1, &e);
        CoTaskMemFree(pidl);
    }
    UpdateRecycle();
    Render();
    ShowWindow(glass_, SW_SHOWNOACTIVATE);
    ShowWindow(content_, SW_SHOWNOACTIVATE);
    return true;
}

void Dock::Destroy() {
    for (auto h : hooks_) if (h) UnhookWinEvent(h);
    if (trigger_) DestroyWindow(trigger_);
    trigger_ = nullptr;
    if (recycleNotify_) SHChangeNotifyDeregister(recycleNotify_);
    AppBarRemove(content_);
    if (content_) DestroyWindow(content_);
    if (glass_) DestroyWindow(glass_);
    content_ = glass_ = nullptr;
}

static int IconPx(float) { return 128; }

// App de arquivos preferido: o Files (se instalado) ou o Explorador.
static std::wstring FilesAppId() {
    for (const wchar_t* id : { L"Files_1y0xx7n9077q4!App", L"FilesPreview_1y0xx7n9077q4!App", L"49306atecsolution.FilesUWP_et10x9a9vyk8t!App" })
        if (!apps::DisplayName(id).empty()) return id;
    return L"Microsoft.Windows.Explorer";
}

// Primeira execução: fixa no Dock só o que existe neste computador.
static std::vector<std::wstring> DefaultPinned() {
    std::vector<std::wstring> v = { L"::finder", L"::launchpad" };
    const wchar_t* cands[] = { L"MSEdge", L"Chrome", L"308046B0AF4A39CB", L"Microsoft.Office.WINWORD.EXE.15", L"Microsoft.Office.EXCEL.EXE.15",
                               L"Microsoft.Office.POWERPNT.EXE.15", L"Microsoft.Office.OUTLOOK.EXE.15", L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",
                               L"Microsoft.Windows.Photos_8wekyb3d8bbwe!App", L"Microsoft.WindowsStore_8wekyb3d8bbwe!App" };
    int browsers = 0;
    for (int i = 0; i < (int)(sizeof(cands) / sizeof(cands[0])); ++i) {
        if (i < 3 && browsers >= 2) continue;
        if (apps::DisplayName(cands[i]).empty()) continue;
        if (i < 3) browsers++;
        v.push_back(cands[i]);
    }
    v.push_back(L"::ajustes");
    return v;
}

void Dock::LoadPinned() {
    items_.clear();
    int px = IconPx(s_);
    std::vector<std::wstring> pinned = cfg::Has(L"Dock", L"Pinned") ? cfg::List(L"Dock", L"Pinned") : DefaultPinned();
    if (!cfg::Has(L"Dock", L"Pinned")) cfg::SetList(L"Dock", L"Pinned", pinned);
    selfExe_ = Lower(SelfPath());
    for (auto& id : pinned) {
        auto it = std::make_unique<Item>();
        it->pinned = true;
        it->appId = id;
        if (id == L"::launchpad") {
            it->kind = Item::Launchpad;
            it->name = L"Apps";
        } else if (id == L"::ajustes") {
            it->kind = Item::Settings;
            it->name = L"Ajustes do Sistema";
            it->target = selfExe_;
        } else if (id == L"::finder") {
            it->kind = Item::App;
            it->special = id;
            it->appId = FilesAppId();
            it->target = apps::ResolveTarget(it->appId);
            it->name = it->appId == L"Microsoft.Windows.Explorer" ? L"Explorador de Arquivos" : L"Arquivos";
            it->icon = apps::AppIcon(it->appId, it->target, nullptr, px);
        } else {
            it->kind = Item::App;
            it->target = apps::ResolveTarget(id);
            it->name = apps::DisplayName(id);
            if (it->name.empty()) it->name = it->target.empty() ? id : apps::FileDescription(it->target);
            it->icon = apps::AppIcon(id, it->target, nullptr, px);
        }
        items_.push_back(std::move(it));
    }
    for (auto& f : cfg::List(L"Dock", L"Folders")) {
        auto it = std::make_unique<Item>();
        it->kind = Item::Folder;
        it->pinned = true;
        it->appId = f;
        it->name = apps::DisplayName(f);
        if (it->name.empty()) it->name = f;
        items_.push_back(std::move(it));
    }
    auto t = std::make_unique<Item>();
    t->kind = Item::Trash;
    t->pinned = true;
    t->name = L"Lixeira";
    items_.push_back(std::move(t));
}

void Dock::SavePinned() {
    std::vector<std::wstring> pinned, folders;
    for (auto& it : items_) {
        if (it->dying) continue;
        if ((it->kind == Item::App && it->pinned) || it->kind == Item::Launchpad || it->kind == Item::Settings)
            pinned.push_back(it->special.empty() ? it->appId : it->special);
        if (it->kind == Item::Folder) folders.push_back(it->appId);
    }
    cfg::SetList(L"Dock", L"Pinned", pinned);
    cfg::SetList(L"Dock", L"Folders", folders);
}

void Dock::Relayout() {
    s_ = gfx::Scale();
    B_ = std::round(std::clamp(cfg::Int(L"Dock", L"IconSize", 50), 24, 128) * s_);
    gap_ = std::round(7 * s_);
    padX_ = std::round(8 * s_);
    padY_ = std::round(8 * s_);
    margin_ = std::round(8 * s_);
    panelH_ = B_ + 2 * padY_;
    magOn_ = cfg::Int(L"Dock", L"Magnification", 1) != 0;
    mag_ = magOn_ ? std::clamp(cfg::Float(L"Dock", L"MagnifyScale", 1.6f), 1.f, 2.2f) : 1.f;
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    monX_ = mi.rcMonitor.left;
    monY_ = mi.rcMonitor.top;
    monW_ = mi.rcMonitor.right - mi.rcMonitor.left;
    monH_ = mi.rcMonitor.bottom - mi.rcMonitor.top;
    ch_ = (int)std::ceil(margin_ + panelH_ + B_ * (mag_ - 1) + 44 * s_);
    cw_ = monW_;
    cx_ = monX_;
    cy_ = monY_ + monH_ - ch_;
    reserve_ = (int)std::ceil(margin_ + panelH_ + 5 * s_);
    autoHide_ = cfg::Int(L"Dock", L"AutoHide", 0) != 0;
    if (autoHide_) AppBarRemove(content_);
    else AppBarReserve(content_, ABE_BOTTOM, reserve_);
    if (autoHide_) SetTimer(content_, TIMER_EDGE, 100, nullptr);
    else KillTimer(content_, TIMER_EDGE);
    if (!autoHide_) { slideTarget_ = 1.f; hideAt_ = 0; }
    PlaceTrigger();
    UpdateAutoHide();
    glassRect_ = RECT{};
    Render();
}

void Dock::Reload() {
    LoadPinned();
    for (auto& it : items_) it->appear = 1.f;
    Relayout();
    Refresh();
}

void Dock::ScheduleRefresh(int ms) {
    if (content_) SetTimer(content_, TIMER_REFRESH, ms, nullptr);
}

bool Dock::Matches(const Item& it, HWND h) const {
    const auto& w = apps::Describe(h);
    if (it.kind == Item::Settings) return w.exe == selfExe_;
    if (w.exe == selfExe_) return false;
    if (!it.pinned) return _wcsicmp(it.appId.c_str(), (w.aumid.empty() ? w.exe : w.aumid).c_str()) == 0;
    if (!w.aumid.empty() && _wcsicmp(w.aumid.c_str(), it.appId.c_str()) == 0) return true;
    if (it.target.empty() || w.exe != it.target) return false;
    if (w.explicitAumid && StartsWithI(w.aumid, it.appId + L".")) return false;  // PWAs do Chrome etc.
    if (!w.aumid.empty()) {
        for (auto& o : items_)
            if (o.get() != &it && o->pinned && _wcsicmp(o->appId.c_str(), w.aumid.c_str()) == 0) return false;
    }
    return true;
}

void Dock::Refresh() {
    auto wins = apps::TaskWindows();
    bool changed = false;
    std::vector<size_t> before;
    for (auto& it : items_) before.push_back(it->wins.size());
    for (auto& it : items_) if (it->kind == Item::App || it->kind == Item::Settings) it->wins.clear();
    std::vector<HWND> unmatched;
    for (HWND h : wins) {
        bool found = false;
        for (auto& it : items_) {
            if ((it->kind == Item::App || it->kind == Item::Settings) && !it->dying && it->pinned && Matches(*it, h)) { it->wins.push_back(h); found = true; break; }
        }
        if (!found) {
            for (auto& it : items_) {
                if (it->kind == Item::App && !it->pinned && Matches(*it, h)) { it->wins.push_back(h); found = true; break; }
            }
        }
        if (!found) unmatched.push_back(h);
    }
    bool showRecent = cfg::Int(L"Dock", L"ShowRecent", 1) != 0;
    int px = IconPx(s_);
    for (HWND h : unmatched) {
        if (!showRecent) break;
        const auto w = apps::Describe(h);
        std::wstring key = w.aumid.empty() ? w.exe : w.aumid;
        if (key.empty()) continue;
        Item* tgt = nullptr;
        for (auto& it : items_)
            if (it->kind == Item::App && !it->pinned && _wcsicmp(it->appId.c_str(), key.c_str()) == 0) { tgt = it.get(); break; }
        if (!tgt) {
            auto ni = std::make_unique<Item>();
            ni->kind = Item::App;
            ni->appId = key;
            ni->target = w.exe;
            if (!w.aumid.empty()) ni->name = apps::DisplayName(w.aumid);
            if (ni->name.empty()) ni->name = apps::FileDescription(w.exe);
            ni->icon = apps::AppIcon(w.aumid, w.exe, h, px);
            ni->appear = 0.f;
            tgt = ni.get();
            auto pos = std::find_if(items_.begin(), items_.end(), [](auto& x) { return Section(*x) == 2; });
            items_.insert(pos, std::move(ni));
            changed = true;
        }
        tgt->dying = false;
        tgt->wins.push_back(h);
    }
    DWORD now = GetTickCount();
    for (auto& it : items_) {
        if (it->kind != Item::App) continue;
        if (!it->pinned) {
            bool d = it->wins.empty();
            if (d != it->dying) { it->dying = d; changed = true; }
        }
        if (it->bounceStart && !it->wins.empty()) {
            float el = (now - it->bounceStart) / 1000.f;
            int need = std::max(1, (int)std::ceil(el / kBouncePeriod));
            if (need < it->bounceCount) it->bounceCount = need;
        }
    }
    size_t i = 0;
    for (auto& it : items_) {
        if (i < before.size() && before[i] != it->wins.size()) changed = true;
        ++i;
    }
    if (changed) StartAnim();
    else if (!animating_) Render();
}

void Dock::UpdateRecycle() {
    SHQUERYRBINFO q{ sizeof(q) };
    bool full = SUCCEEDED(SHQueryRecycleBinW(nullptr, &q)) && q.i64NumItems > 0;
    if (full != recycleFull_) {
        recycleFull_ = full;
        Render();
    }
}

bool Dock::Busy() const {
    if (std::fabs(amp_ - ampTarget_) > 0.002f) return true;
    if (std::fabs(slide_ - slideTarget_) > 0.001f) return true;
    if (hideAt_) return true;
    DWORD now = GetTickCount();
    for (auto& it : items_) {
        if (it->dying ? it->appear > 0.f : it->appear < 1.f) return true;
        if (it->bounceStart && (now - it->bounceStart) < (DWORD)(it->bounceCount * kBouncePeriod * 1000)) return true;
    }
    return false;
}

void Dock::StartAnim() {
    if (animating_) return;
    animating_ = true;
    lastTick_ = GetTickCount();
    SetTimer(content_, TIMER_ANIM, 15, nullptr);
}

void Dock::Step() {
    DWORD now = GetTickCount();
    float dt = std::min(0.05f, (now - lastTick_) / 1000.f);
    lastTick_ = now;
    float k = 1.f - std::exp(-dt / 0.07f);
    amp_ += (ampTarget_ - amp_) * k;
    if (hideAt_ && now >= hideAt_) { hideAt_ = 0; UpdateAutoHide(); }
    if (slide_ != slideTarget_) {
        float sp = dt / (slideTarget_ > slide_ ? 0.22f : 0.30f);
        slide_ = slideTarget_ > slide_ ? std::min(slideTarget_, slide_ + sp) : std::max(slideTarget_, slide_ - sp);
    }
    if (std::fabs(amp_ - ampTarget_) < 0.002f) amp_ = ampTarget_;
    for (auto& it : items_) {
        if (it->dying) it->appear = std::max(0.f, it->appear - dt / 0.22f);
        else if (it->appear < 1.f) it->appear = std::min(1.f, it->appear + dt / 0.26f);
        if (it->bounceStart && (now - it->bounceStart) >= (DWORD)(it->bounceCount * kBouncePeriod * 1000)) it->bounceStart = 0;
    }
    items_.erase(std::remove_if(items_.begin(), items_.end(), [](auto& it) { return it->dying && it->appear <= 0.f; }), items_.end());
    Render();
    if (!Busy()) {
        KillTimer(content_, TIMER_ANIM);
        animating_ = false;
    }
}

// Calcula a posição de cada ícone (com a ampliação estilo macOS).
void Dock::Layout(float) {
    struct Slot { Item* it; bool sep; bool hole; float appear; float baseW; float w; float size; };
    std::vector<Slot> slots;
    int prevSec = -1;
    float sepW = std::round(16 * s_);
    for (size_t i = 0; i < items_.size(); ++i) {
        Item* it = items_[i].get();
        if (dragging_ && (int)i == dropIdx_) slots.push_back({ nullptr, false, true, 1.f, B_ + gap_, 0, 0 });
        if (dragging_ && (int)i == dragIdx_) { it->size = 0; continue; }
        int sec = Section(*it);
        if (prevSec >= 0 && sec != prevSec) {
            float a = 0;
            for (size_t j = i; j < items_.size() && Section(*items_[j]) == sec; ++j) a = std::max(a, items_[j]->appear);
            slots.push_back({ nullptr, true, false, a, sepW * a, 0, 0 });
        }
        prevSec = sec;
        it->sepBefore = false;
        slots.push_back({ it, false, false, it->appear, (B_ + gap_) * it->appear, 0, 0 });
    }
    if (dragging_ && dropIdx_ >= (int)items_.size()) slots.push_back({ nullptr, false, true, 1.f, B_ + gap_, 0, 0 });
    float baseTotal = 0;
    for (auto& sl : slots) baseTotal += sl.baseW;
    // a janela de conteúdo só cobre o dock + a folga da ampliação e do rótulo (menos pixels por quadro)
    int ncw = std::min(monW_, (int)std::ceil(baseTotal + 2 * padX_ + 2 * (B_ * 1.7f + 190 * s_)));
    if (ncw != cw_) {
        if (mouseX_ >= 0) mouseX_ += (ncw - cw_) / 2.f;
        cw_ = ncw;
        cx_ = monX_ + (monW_ - cw_) / 2;
    }
    float baseLeft = cw_ / 2.f - (baseTotal + 2 * padX_ - gap_) / 2.f;
    float acc = baseLeft + padX_ - gap_ / 2.f;
    float R = B_ * 2.35f;
    for (auto& sl : slots) {
        float c = acc + sl.baseW / 2.f;
        acc += sl.baseW;
        float f = 0;
        if (amp_ > 0.f && mouseX_ >= 0 && !sl.sep) {
            float d = std::fabs(c - mouseX_) / R;
            f = d < 1.f ? (std::cos(d * 3.14159265f) + 1.f) / 2.f : 0.f;
        }
        float size = B_ * (1.f + (mag_ - 1.f) * f * amp_);
        if (sl.sep) sl.w = sl.baseW;
        else {
            sl.size = size * sl.appear;
            sl.w = (size + gap_) * sl.appear;
        }
    }
    float total = 0;
    for (auto& sl : slots) total += sl.w;
    float panelW = total + 2 * padX_ - gap_;
    float left = std::round(cw_ / 2.f - panelW / 2.f);
    float bottom = (float)ch_ - margin_;
    float top = bottom - panelH_;
    panel_ = D2D1::RectF(left, top, left + panelW, bottom);
    float x = left + padX_ - gap_ / 2.f;
    DWORD now = GetTickCount();
    for (auto& sl : slots) {
        if (sl.it) {
            Item* it = sl.it;
            it->x = x;
            it->w = sl.w;
            it->size = sl.size;
            it->iconX = x + (sl.w - sl.size) / 2.f;
            float by = 0;
            if (it->bounceStart) {
                float t = (now - it->bounceStart) / 1000.f;
                if (t < it->bounceCount * kBouncePeriod) by = B_ * 0.42f * std::fabs(std::sin(3.14159265f * t / kBouncePeriod));
            }
            it->iconY = bottom - padY_ - sl.size - by;
        } else if (sl.sep) {
            // guarda a posição do separador num item "virtual": desenhado em Render via sepXs
        }
        x += sl.w;
    }
    // posições dos separadores para o Render
    sepXs_.clear();
    x = left + padX_ - gap_ / 2.f;
    for (auto& sl : slots) {
        if (sl.sep && sl.appear > 0.01f) sepXs_.push_back({ x + sl.w / 2.f, sl.appear });
        x += sl.w;
    }
}

void Dock::DrawLaunchpad(ID2D1DeviceContext* c, D2D1_RECT_F r) {
    float w = r.right - r.left;
    auto rr = gfx::RR(r.left + w * 0.06f, r.top + w * 0.06f, r.right - w * 0.06f, r.bottom - w * 0.06f, w * 0.21f);
    ComPtr<ID2D1GradientStopCollection> sc;
    D2D1_GRADIENT_STOP st[2] = { { 0.f, gfx::Rgba(120, 120, 128) }, { 1.f, gfx::Rgba(44, 44, 48) } };
    c->CreateGradientStopCollection(st, 2, &sc);
    ComPtr<ID2D1LinearGradientBrush> br;
    c->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, rr.rect.top), D2D1::Point2F(0, rr.rect.bottom)), sc.Get(), &br);
    c->FillRoundedRectangle(rr, br.Get());
    gfx::Stroke(c, rr, gfx::White(0.25f), 1.f);
    static const D2D1_COLOR_F cols[9] = { gfx::Rgba(255, 69, 58), gfx::Rgba(255, 159, 10), gfx::Rgba(255, 214, 10),
                                          gfx::Rgba(48, 209, 88), gfx::Rgba(100, 210, 255), gfx::Rgba(10, 132, 255),
                                          gfx::Rgba(94, 92, 230), gfx::Rgba(191, 90, 242), gfx::Rgba(255, 55, 95) };
    float inner = (rr.rect.right - rr.rect.left);
    float cell = inner * 0.2f, sp = inner * 0.08f;
    float gx = rr.rect.left + (inner - (cell * 3 + sp * 2)) / 2, gy = rr.rect.top + (inner - (cell * 3 + sp * 2)) / 2;
    for (int i = 0; i < 9; ++i) {
        float xx = gx + (i % 3) * (cell + sp), yy = gy + (i / 3) * (cell + sp);
        gfx::Fill(c, gfx::RR(xx, yy, xx + cell, yy + cell, cell * 0.28f), cols[i]);
    }
}

void Dock::DrawTrash(ID2D1DeviceContext* c, D2D1_RECT_F r, bool full) {
    float w = r.right - r.left, h = r.bottom - r.top;
    float l = r.left + w * 0.2f, rr_ = r.right - w * 0.2f, t = r.top + h * 0.2f, b = r.bottom - h * 0.06f;
    // corpo levemente afunilado
    ComPtr<ID2D1PathGeometry> body;
    gfx::d2d->CreatePathGeometry(&body);
    ComPtr<ID2D1GeometrySink> sk;
    body->Open(&sk);
    float inset = w * 0.05f;
    sk->BeginFigure(D2D1::Point2F(l, t), D2D1_FIGURE_BEGIN_FILLED);
    sk->AddLine(D2D1::Point2F(rr_, t));
    sk->AddLine(D2D1::Point2F(rr_ - inset, b - w * 0.04f));
    sk->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(rr_ - inset, b), D2D1::Point2F(rr_ - inset - w * 0.05f, b)));
    sk->AddLine(D2D1::Point2F(l + inset + w * 0.05f, b));
    sk->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(l + inset, b), D2D1::Point2F(l + inset, b - w * 0.04f)));
    sk->EndFigure(D2D1_FIGURE_END_CLOSED);
    sk->Close();
    ComPtr<ID2D1GradientStopCollection> sc;
    D2D1_GRADIENT_STOP st[3] = { { 0.f, gfx::White(0.80f) }, { 0.5f, gfx::White(0.55f) }, { 1.f, gfx::White(0.72f) } };
    c->CreateGradientStopCollection(st, 3, &sc);
    ComPtr<ID2D1LinearGradientBrush> br;
    c->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(l, 0), D2D1::Point2F(rr_, 0)), sc.Get(), &br);
    c->FillGeometry(body.Get(), br.Get());
    c->DrawGeometry(body.Get(), gfx::Brush(c, gfx::Black(0.18f)).Get(), 1.f);
    // nervuras verticais
    for (int i = 1; i < 6; ++i) {
        float fx = l + (rr_ - l) * i / 6.f;
        float bx = l + inset + (rr_ - l - 2 * inset) * i / 6.f;
        gfx::Line(c, fx, t + h * 0.08f, bx, b - h * 0.05f, gfx::Black(0.13f), std::max(1.f, w * 0.018f));
    }
    if (full) {
        gfx::Circle(c, l + w * 0.18f, t - h * 0.01f, w * 0.12f, gfx::White(0.95f));
        gfx::Circle(c, l + w * 0.36f, t - h * 0.05f, w * 0.13f, gfx::Rgba(235, 235, 235));
        gfx::Circle(c, l + w * 0.52f, t, w * 0.1f, gfx::White(0.9f));
    }
    // borda superior (boca da lixeira)
    D2D1_ELLIPSE mouth = D2D1::Ellipse(D2D1::Point2F((l + rr_) / 2, t), (rr_ - l) / 2, h * 0.055f);
    c->FillEllipse(mouth, gfx::Brush(c, full ? gfx::Rgba(90, 90, 96, 0.55f) : gfx::Rgba(60, 60, 66, 0.7f)).Get());
    c->DrawEllipse(mouth, gfx::Brush(c, gfx::White(0.92f)).Get(), std::max(1.2f, w * 0.03f));
}

void Dock::DrawFolder(ID2D1DeviceContext* c, D2D1_RECT_F r, const std::wstring& id) {
    float w = r.right - r.left, h = r.bottom - r.top;
    float l = r.left + w * 0.08f, rt = r.right - w * 0.08f, t = r.top + h * 0.18f, b = r.bottom - h * 0.12f;
    float rad = w * 0.06f;
    gfx::Fill(c, gfx::RR(l, t, l + w * 0.38f, t + h * 0.16f, rad), gfx::Rgba(72, 152, 226));
    gfx::Fill(c, gfx::RR(l, t + h * 0.07f, rt, b, rad), gfx::Rgba(72, 152, 226));
    ComPtr<ID2D1GradientStopCollection> sc;
    D2D1_GRADIENT_STOP st[2] = { { 0.f, gfx::Rgba(124, 196, 250) }, { 1.f, gfx::Rgba(88, 168, 238) } };
    c->CreateGradientStopCollection(st, 2, &sc);
    ComPtr<ID2D1LinearGradientBrush> br;
    c->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, t + h * 0.2f), D2D1::Point2F(0, b)), sc.Get(), &br);
    auto front = gfx::RR(l, t + h * 0.2f, rt, b, rad);
    c->FillRoundedRectangle(front, br.Get());
    gfx::Stroke(c, front, gfx::White(0.35f), 1.f);
    if (Lower(id).find(L"download") != std::wstring::npos) {
        float cx = (l + rt) / 2, cy = (t + h * 0.2f + b) / 2 + h * 0.02f, a = w * 0.17f;
        D2D1_COLOR_F col = gfx::Rgba(40, 110, 190, 0.85f);
        gfx::Line(c, cx, cy - a, cx, cy + a, col, w * 0.06f);
        gfx::Line(c, cx - a * 0.75f, cy + a * 0.2f, cx, cy + a, col, w * 0.06f);
        gfx::Line(c, cx + a * 0.75f, cy + a * 0.2f, cx, cy + a, col, w * 0.06f);
    }
}

void Dock::Render() {
    if (!content_ || hidden_) return;
    Layout(0);
    bool dark = gfx::DarkMode();
    // 1) janela de vidro (blur do DWM) exatamente do tamanho do painel
    int oy = (int)OffY();
    RECT gr{ cx_ + (LONG)panel_.left, cy_ + oy + (LONG)panel_.top, cx_ + (LONG)std::ceil(panel_.right), cy_ + oy + (LONG)std::ceil(panel_.bottom) };
    int rad = (int)std::round(panelH_ * 0.36f);
    if (memcmp(&gr, &glassRect_, sizeof(RECT)) != 0) {
        int gw = gr.right - gr.left, gh = gr.bottom - gr.top;
        if (auto* g = glassSurf_.Begin(gw, gh)) {
            gfx::Fill(g, gfx::RR(0, 0, (float)gw, (float)gh, (float)rad), dark ? gfx::Rgba(30, 30, 34, 0.16f) : gfx::Rgba(255, 255, 255, 0.24f));
            glassSurf_.End(gr.left, gr.top);
            gfx::SetRoundRegion(glass_, 0, 0, gw, gh, rad);
        }
        glassRect_ = gr;
    }
    // 2) conteúdo: brilho do vidro, ícones, indicadores e rótulo
    auto* c = surf_.Begin(cw_, ch_);
    if (!c) return;
    auto pr = D2D1::RoundedRect(D2D1::RectF(panel_.left, panel_.top, std::ceil(panel_.right), std::ceil(panel_.bottom)), (float)rad, (float)rad);
    // área "clicável" invisível (alfa mínimo) para o mouse não atravessar o dock
    float bandTop = hovering_ ? panel_.top - B_ * (mag_ - 1.f) - 6 * s_ : panel_.top;
    gfx::FillRect(c, D2D1::RectF(panel_.left - 4 * s_, bandTop, panel_.right + 4 * s_, (float)ch_), gfx::Black(1.f / 255.f));
    gfx::Shadow(c, pr, 16 * s_, 0.30f, 4 * s_);
    gfx::GlassOverlay(c, pr, dark, 1.f);
    for (auto& sx : sepXs_) {
        float x = std::floor(sx.first) + 0.5f;
        gfx::Line(c, x, panel_.top + padY_ * 1.1f, x, panel_.bottom - padY_ * 1.1f, dark ? gfx::White(0.22f * sx.second) : gfx::Black(0.16f * sx.second), 1.f, false);
    }
    for (auto& up : items_) {
        Item* it = up.get();
        if (it->size <= 0.5f) continue;
        D2D1_RECT_F r = D2D1::RectF(it->iconX, it->iconY, it->iconX + it->size, it->iconY + it->size);
        float op = std::min(1.f, it->appear * 1.2f);
        switch (it->kind) {
        case Item::Launchpad: DrawLaunchpad(c, r); break;
        case Item::Trash: DrawTrash(c, r, recycleFull_); break;
        case Item::Folder: DrawFolder(c, r, it->appId); break;
        case Item::Settings: {
            float in = it->size * 0.075f;
            art::SettingsIcon(c, D2D1::RectF(r.left + in, r.top + in, r.right - in, r.bottom - in));
            break;
        }
        default:
            if (it->icon) {
                float in = it->size * 0.075f;
                surf_.DrawBitmap(it->icon.Get(), D2D1::RectF(r.left + in, r.top + in, r.right - in, r.bottom - in), op, !animating_);
            } else {
                gfx::Fill(c, gfx::RR(r.left, r.top, r.right, r.bottom, it->size * 0.22f), gfx::White(0.3f));
            }
        }
        if (!it->wins.empty()) {
            float cxi = it->iconX + it->size / 2.f;
            gfx::Circle(c, cxi, panel_.bottom - padY_ * 0.42f, 2.2f * s_, dark ? gfx::White(0.80f) : gfx::Black(0.55f));
        }
    }
    if (dragging_ && dragIdx_ >= 0 && dragIdx_ < (int)items_.size()) {
        Item* it = items_[dragIdx_].get();
        D2D1_RECT_F r = D2D1::RectF(dragX_ - B_ / 2, dragY_ - B_ / 2, dragX_ + B_ / 2, dragY_ + B_ / 2);
        bool remove = dragY_ < panel_.top - B_ * 1.4f && it->pinned;
        if (it->kind == Item::Launchpad) DrawLaunchpad(c, r);
        else if (it->kind == Item::Settings) art::SettingsIcon(c, r);
        else if (it->icon) surf_.DrawBitmap(it->icon.Get(), r, remove ? 0.45f : 0.9f);
        if (remove) gfx::Text(c, L"Remover", gfx::Font(12 * s_, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(r.left - 20 * s_, r.bottom, r.right + 20 * s_, r.bottom + 18 * s_),
                              gfx::White(0.95f), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    // rótulo com o nome (como no macOS)
    if (hovering_ && !dragging_ && hoverIdx_ >= 0 && hoverIdx_ < (int)items_.size() && !PopupMenuOpen()) {
        Item* it = items_[hoverIdx_].get();
        auto* f = gfx::Font(13 * s_, DWRITE_FONT_WEIGHT_MEDIUM);
        auto sz = gfx::Measure(it->name, f);
        float bw = std::min(sz.width, 360 * s_) + 22 * s_, bh = 26 * s_;
        float bx = it->iconX + it->size / 2 - bw / 2;
        bx = std::clamp(bx, 4.f, cw_ - bw - 4.f);
        float by = it->iconY - bh - 9 * s_;
        if (by < 2) by = 2;
        auto lr = gfx::RR(bx, by, bx + bw, by + bh, 7 * s_);
        gfx::Shadow(c, lr, 8 * s_, 0.25f, 2 * s_);
        gfx::Fill(c, lr, dark ? gfx::Rgba(36, 36, 40, 0.92f) : gfx::Rgba(246, 246, 248, 0.94f));
        gfx::Stroke(c, lr, dark ? gfx::White(0.16f) : gfx::Black(0.10f), 1.f);
        gfx::Text(c, it->name, f, D2D1::RectF(bx + 11 * s_, by, bx + bw - 11 * s_, by + bh), dark ? gfx::White(0.95f) : gfx::Black(0.86f),
                  DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    surf_.End(cx_, cy_ + oy);
}

bool Dock::IconFor(HWND h, RECT* rc, ComPtr<IWICBitmapSource>* icon) {
    Refresh();
    for (auto& it : items_) {
        if (std::find(it->wins.begin(), it->wins.end(), h) == it->wins.end()) continue;
        if (rc) *rc = RECT{ cx_ + (LONG)it->iconX, cy_ + (LONG)it->iconY, cx_ + (LONG)(it->iconX + it->size), cy_ + (LONG)(it->iconY + it->size) };
        if (icon) *icon = it->icon;
        return !hidden_;
    }
    return false;
}

double Dock::Bench(int frames) {
    hovering_ = true;
    amp_ = ampTarget_ = 1.f;
    bool wasAnim = animating_;
    animating_ = true;
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    for (int i = 0; i < frames; ++i) {
        mouseX_ = panel_.left + (panel_.right - panel_.left) * (float)i / frames;
        hoverIdx_ = i % (int)items_.size();
        Render();
    }
    QueryPerformanceCounter(&b);
    animating_ = wasAnim;
    hovering_ = false;
    amp_ = ampTarget_ = 0.f;
    hoverIdx_ = -1;
    Render();
    return (double)(b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart / frames;
}

int Dock::HitItem(POINT p) const {
    if (p.y < panel_.top - B_ * (mag_ - 1.f) - 4 * s_) return -1;
    for (size_t i = 0; i < items_.size(); ++i) {
        auto& it = items_[i];
        if (it->size <= 0.5f || it->dying) continue;
        if (p.x >= it->x && p.x < it->x + it->w && p.y >= std::min(it->iconY, panel_.top)) return (int)i;
    }
    return -1;
}

void Dock::Click(int idx, bool middle) {
    if (idx < 0 || idx >= (int)items_.size()) return;
    Item* it = items_[idx].get();
    DWORD now = GetTickCount();
    switch (it->kind) {
    case Item::Launchpad: ToggleLaunchpad(); return;
    case Item::Settings:
        if (!it->wins.empty()) { apps::Activate(it->wins[0]); return; }
        OpenAjustes();
        it->bounceStart = now;
        it->bounceCount = 2;
        StartAnim();
        return;
    case Item::Trash: OpenPath(L"shell:RecycleBinFolder"); return;
    case Item::Folder: {
        // abre a pasta no Files (mais parecido com o Finder); sem ele, no Explorador
        std::wstring path = it->appId;
        PIDLIST_ABSOLUTE pidl = nullptr;
        wchar_t buf[MAX_PATH] = {};
        if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) {
            if (SHGetPathFromIDListW(pidl, buf)) path = buf;
            CoTaskMemFree(pidl);
        }
        wchar_t la[MAX_PATH];
        ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Microsoft\\WindowsApps\\files-stable.exe", la, MAX_PATH);
        if (buf[0] && cfg::Int(L"Dock", L"UseFiles", 1) && GetFileAttributesW(la) != INVALID_FILE_ATTRIBUTES)
            ShellExecuteW(nullptr, L"open", la, (L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        else OpenPath(it->appId);
        return;
    }
    default: break;
    }
    if (it->wins.empty() || middle) {
        if (apps::Launch(it->appId)) {
            it->bounceStart = now;
            it->bounceCount = 3;
            StartAnim();
        }
        return;
    }
    HWND fg = apps::ForegroundApp();
    auto pos = std::find(it->wins.begin(), it->wins.end(), fg);
    if (it->wins.size() == 1) {
        HWND h = it->wins[0];
        if (fg == h && !IsIconic(h)) apps::Minimize(h);
        else apps::Activate(h);
        return;
    }
    if (pos != it->wins.end()) {
        // já está na frente: alterna para a próxima janela do mesmo app
        HWND next = it->wins.back();
        apps::Activate(next);
    } else {
        apps::Activate(it->wins.front());
    }
}

void Dock::ContextMenu(int idx, POINT pt) {
    std::vector<PMItem> m;
    auto add = [&](const std::wstring& t, int id, bool en = true, bool chk = false) {
        PMItem i;
        i.text = t;
        i.id = id;
        i.enabled = en;
        i.checked = chk;
        m.push_back(i);
    };
    Item* it = (idx >= 0 && idx < (int)items_.size()) ? items_[idx].get() : nullptr;
    float anchorX = (float)pt.x;
    if (it) anchorX = cx_ + it->iconX + it->size / 2;
    if (!it) {
        add(L"Ampliação", 20, true, magOn_);
        add(L"Mostrar apps recentes no Dock", 23, true, cfg::Int(L"Dock", L"ShowRecent", 1) != 0);
        add(L"Ocultar automaticamente", 24, true, autoHide_);
        m.push_back(PMItem::Sep());
        add(L"Ajustes do Dock…", 21);
    } else if (it->kind == Item::Trash) {
        add(L"Abrir", 11);
        m.push_back(PMItem::Sep());
        add(L"Esvaziar Lixeira", 10, recycleFull_);
    } else if (it->kind == Item::Launchpad || it->kind == Item::Settings) {
        add(L"Abrir", 1);
        m.push_back(PMItem::Sep());
        add(L"Remover do Dock", 2);
    } else if (it->kind == Item::Folder) {
        add(L"Abrir", 1);
        m.push_back(PMItem::Sep());
        add(L"Remover do Dock", 2);
    } else {
        for (size_t i = 0; i < it->wins.size() && i < 12; ++i) {
            std::wstring t = apps::Title(it->wins[i]);
            if (t.empty()) t = it->name;
            add(t, 100 + (int)i, true, it->wins[i] == apps::ForegroundApp());
        }
        if (!it->wins.empty()) m.push_back(PMItem::Sep());
        add(it->wins.empty() ? L"Abrir" : L"Nova janela", 1);
        add(L"Manter no Dock", 2, true, it->pinned);
        if (!it->target.empty()) add(L"Mostrar no Explorador", 3);
        if (!it->wins.empty()) {
            m.push_back(PMItem::Sep());
            add(L"Ocultar", 4);
            add(L"Encerrar", 5);
        }
    }
    hovering_ = false;
    tracking_ = false;
    ampTarget_ = 0;
    StartAnim();
    int r = PopupMenu(m, (int)anchorX, cy_ + (int)panel_.top - (int)(10 * s_), nullptr, true);
    if (r <= 0) return;
    // o item pode ter sido removido enquanto o menu estava aberto
    bool alive = false;
    for (auto& x : items_) if (x.get() == it) alive = true;
    if (it && !alive) return;
    switch (r) {
    case 1:
        if (it->kind == Item::Launchpad) ToggleLaunchpad();
        else if (it->kind == Item::Settings) OpenAjustes();
        else if (it->kind == Item::Folder) Click(idx, false);
        else Click(idx, true);
        break;
    case 2:
        if (it->kind != Item::App) {
            it->dying = true;
            it->pinned = false;
            SavePinned();
            StartAnim();
        } else if (it->pinned) {
            it->pinned = false;
            if (it->wins.empty()) it->dying = true;
            else {  // continua aberto: vai para a seção de recentes
                auto& w = apps::Describe(it->wins[0]);
                it->appId = w.aumid.empty() ? w.exe : w.aumid;
                auto self = std::find_if(items_.begin(), items_.end(), [&](auto& x) { return x.get() == it; });
                auto keep = std::move(*self);
                items_.erase(self);
                auto pos = std::find_if(items_.begin(), items_.end(), [](auto& x) { return Section(*x) == 2; });
                items_.insert(pos, std::move(keep));
            }
            SavePinned();
            StartAnim();
        } else {
            it->pinned = true;
            auto self = std::find_if(items_.begin(), items_.end(), [&](auto& x) { return x.get() == it; });
            auto keep = std::move(*self);
            items_.erase(self);
            auto pos = std::find_if(items_.begin(), items_.end(), [](auto& x) { return Section(*x) != 0; });
            items_.insert(pos, std::move(keep));
            SavePinned();
            Render();
        }
        break;
    case 3: {
        std::wstring args = L"/select,\"" + it->target + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        break;
    }
    case 4: for (HWND h : it->wins) apps::Minimize(h); break;
    case 5: for (HWND h : it->wins) apps::Close(h); break;
    case 10: SHEmptyRecycleBinW(nullptr, nullptr, 0); UpdateRecycle(); break;
    case 11: OpenPath(L"shell:RecycleBinFolder"); break;
    case 20:
        cfg::SetInt(L"Dock", L"Magnification", magOn_ ? 0 : 1);
        Relayout();
        break;
    case 21: OpenAjustes(L"dock"); break;
    case 22: QuitWinMac(); break;
    case 24:
        cfg::SetInt(L"Dock", L"AutoHide", autoHide_ ? 0 : 1);
        Relayout();
        break;
    case 23:
        cfg::SetInt(L"Dock", L"ShowRecent", cfg::Int(L"Dock", L"ShowRecent", 1) ? 0 : 1);
        Refresh();
        break;
    default:
        if (r >= 100 && it && r - 100 < (int)it->wins.size()) apps::Activate(it->wins[r - 100]);
    }
}

void Dock::SetHidden(bool hidden) {
    if (hidden == hidden_) return;
    hidden_ = hidden;
    PlaceTrigger();
    ShowWindow(glass_, hidden ? SW_HIDE : SW_SHOWNOACTIVATE);
    ShowWindow(content_, hidden ? SW_HIDE : SW_SHOWNOACTIVATE);
    if (!hidden) {
        glassRect_ = RECT{};
        Render();
    }
}

void Dock::OnShellHook(WPARAM code, LPARAM lp, LRESULT* res) {
    switch (code) {
    case HSHELL_WINDOWCREATED:
    case HSHELL_WINDOWREPLACED:
        ScheduleRefresh(40);
        break;
    case HSHELL_WINDOWDESTROYED:
        apps::ForgetWindow((HWND)lp);
        ScheduleRefresh(40);
        break;
    case HSHELL_WINDOWACTIVATED:
    case HSHELL_RUDEAPPACTIVATED:
        ScheduleRefresh(60);
        break;
    case HSHELL_FLASH:
        for (auto& it : items_) {
            if (std::find(it->wins.begin(), it->wins.end(), (HWND)lp) != it->wins.end() && !it->bounceStart) {
                it->bounceStart = GetTickCount();
                it->bounceCount = 2;
                StartAnim();
            }
        }
        break;
    case HSHELL_GETMINRECT: {
        HWND h = nullptr;
        bool ok = SafeMinRect(lp, &h, nullptr);
        if (gfx::s_dumpDir.size()) Log(L"GETMINRECT lp=%p ok=%d hwnd=%p", (void*)lp, ok, h);
        if (!ok) break;
        for (auto& it : items_) {
            if (std::find(it->wins.begin(), it->wins.end(), h) != it->wins.end() && it->size > 1) {
                RECT rc{ cx_ + (LONG)it->iconX, cy_ + (LONG)it->iconY, cx_ + (LONG)(it->iconX + it->size), cy_ + (LONG)(it->iconY + it->size) };
                if (SafeMinRect(lp, nullptr, &rc) && res) *res = TRUE;
                break;
            }
        }
        break;
    }
    }
}

LRESULT CALLBACK Dock::ContentProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* d = (Dock*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (d && d->content_ == h) return d->OnMsg(m, w, l);
    return DefWindowProcW(h, m, w, l);
}

LRESULT Dock::OnMsg(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_TIMER:
        if (w == TIMER_ANIM) Step();
        else if (w == TIMER_REFRESH) {
            KillTimer(content_, TIMER_REFRESH);
            Refresh();
        } else if (w == TIMER_EDGE) {
            // ocultar automaticamente: encostar o mouse na borda de baixo revela; sair de perto esconde
            POINT pt;
            GetCursorPos(&pt);
            bool atEdge = pt.y >= monY_ + monH_ - 3 && pt.x >= monX_ && pt.x < monX_ + monW_;
            if (slideTarget_ < 1.f && trigger_) SetWindowPos(trigger_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (slideTarget_ < 1.f && atEdge) Reveal();
            else if (slideTarget_ >= 1.f && !hovering_ && !dragging_ && !PopupMenuOpen() && !hideAt_ &&
                     pt.y < monY_ + monH_ - reserve_ - (int)(30 * s_)) {
                HWND fg = GetForegroundWindow();
                wchar_t cls[64] = {};
                if (fg) GetClassNameW(fg, cls, 64);
                if (!fg || !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || IsOwnWindow(fg)) return 0;  // área de trabalho: fica visível
                hideAt_ = GetTickCount() + 300;
                StartAnim();
            }
        } else if (w == TIMER_SAFETY) {
            static int n = 0;
            Refresh();
            if (++n % 4 == 0) UpdateRecycle();
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (!tracking_) {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, content_, 0 };
            TrackMouseEvent(&t);
            tracking_ = true;
        }
        mouseX_ = (float)p.x;
        hideAt_ = 0;
        if (slideTarget_ < 1.f) Reveal();
        if (!hovering_) {
            hovering_ = true;
            ampTarget_ = 1.f;
            StartAnim();
        }
        if (pressIdx_ >= 0 && !dragging_ && (w & MK_LBUTTON) &&
            (std::abs(p.x - pressPt_.x) > 6 * s_ || std::abs(p.y - pressPt_.y) > 6 * s_)) {
            auto k = items_[pressIdx_]->kind;
            if (k == Item::App || k == Item::Launchpad || k == Item::Settings) {
                dragging_ = true;
                dragIdx_ = pressIdx_;
                SetCapture(content_);
            }
        }
        if (dragging_) {
            dragX_ = (float)p.x;
            dragY_ = (float)p.y;
            int pinnedEnd = 0;
            for (auto& it : items_) if (Section(*it) == 0) pinnedEnd++;
            int drop = 0;
            for (int i = 0; i < pinnedEnd; ++i) {
                if (i == dragIdx_) continue;
                if (items_[i]->x + items_[i]->w / 2 < p.x) drop = i + 1;
            }
            if (dragY_ < panel_.top - B_ * 1.4f) drop = -1;
            dropIdx_ = drop;
        }
        int hi = HitItem(p);
        hoverIdx_ = hi;
        if (!animating_) Render();
        return 0;
    }
    case WM_MOUSELEAVE:
        if (autoHide_) { hideAt_ = GetTickCount() + 350; StartAnim(); }
        tracking_ = false;
        if (dragging_) return 0;
        hovering_ = false;
        hoverIdx_ = -1;
        ampTarget_ = 0.f;
        StartAnim();
        return 0;
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        pressIdx_ = HitItem(p);
        pressPt_ = p;
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (dragging_) {
            ReleaseCapture();
            dragging_ = false;
            if (dragIdx_ >= 0 && dragIdx_ < (int)items_.size()) {
                Item* it = items_[dragIdx_].get();
                if (dropIdx_ < 0) {
                    if (it->pinned) {
                        it->pinned = false;
                        if (it->kind != Item::App || it->wins.empty()) it->dying = true;
                        else {
                            auto& wi = apps::Describe(it->wins[0]);
                            it->appId = wi.aumid.empty() ? wi.exe : wi.aumid;
                            auto keep = std::move(items_[dragIdx_]);
                            items_.erase(items_.begin() + dragIdx_);
                            auto pos = std::find_if(items_.begin(), items_.end(), [](auto& x) { return Section(*x) == 2; });
                            items_.insert(pos, std::move(keep));
                        }
                    }
                } else {
                    auto keep = std::move(items_[dragIdx_]);
                    items_.erase(items_.begin() + dragIdx_);
                    int at = dropIdx_ > dragIdx_ ? dropIdx_ - 1 : dropIdx_;
                    at = std::clamp(at, 0, (int)items_.size());
                    keep->pinned = true;
                    items_.insert(items_.begin() + at, std::move(keep));
                }
                SavePinned();
            }
            dragIdx_ = dropIdx_ = -1;
            pressIdx_ = -1;
            StartAnim();
            return 0;
        }
        int hi = HitItem(p);
        if (hi >= 0 && hi == pressIdx_) Click(hi, false);
        pressIdx_ = -1;
        return 0;
    }
    case WM_MBUTTONUP: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        Click(HitItem(p), true);
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        POINT sp = p;
        ClientToScreen(content_, &sp);
        ContextMenu(HitItem(p), sp);
        return 0;
    }
    case WM_MD_RECYCLE:
        UpdateRecycle();
        return 0;
    case WM_MD_APPBAR:
        if (w == ABN_FULLSCREENAPP) PostMessageW(g_ctrl, WM_MD_FULLSCREEN, l, 0);
        else if (w == ABN_POSCHANGED && !autoHide_) AppBarReserve(content_, ABE_BOTTOM, reserve_);
        return 0;
    }
    return DefWindowProcW(content_, m, w, l);
}

float Dock::OffY() const {
    float e = 1.f - std::pow(1.f - slide_, 3.f);
    return (1.f - e) * (ch_ + 4.f);
}

void Dock::Reveal() {
    hideAt_ = 0;
    if (slideTarget_ < 1.f) { slideTarget_ = 1.f; StartAnim(); }
    PlaceTrigger();
}

// Faixa invisível de 2 px na borda de baixo: recebe o mouse quando o Dock está escondido e o faz aparecer.
static LRESULT CALLBACK TriggerProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEMOVE || m == WM_LBUTTONDOWN) { g_dock.Reveal(); return 0; }
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, m, w, l);
}

void Dock::PlaceTrigger() {
    if (!autoHide_ || hidden_) {
        if (trigger_) ShowWindow(trigger_, SW_HIDE);
        return;
    }
    if (!trigger_) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = TriggerProc;
        wc.hInstance = g_inst;
        wc.lpszClassName = L"WinMac.DockEdge";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        trigger_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"WinMac.DockEdge", L"", WS_POPUP, 0, 0, 1, 1,
                                   nullptr, nullptr, g_inst, nullptr);
        if (!trigger_) return;
    }
    int th = std::max(2, (int)std::round(2 * s_));
    if (slideTarget_ < 1.f) {
        SetLayeredWindowAttributes(trigger_, 0, 1, LWA_ALPHA);
        SetWindowPos(trigger_, HWND_TOPMOST, monX_, monY_ + monH_ - th, monW_, th, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else ShowWindow(trigger_, SW_HIDE);
}

// Visível só quando a área de trabalho está em foco, o mouse está no dock ou um menu está aberto.
void Dock::UpdateAutoHide() {
    if (!content_) return;
    float target = 1.f;
    if (autoHide_ && !hovering_ && !dragging_ && !PopupMenuOpen() && !hideAt_) {
        HWND fg = GetForegroundWindow();
        wchar_t cls[64] = {};
        if (fg) GetClassNameW(fg, cls, 64);
        bool desktop = !fg || !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || IsOwnWindow(fg);
        POINT pt;
        GetCursorPos(&pt);
        bool atEdge = pt.y >= monY_ + monH_ - 3;
        if (!desktop && !atEdge) target = 0.f;
    }
    if (target != slideTarget_) { slideTarget_ = target; StartAnim(); PlaceTrigger(); }
}
