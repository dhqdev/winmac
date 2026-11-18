#include "traffic.h"
#include "gfx.h"
#include "apps.h"
#include "config.h"
#include <UIAutomation.h>
#include <condition_variable>
#include <deque>

// Como funciona:
// 1) Uma thread de fundo usa a acessibilidade do Windows (UI Automation) para achar os botões reais
//    (minimizar, maximizar, fechar) de cada janela — funciona no Explorador, Bloco de Notas, Chrome, Edge,
//    Configurações, Office... Se não achar com segurança, tenta o retângulo padrão do DWM só em janelas com
//    moldura padrão. Se nada der certo, a janela fica com os botões originais (melhor que um botão torto).
// 2) A posição dos botões é guardada relativa à janela: ao arrastar, a camada só acompanha.
// 3) A camada é pintada com a cor da barra de título e desenha os três círculos do Mac.
namespace traffic {
namespace {
const wchar_t* kCls = L"WinMac.Traffic";
const wchar_t* kTimerCls = L"WinMac.TrafficTimer";
enum { T_SYNC = 1, T_PERIODIC = 2, T_COLOR = 3 };
const UINT WM_PROBED = WM_APP + 60;

struct Geo {
    bool ok = false;
    RECT rel{};          // botões relativos ao canto da janela
    SIZE size{};         // tamanho da janela quando foi medida
    COLORREF bg = RGB(255, 255, 255);
    bool uniform = true; // a barra tem cor lisa (dá para reler a cor pelo lado de fora)
    bool probing = false;
    DWORD failedAt = 0;
};

struct Ov {
    HWND target = nullptr, wnd = nullptr;
    Surface surf;
    RECT btn{};
    float s = 1;
    bool active = false, hover = false, tracking = false, shown = false, painted = false;
    int zone = -1, press = -1;
    Geo geo;
};
std::map<HWND, std::unique_ptr<Ov>> g_ovs;
HWND g_timer = nullptr;
HWINEVENTHOOK g_hooks[6] = {};
bool g_on = false;
std::map<DWORD, bool> g_elev;
std::vector<std::wstring> g_exclude;

// ---------------- sondagem (thread de fundo) ----------------
struct ProbeReq { HWND target; };
struct ProbeRes { HWND target; Geo geo; };
std::mutex g_qm;
std::condition_variable g_qcv;
std::deque<ProbeReq> g_queue;
std::thread g_worker;
std::atomic<bool> g_quit{ false };

const D2D1_COLOR_F kFill[3] = { gfx::Rgba(255, 95, 87), gfx::Rgba(254, 188, 46), gfx::Rgba(40, 200, 64) };
const D2D1_COLOR_F kEdge[3] = { gfx::Rgba(224, 68, 62), gfx::Rgba(222, 161, 35), gfx::Rgba(26, 171, 41) };
const D2D1_COLOR_F kInk[3] = { gfx::Rgba(77, 0, 0, 0.75f), gfx::Rgba(153, 87, 0, 0.85f), gfx::Rgba(0, 101, 0, 0.8f) };

COLORREF Sample(int x, int y) {
    HDC scr = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, 1, 1);
    HGDIOBJ old = SelectObject(mem, bm);
    BitBlt(mem, 0, 0, 1, 1, scr, x, y, SRCCOPY);
    COLORREF c = GetPixel(mem, 0, 0);
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(nullptr, scr);
    return c == CLR_INVALID ? RGB(255, 255, 255) : c;
}

bool Near(COLORREF a, COLORREF b) {
    return std::abs(GetRValue(a) - GetRValue(b)) < 10 && std::abs(GetGValue(a) - GetGValue(b)) < 10 && std::abs(GetBValue(a) - GetBValue(b)) < 10;
}

bool StandardFrame(HWND h) {
    wchar_t cls[96] = {};
    GetClassNameW(h, cls, 96);
    std::wstring c = cls;
    return c.rfind(L"Chrome_WidgetWin", 0) != 0 && c != L"MozillaWindowClass" && c != L"ApplicationFrameWindow";
}

// Botões filhos de um elemento (só os que estão na faixa de cima da janela).
void ButtonsOf(IUIAutomationTreeWalker* walker, IUIAutomationElement* parent, LONG topLimit, std::vector<RECT>& out) {
    ComPtr<IUIAutomationElement> ch;
    if (FAILED(walker->GetFirstChildElement(parent, &ch))) return;
    int guard = 0;
    while (ch && guard++ < 80) {
        CONTROLTYPEID ct = 0;
        ch->get_CurrentControlType(&ct);
        if (ct == UIA_ButtonControlTypeId) {
            RECT r{};
            ch->get_CurrentBoundingRectangle(&r);
            if (r.top <= topLimit && r.right > r.left) out.push_back(r);
        }
        ComPtr<IUIAutomationElement> next;
        if (FAILED(walker->GetNextSiblingElement(ch.Get(), &next))) break;
        ch = next;
    }
}

// Dos botões achados, pega os três colados na ponta direita (minimizar, maximizar, fechar).
bool PickRow(std::vector<RECT> row, const RECT& vf, float s, RECT* out) {
    if (row.size() < 3) return false;
    std::sort(row.begin(), row.end(), [](const RECT& a, const RECT& b) { return a.right < b.right; });
    RECT close = row.back();
    if (vf.right - close.right > 40 * s || close.bottom - close.top < 14 * s || close.bottom - close.top > 72 * s) return false;
    std::vector<RECT> chain{ close };
    for (int i = (int)row.size() - 2; i >= 0 && chain.size() < 3; --i) {
        const RECT& r = row[i];
        if (std::abs(r.top - close.top) > 4 || std::abs(r.bottom - close.bottom) > 4) continue;
        if (std::abs(r.right - chain.back().left) <= 4 && std::abs((r.right - r.left) - (close.right - close.left)) <= 14 * s) chain.push_back(r);
        else if (r.right < chain.back().left - 4) break;
    }
    if (chain.size() < 3) return false;
    *out = RECT{ chain.back().left, close.top, std::min(close.right, vf.right), close.bottom };
    return true;
}

// Acha a fileira de botões da barra de título pela UI Automation.
bool ProbeUia(IUIAutomation* uia, HWND target, RECT* out) {
    RECT vf{};
    if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &vf, sizeof(vf)))) GetWindowRect(target, &vf);
    float s = GetDpiForWindow(target) / 96.f;
    if (s <= 0) s = 1;
    ComPtr<IUIAutomationTreeWalker> walker;
    uia->get_ControlViewWalker(&walker);
    if (!walker) return false;
    LONG topLimit = vf.top + (LONG)(40 * s);
    POINT pts[] = { { vf.right - (LONG)(14 * s), vf.top + (LONG)(9 * s) }, { vf.right - (LONG)(22 * s), vf.top + (LONG)(16 * s) },
                    { vf.right - (LONG)(30 * s), vf.top + (LONG)(5 * s) } };
    for (POINT p : pts) {
        HWND hit = WindowFromPoint(p);
        if (!hit || GetAncestor(hit, GA_ROOT) != target) continue;   // outra janela por cima
        ComPtr<IUIAutomationElement> el;
        if (FAILED(uia->ElementFromPoint(p, &el)) || !el) continue;
        // sobe alguns níveis procurando um elemento cujos filhos sejam os botões da barra
        // (janelas comuns: o próprio pai do botão; Chrome/Edge: o "BrowserCaptionButtonContainer")
        ComPtr<IUIAutomationElement> cur = el;
        for (int level = 0; level < 5 && cur; ++level) {
            std::vector<RECT> row;
            ButtonsOf(walker.Get(), cur.Get(), topLimit, row);
            // contêineres de botões entre os filhos
            ComPtr<IUIAutomationElement> ch;
            walker->GetFirstChildElement(cur.Get(), &ch);
            int guard = 0;
            while (ch && guard++ < 40) {
                BSTR cls = nullptr;
                ch->get_CurrentClassName(&cls);
                bool container = cls && wcsstr(cls, L"CaptionButton");
                if (cls) SysFreeString(cls);
                if (container) ButtonsOf(walker.Get(), ch.Get(), topLimit, row);
                ComPtr<IUIAutomationElement> next;
                if (FAILED(walker->GetNextSiblingElement(ch.Get(), &next))) break;
                ch = next;
            }
            if (PickRow(row, vf, s, out)) return true;
            ComPtr<IUIAutomationElement> up;
            if (FAILED(walker->GetParentElement(cur.Get(), &up))) break;
            cur = up;
        }
    }
    // caminho 2: parte da própria janela e desce poucos níveis (não depende do que está por cima na tela)
    ComPtr<IUIAutomationElement> root;
    if (FAILED(uia->ElementFromHandle(target, &root)) || !root) return false;
    std::vector<RECT> row;
    std::vector<ComPtr<IUIAutomationElement>> level{ root };
    int visited = 0;
    for (int depth = 0; depth < 7 && !level.empty() && visited < 300; ++depth) {
        std::vector<ComPtr<IUIAutomationElement>> next;
        for (auto& node : level) {
            ComPtr<IUIAutomationElement> ch;
            walker->GetFirstChildElement(node.Get(), &ch);
            int guard = 0;
            while (ch && guard++ < 40 && visited++ < 300) {
                CONTROLTYPEID ct = 0;
                ch->get_CurrentControlType(&ct);
                RECT r{};
                ch->get_CurrentBoundingRectangle(&r);
                if (ct == UIA_ButtonControlTypeId) {
                    if (r.top <= topLimit && r.right > r.left) row.push_back(r);
                } else if (r.top <= topLimit && r.right >= vf.right - (LONG)(60 * s)) {
                    next.push_back(ch);   // só desce no que encosta no canto de cima à direita
                }
                ComPtr<IUIAutomationElement> sib;
                if (FAILED(walker->GetNextSiblingElement(ch.Get(), &sib))) break;
                ch = sib;
            }
        }
        if (PickRow(row, vf, s, out)) return true;
        level.swap(next);
    }
    return false;
}

bool ProbeDwm(HWND h, RECT* out) {
    RECT wr, b;
    if (!StandardFrame(h) || !GetWindowRect(h, &wr) || FAILED(DwmGetWindowAttribute(h, DWMWA_CAPTION_BUTTON_BOUNDS, &b, sizeof(b)))) return false;
    float s = GetDpiForWindow(h) / 96.f;
    if (b.right - b.left < 60 * s || b.bottom - b.top < 14 * s || b.bottom - b.top > 70 * s) return false;
    *out = RECT{ wr.left + b.left, wr.top + b.top, wr.left + b.right, wr.top + b.bottom };
    return true;
}

void Worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IUIAutomation> uia;
    CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
    if (uia) {
        // não deixa um app travado segurar a sondagem
        ComPtr<IUIAutomation2> u2;
        if (SUCCEEDED(uia.As(&u2))) {
            u2->put_ConnectionTimeout(1500);
            u2->put_TransactionTimeout(1500);
        }
    }
    while (!g_quit) {
        ProbeReq req;
        {
            std::unique_lock<std::mutex> lk(g_qm);
            g_qcv.wait(lk, [] { return g_quit || !g_queue.empty(); });
            if (g_quit) break;
            req = g_queue.front();
            g_queue.pop_front();
        }
        Sleep(60);   // espera a camada sumir da tela antes de olhar
        auto* res = new ProbeRes{ req.target, {} };
        RECT wr{}, btn{};
        if (IsWindow(req.target) && GetWindowRect(req.target, &wr)) {
            bool ok = (uia && ProbeUia(uia.Get(), req.target, &btn)) || ProbeDwm(req.target, &btn);
            if (ok) {
                float s = GetDpiForWindow(req.target) / 96.f;
                res->geo.ok = true;
                res->geo.rel = RECT{ btn.left - wr.left, btn.top - wr.top, btn.right - wr.left, btn.bottom - wr.top };
                res->geo.size = SIZE{ wr.right - wr.left, wr.bottom - wr.top };
                COLORREF in = Sample(btn.left + (int)(3 * s), btn.top + (int)(3 * s));
                COLORREF outside = Sample(btn.left - (int)(3 * s), (btn.top + btn.bottom) / 2);
                res->geo.bg = in;
                res->geo.uniform = Near(in, outside);
            }
        }
        if (!PostMessageW(g_timer, WM_PROBED, 0, (LPARAM)res)) delete res;
    }
    uia.Reset();
    CoUninitialize();
}

void RequestProbe(Ov& o) {
    if (o.geo.probing) return;
    o.geo.probing = true;
    {
        std::lock_guard<std::mutex> lk(g_qm);
        g_queue.push_back({ o.target });
    }
    g_qcv.notify_one();
}

// ---------------- camadas ----------------
bool Elevated(DWORD pid) {
    auto it = g_elev.find(pid);
    if (it != g_elev.end()) return it->second;
    bool elev = true;   // sem acesso ao token = processo de administrador
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        HANDLE tok = nullptr;
        if (OpenProcessToken(p, TOKEN_QUERY, &tok)) {
            TOKEN_ELEVATION te{};
            DWORD sz = 0;
            if (GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &sz)) elev = te.TokenIsElevated != 0;
            CloseHandle(tok);
        }
        CloseHandle(p);
    }
    if (g_elev.size() > 400) g_elev.clear();
    g_elev[pid] = elev;
    return elev;
}

bool Eligible(HWND h) {
    if (!apps::IsTaskWindow(h) || IsIconic(h)) return false;
    LONG st = GetWindowLongW(h, GWL_STYLE);
    if (!(st & WS_SYSMENU)) return false;
    RECT r;
    GetWindowRect(h, &r);
    if (r.right - r.left < 260 || r.bottom - r.top < 120) return false;
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    if (r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom) return false;
    const auto& w = apps::Describe(h);
    if (Elevated(w.pid)) return false;
    std::wstring exe = PathFindFileNameW(w.exe.c_str());
    for (auto& e : g_exclude)
        if (!e.empty() && _wcsicmp(e.c_str(), exe.c_str()) == 0) return false;
    return true;
}

float Lum(COLORREF c) { return (0.299f * GetRValue(c) + 0.587f * GetGValue(c) + 0.114f * GetBValue(c)) / 255.f; }

void Geometry(float w, float h, float s, float* d, float* gap, float* x0, float* cy) {
    *d = std::min(12.5f * s, h * 0.5f);
    *gap = *d * 1.62f;
    *cy = h / 2;
    *x0 = w - 14 * s - *d / 2 - 2 * *gap;   // centro do botão vermelho
}

void Draw(ID2D1DeviceContext* c, float w, float h, float s, COLORREF bg, bool active, bool hover, int zone, int press) {
    c->FillRectangle(D2D1::RectF(0, 0, w, h), gfx::Brush(c, gfx::Rgba(GetRValue(bg), GetGValue(bg), GetBValue(bg))).Get());
    float d, gap, x0, cy;
    Geometry(w, h, s, &d, &gap, &x0, &cy);
    bool darkBg = Lum(bg) < 0.45f;
    for (int i = 0; i < 3; ++i) {
        float cx = x0 + i * gap;
        bool lit = active || hover;
        D2D1_COLOR_F f = lit ? kFill[i] : (darkBg ? gfx::Rgba(86, 86, 90) : gfx::Rgba(206, 206, 210));
        if (press == i && zone == i) { f.r *= 0.82f; f.g *= 0.82f; f.b *= 0.82f; }
        gfx::Circle(c, cx, cy, d / 2, f);
        c->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), d / 2 - 0.4f, d / 2 - 0.4f), gfx::Brush(c, lit ? kEdge[i] : gfx::Black(darkBg ? 0.3f : 0.12f)).Get(), 0.8f);
        if (hover) {
            float k = d * 0.22f;
            if (i == 0) {
                gfx::Line(c, cx - k, cy - k, cx + k, cy + k, kInk[0], 1.3f * s);
                gfx::Line(c, cx - k, cy + k, cx + k, cy - k, kInk[0], 1.3f * s);
            } else if (i == 1) {
                gfx::Line(c, cx - k * 1.2f, cy, cx + k * 1.2f, cy, kInk[1], 1.4f * s);
            } else {
                gfx::Line(c, cx - k * 1.2f, cy, cx + k * 1.2f, cy, kInk[2], 1.3f * s);
                gfx::Line(c, cx, cy - k * 1.2f, cx, cy + k * 1.2f, kInk[2], 1.3f * s);
            }
        }
    }
}

void Paint(Ov& o) {
    int w = o.btn.right - o.btn.left, h = o.btn.bottom - o.btn.top;
    if (w <= 0 || h <= 0) return;
    if (auto* c = o.surf.Begin(w, h)) {
        Draw(c, (float)w, (float)h, o.s, o.geo.bg, o.active, o.hover, o.zone, o.press);
        o.surf.End(o.btn.left, o.btn.top);
        o.painted = true;
    }
}

int ZoneAt(const Ov& o, int x, int y) {
    float w = (float)(o.btn.right - o.btn.left), h = (float)(o.btn.bottom - o.btn.top);
    float d, gap, x0, cy;
    Geometry(w, h, o.s, &d, &gap, &x0, &cy);
    if (y < 0 || y >= h) return -1;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(x - (x0 + i * gap)) <= gap / 2) return i;
    return -1;
}

void Restack(Ov& o) {
    HWND prev = GetWindow(o.target, GW_HWNDPREV);
    if (prev == o.wnd) return;
    bool topT = (GetWindowLongW(o.target, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    HWND after;
    if (topT) after = HWND_TOPMOST;
    else if (!prev || (GetWindowLongW(prev, GWL_EXSTYLE) & WS_EX_TOPMOST)) after = HWND_TOP;
    else after = prev;
    SetWindowPos(o.wnd, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Hide(Ov& o) {
    if (o.shown) ShowWindow(o.wnd, SW_HIDE);
    o.shown = false;
}

// Reposiciona pela geometria guardada. Se o tamanho da janela mudou, mede de novo.
void Place(Ov& o, bool restack) {
    if (!Eligible(o.target)) { Hide(o); return; }
    RECT wr;
    GetWindowRect(o.target, &wr);
    SIZE sz{ wr.right - wr.left, wr.bottom - wr.top };
    if (!o.geo.ok || sz.cx != o.geo.size.cx || sz.cy != o.geo.size.cy) {
        Hide(o);
        if (!o.geo.probing && (!o.geo.failedAt || GetTickCount() - o.geo.failedAt > 5000)) {
            o.geo.ok = false;
            SetTimer(g_timer, T_SYNC, 220, nullptr);   // espera terminar de redimensionar
        }
        return;
    }
    RECT btn{ wr.left + o.geo.rel.left, wr.top + o.geo.rel.top, wr.left + o.geo.rel.right, wr.top + o.geo.rel.bottom };
    o.s = GetDpiForWindow(o.target) / 96.f;
    bool moved = memcmp(&btn, &o.btn, sizeof(RECT)) != 0;
    o.btn = btn;
    if (moved || !o.painted) Paint(o);
    if (!o.shown) {
        ShowWindow(o.wnd, SW_SHOWNOACTIVATE);
        o.shown = true;
        restack = true;
    }
    if (restack) Restack(o);
}

LRESULT CALLBACK OvProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Ov* o = (Ov*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!o) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        if (!o->tracking) {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            o->tracking = true;
        }
        int z = ZoneAt(*o, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (!o->hover || z != o->zone) { o->hover = true; o->zone = z; Paint(*o); }
        return 0;
    }
    case WM_MOUSELEAVE:
        o->tracking = false;
        o->hover = false;
        o->zone = -1;
        Paint(*o);
        return 0;
    case WM_LBUTTONDOWN:
        o->press = ZoneAt(*o, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        SetCapture(h);
        Paint(*o);
        return 0;
    case WM_LBUTTONUP: {
        if (GetCapture() == h) ReleaseCapture();
        int z = ZoneAt(*o, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        int p = o->press;
        o->press = -1;
        HWND t = o->target;
        if (z >= 0 && z == p) {
            WPARAM cmd = z == 0 ? SC_CLOSE : z == 1 ? SC_MINIMIZE : (IsZoomed(t) ? SC_RESTORE : SC_MAXIMIZE);
            PostMessageW(t, WM_SYSCOMMAND, cmd, 0);
        }
        if (IsWindow(h)) Paint(*o);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        if (ZoneAt(*o, GET_X_LPARAM(l), GET_Y_LPARAM(l)) < 0) PostMessageW(o->target, WM_SYSCOMMAND, IsZoomed(o->target) ? SC_RESTORE : SC_MAXIMIZE, 0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

Ov* Ensure(HWND target) {
    auto it = g_ovs.find(target);
    if (it != g_ovs.end()) return it->second.get();
    auto o = std::make_unique<Ov>();
    o->target = target;
    o->wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kCls, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_inst, nullptr);
    if (!o->wnd) return nullptr;
    SetWindowLongPtrW(o->wnd, GWLP_USERDATA, (LONG_PTR)o.get());
    o->surf.Attach(o->wnd);
    Ov* raw = o.get();
    g_ovs[target] = std::move(o);
    return raw;
}

void Remove(HWND target) {
    auto it = g_ovs.find(target);
    if (it == g_ovs.end()) return;
    it->second->surf.Reset();
    DestroyWindow(it->second->wnd);
    g_ovs.erase(it);
}

// resample: relê a cor da barra (só quando dá para ler pelo lado de fora, sem esconder a camada)
void Sync(bool resample) {
    if (!g_on) return;
    HWND fg = apps::ForegroundApp();
    std::set<HWND> keep;
    for (HWND h : apps::TaskWindows()) {
        if (!Eligible(h)) continue;
        Ov* o = Ensure(h);
        if (!o) continue;
        keep.insert(h);
        bool act = h == fg;
        if (act != o->active) {
            o->active = act;
            o->painted = false;
            if (!o->geo.uniform && o->geo.ok) {   // barra sem cor lisa (ex.: Chrome): relê a cor por dentro, rapidinho
                Hide(*o);
                SetTimer(g_timer, T_COLOR, 90, nullptr);
            }
        }
        RECT wr;
        GetWindowRect(h, &wr);
        bool needProbe = !o->geo.ok || wr.right - wr.left != o->geo.size.cx || wr.bottom - wr.top != o->geo.size.cy;
        if (needProbe) {
            if (!o->geo.probing && (!o->geo.failedAt || GetTickCount() - o->geo.failedAt > 8000)) {
                Hide(*o);
                RequestProbe(*o);
            }
            continue;
        }
        if (resample && o->geo.uniform && o->shown) {
            COLORREF c = Sample(o->btn.left - (int)(3 * o->s), (o->btn.top + o->btn.bottom) / 2);
            if (!Near(c, o->geo.bg)) { o->geo.bg = c; o->painted = false; }
        }
        Place(*o, true);
    }
    std::vector<HWND> gone;
    for (auto& kv : g_ovs)
        if (!keep.count(kv.first)) gone.push_back(kv.first);
    for (HWND h : gone) Remove(h);
}

void RecolorHidden() {
    for (auto& kv : g_ovs) {
        Ov& o = *kv.second;
        if (o.shown || !o.geo.ok || o.geo.uniform || o.geo.probing) continue;
        RECT wr;
        GetWindowRect(o.target, &wr);
        if (wr.right - wr.left != o.geo.size.cx || wr.bottom - wr.top != o.geo.size.cy) continue;
        float s = GetDpiForWindow(o.target) / 96.f;
        o.geo.bg = Sample(wr.left + o.geo.rel.left + (int)(3 * s), wr.top + o.geo.rel.top + (int)(3 * s));
        o.painted = false;
        Place(o, true);
    }
}

void Schedule(int ms) {
    if (g_timer) SetTimer(g_timer, T_SYNC, ms, nullptr);
}

void CALLBACK OnEvent(HWINEVENTHOOK, DWORD ev, HWND h, LONG obj, LONG child, DWORD, DWORD) {
    if (obj != OBJID_WINDOW || child != CHILDID_SELF || !h) return;
    if (ev == EVENT_OBJECT_LOCATIONCHANGE) {
        auto it = g_ovs.find(h);
        if (it != g_ovs.end()) Place(*it->second, false);
        return;
    }
    if (IsOwnWindow(h)) return;
    Schedule(ev == EVENT_SYSTEM_FOREGROUND ? 60 : 80);
}

LRESULT CALLBACK TimerProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER) {
        if (w == T_SYNC) { KillTimer(h, T_SYNC); Sync(false); }
        else if (w == T_PERIODIC) Sync(true);
        else if (w == T_COLOR) { KillTimer(h, T_COLOR); RecolorHidden(); }
        return 0;
    }
    if (m == WM_PROBED) {
        std::unique_ptr<ProbeRes> r((ProbeRes*)l);
        auto it = g_ovs.find(r->target);
        if (it == g_ovs.end()) return 0;
        Ov& o = *it->second;
        o.geo.probing = false;
        if (r->geo.ok) {
            o.geo = r->geo;
            o.painted = false;
            Place(o, true);
        } else {
            o.geo.ok = false;
            o.geo.failedAt = GetTickCount();
            Hide(o);
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}  // namespace

void Init() {
    if (!g_timer) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = OvProc;
        wc.hInstance = g_inst;
        wc.lpszClassName = kCls;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.style = CS_DBLCLKS;
        RegisterClassW(&wc);
        WNDCLASSW tc{};
        tc.lpfnWndProc = TimerProc;
        tc.hInstance = g_inst;
        tc.lpszClassName = kTimerCls;
        RegisterClassW(&tc);
        g_timer = CreateWindowExW(0, kTimerCls, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_inst, nullptr);
    }
    Reload();
}

void Reload() {
    bool on = cfg::Int(L"Windows", L"TrafficLights", 1) != 0;
    g_exclude = cfg::List(L"Windows", L"TrafficExclude");
    if (on && !g_on) {
        g_on = true;
        g_quit = false;
        if (!g_worker.joinable()) g_worker = std::thread(Worker);
        DWORD fl = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        g_hooks[0] = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnEvent, 0, 0, fl);
        g_hooks[1] = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, OnEvent, 0, 0, fl);
        g_hooks[2] = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, OnEvent, 0, 0, fl);
        g_hooks[3] = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, OnEvent, 0, 0, fl);
        g_hooks[4] = SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, OnEvent, 0, 0, fl);
        g_hooks[5] = SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND, nullptr, OnEvent, 0, 0, fl);
        SetTimer(g_timer, T_PERIODIC, 3000, nullptr);
        Schedule(200);
    } else if (!on && g_on) {
        Shutdown();
    } else if (on) {
        for (auto& kv : g_ovs) kv.second->geo = Geo{};
        Schedule(50);
    }
}

void Shutdown() {
    for (auto& hk : g_hooks)
        if (hk) { UnhookWinEvent(hk); hk = nullptr; }
    if (g_timer) KillTimer(g_timer, T_PERIODIC);
    std::vector<HWND> all;
    for (auto& kv : g_ovs) all.push_back(kv.first);
    for (HWND h : all) Remove(h);
    g_on = false;
    g_quit = true;
    g_qcv.notify_all();
    if (g_worker.joinable()) g_worker.detach();   // a thread termina sozinha; não trava a saída
}

void Diagnose(const std::wstring& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, out.c_str(), L"w, ccs=UTF-8") || !f) return;
    ComPtr<IUIAutomation> uia;
    CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
    for (HWND h : apps::TaskWindows()) {
        wchar_t cls[96] = {};
        GetClassNameW(h, cls, 96);
        RECT wr{}, b{};
        GetWindowRect(h, &wr);
        bool el = Eligible(h);
        DWORD t0 = GetTickCount();
        bool u = uia && ProbeUia(uia.Get(), h, &b);
        DWORD t1 = GetTickCount();
        RECT d{};
        bool dw = ProbeDwm(h, &d);
        fwprintf(f, L"%s | elig=%d | win=%d,%d,%d,%d | uia=%d %d,%d,%d,%d (%ums) | dwm=%d %d,%d,%d,%d\n", cls, el, wr.left, wr.top, wr.right, wr.bottom, u, b.left, b.top, b.right, b.bottom,
                 t1 - t0, dw, d.left, d.top, d.right, d.bottom);
    }
    fclose(f);
}

void RenderPreview(const std::wstring& png) {
    float s = 2.f;
    int w = (int)(520 * s), h = (int)(130 * s);
    auto b = gfx::Offscreen(w, h, [&](ID2D1DeviceContext* c) {
        auto bar = [&](float y, COLORREF bg, D2D1_COLOR_F text, bool active, bool hover) {
            c->FillRectangle(D2D1::RectF(0, y, (float)w, y + 31 * s), gfx::Brush(c, gfx::Rgba(GetRValue(bg), GetGValue(bg), GetBValue(bg))).Get());
            gfx::Text(c, L"Sem título - Bloco de Notas", gfx::Font(12 * s), D2D1::RectF(14 * s, y, 400 * s, y + 31 * s), text);
            D2D1_MATRIX_3X2_F old;
            c->GetTransform(&old);
            c->SetTransform(D2D1::Matrix3x2F::Translation(w - 138 * s, y) * old);
            Draw(c, 138 * s, 31 * s, s, bg, active, hover, -1, -1);
            c->SetTransform(old);
        };
        c->Clear(gfx::Rgba(240, 240, 244));
        bar(8 * s, RGB(255, 255, 255), gfx::Black(0.85f), true, false);
        bar(48 * s, RGB(32, 32, 36), gfx::White(0.9f), true, true);
        bar(88 * s, RGB(243, 243, 243), gfx::Black(0.45f), false, false);
    });
    gfx::SaveImage(b.Get(), png);
}
}  // namespace traffic
