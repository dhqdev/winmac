#include "widgets.h"
#include "apps.h"
#include "config.h"
#include "popup.h"
#include "net.h"

Widgets g_widgets;

namespace {
enum Kind { kDate = 0, kMonth = 1, kClock = 2, kWeather = 3 };
const wchar_t* kNames[] = { L"Date", L"Month", L"Clock", L"Weather" };
const wchar_t* kCls = L"WinMac.Widget";
enum { T_MINUTE = 1, T_WEATHER = 2, T_FIRSTWEATHER = 3 };

struct Weather {
    bool ok = false;
    float temp = 0, tmax = 0, tmin = 0;
    int code = 0;
    bool day = true;
    struct H { std::wstring label; float t; int code; bool day; };
    std::vector<H> hours;
};
Weather g_weather;

std::wstring WText(int code) {
    switch (code) {
    case 0: return L"Céu limpo";
    case 1: return L"Predominantemente limpo";
    case 2: return L"Parcialmente nublado";
    case 3: return L"Nublado";
    case 45: case 48: return L"Neblina";
    case 51: case 53: case 55: return L"Garoa";
    case 56: case 57: return L"Garoa congelante";
    case 61: return L"Chuva fraca";
    case 63: return L"Chuva";
    case 65: return L"Chuva forte";
    case 66: case 67: return L"Chuva congelante";
    case 71: case 73: case 75: case 77: return L"Neve";
    case 80: case 81: return L"Pancadas de chuva";
    case 82: return L"Chuva torrencial";
    case 85: case 86: return L"Pancadas de neve";
    case 95: return L"Trovoadas";
    case 96: case 99: return L"Trovoadas com granizo";
    }
    return L"—";
}

std::wstring WEmoji(int code, bool day) {
    switch (code) {
    case 0: return day ? L"☀️" : L"\U0001F319";
    case 1: return day ? L"\U0001F324️" : L"\U0001F319";
    case 2: return day ? L"⛅" : L"☁️";
    case 3: return L"☁️";
    case 45: case 48: return L"\U0001F32B️";
    case 51: case 53: case 55: case 80: case 81: return day ? L"\U0001F326️" : L"\U0001F327️";
    case 71: case 73: case 75: case 77: case 85: case 86: return L"❄️";
    case 95: case 96: case 99: return L"⛈️";
    }
    return L"\U0001F327️";
}

std::wstring Upper(const std::wstring& s) {
    std::wstring o(s.size() + 8, L'\0');
    int n = LCMapStringEx(L"pt-BR", LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, s.c_str(), (int)s.size(), o.data(), (int)o.size(), nullptr, nullptr, 0);
    o.resize(n > 0 ? n : 0);
    return n > 0 ? o : s;
}

float JNum(const net::JVal* v) { return v ? (float)v->Number() : 0.f; }

void CALLBACK FgHook(HWINEVENTHOOK, DWORD, HWND h, LONG, LONG, DWORD, DWORD) { g_widgets.OnForeground(h); }
HWINEVENTHOOK g_fgHook = nullptr;
}  // namespace

bool Widgets::Owns(HWND h) const {
    for (auto& w : ws_) if (w->hwnd == h) return true;
    return false;
}

bool Widgets::Create() {
    if (!cfg::Int(L"Widgets", L"Enabled", 1)) return true;
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = g_inst;
    wc.lpszClassName = kCls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    bool hasWeather = !cfg::Str(L"Weather", L"Lat").empty() && !cfg::Str(L"Weather", L"Lon").empty();
    for (int k = 0; k < 4; ++k) {
        if (k == kWeather && !hasWeather) continue;
        auto w = std::make_unique<W>();
        w->kind = k;
        w->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kCls, kNames[k], WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_inst, nullptr);
        if (!w->hwnd) continue;
        SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, (LONG_PTR)w.get());
        w->surf.Attach(w->hwnd);
        gfx::SetBlur(w->hwnd, true);
        ws_.push_back(std::move(w));
    }
    if (ws_.empty()) return true;
    timerWnd_ = ws_[0]->hwnd;
    Relayout();
    for (auto& w : ws_) ShowWindow(w->hwnd, SW_SHOWNOACTIVATE);
    KeepAtDesktop(false);
    SYSTEMTIME st;
    GetLocalTime(&st);
    SetTimer(timerWnd_, T_MINUTE, (60 - st.wSecond) * 1000 - st.wMilliseconds + 50, nullptr);
    if (hasWeather) {
        SetTimer(timerWnd_, T_FIRSTWEATHER, 1500, nullptr);
        SetTimer(timerWnd_, T_WEATHER, 30 * 60 * 1000, nullptr);
    }
    g_fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, FgHook, 0, 0, WINEVENT_OUTOFCONTEXT);
    return true;
}

void Widgets::Destroy() {
    if (g_fgHook) UnhookWinEvent(g_fgHook);
    g_fgHook = nullptr;
    for (auto& w : ws_) {
        w->surf.Reset();
        DestroyWindow(w->hwnd);
    }
    ws_.clear();
}

void Widgets::Place(W& w) {
    float s = s_;
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT wa = mi.rcWork;
    float smallSz = 152, gap = 18;
    float lw = w.kind <= kMonth ? smallSz : 320, lh = smallSz;
    float monW = (mi.rcMonitor.right - mi.rcMonitor.left) / s;
    float defX = 0, defY = gap;
    switch (w.kind) {
    case kDate: defX = gap; break;
    case kMonth: defX = gap + smallSz + 14; break;
    case kClock: defX = monW / 2 - lw / 2; break;
    case kWeather: defX = monW - gap - lw; break;
    }
    std::wstring pos = cfg::Str(L"Widgets", (std::wstring(L"Pos") + kNames[w.kind]).c_str());
    float px = defX, py = defY;
    if (!pos.empty()) swscanf_s(pos.c_str(), L"%f,%f", &px, &py);
    w.lx = px;
    w.ly = py;
    w.w = (int)std::round(lw * s);
    w.h = (int)std::round(lh * s);
    w.x = wa.left + (int)std::round(px * s);
    w.y = wa.top + (int)std::round(py * s);
}

void Widgets::Relayout() {
    s_ = gfx::Scale();
    for (auto& w : ws_) {
        Place(*w);
        gfx::SetRoundRegion(w->hwnd, 0, 0, w->w, w->h, (int)std::round(22 * s_));
        Draw(*w);
    }
}

void Widgets::RedrawAll() {
    for (auto& w : ws_) Draw(*w);
}

void Widgets::Draw(W& w) {
    auto* c = w.surf.Begin(w.w, w.h);
    if (!c) return;
    float s = s_, W_ = (float)w.w, H_ = (float)w.h;
    bool dark = gfx::DarkMode();
    auto rr = gfx::RR(0, 0, W_, H_, 22 * s);
    SYSTEMTIME st;
    GetLocalTime(&st);
    D2D1_COLOR_F fg = dark ? gfx::White(0.96f) : gfx::Black(0.86f);
    D2D1_COLOR_F sub = dark ? gfx::White(0.6f) : gfx::Black(0.5f);
    D2D1_COLOR_F red = gfx::Rgba(255, 69, 58);
    auto tint = gfx::ReduceTransparency() ? gfx::Panel(dark, 0, 0) : (dark ? gfx::Rgba(24, 24, 28, 0.36f) : gfx::Rgba(255, 255, 255, 0.40f));
    float p = 16 * s;
    switch (w.kind) {
    case kDate: {
        gfx::Fill(c, rr, tint);
        gfx::GlassOverlay(c, rr, dark, 0.85f);
        wchar_t wd[32] = {}, mo[64] = {};
        GetDateFormatEx(L"pt-BR", 0, &st, L"dddd", wd, 32, nullptr);
        GetDateFormatEx(L"pt-BR", 0, &st, L"MMMM 'de' yyyy", mo, 64, nullptr);
        std::wstring wds = wd;
        wds = Upper(wds);
        gfx::Text(c, wds, gfx::Font(12.5f * s, DWRITE_FONT_WEIGHT_BOLD), D2D1::RectF(p, p - 2 * s, W_ - p, p + 18 * s), red);
        gfx::Text(c, std::to_wstring(st.wDay), gfx::Font(64 * s, DWRITE_FONT_WEIGHT_LIGHT), D2D1::RectF(p - 3 * s, p + 12 * s, W_ - p, p + 96 * s), fg,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        std::wstring mos = mo;
        if (!mos.empty()) mos[0] = towupper(mos[0]);
        gfx::Text(c, mos, gfx::Font(13 * s, DWRITE_FONT_WEIGHT_MEDIUM), D2D1::RectF(p, H_ - p - 20 * s, W_ - p, H_ - p + 2 * s), sub);
        break;
    }
    case kMonth: {
        gfx::Fill(c, rr, tint);
        gfx::GlassOverlay(c, rr, dark, 0.85f);
        wchar_t mo[32] = {};
        GetDateFormatEx(L"pt-BR", 0, &st, L"MMMM", mo, 32, nullptr);
        std::wstring mos = mo;
        mos = Upper(mos);
        gfx::Text(c, mos, gfx::Font(11.5f * s, DWRITE_FONT_WEIGHT_BOLD), D2D1::RectF(p, p - 4 * s, W_ - p, p + 14 * s), red);
        const wchar_t* ini[] = { L"D", L"S", L"T", L"Q", L"Q", L"S", L"S" };
        float gx = 12 * s, gw = (W_ - 2 * gx) / 7.f, top = p + 16 * s, rowH = 15.5f * s;
        auto* f = gfx::Font(10 * s, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        auto* fd = gfx::Font(10.5f * s, DWRITE_FONT_WEIGHT_MEDIUM);
        for (int i = 0; i < 7; ++i) gfx::Text(c, ini[i], f, D2D1::RectF(gx + i * gw, top, gx + (i + 1) * gw, top + rowH), sub, DWRITE_TEXT_ALIGNMENT_CENTER);
        SYSTEMTIME first = st;
        first.wDay = 1;
        FILETIME ft;
        SystemTimeToFileTime(&first, &ft);
        FileTimeToSystemTime(&ft, &first);  // calcula o dia da semana
        int startDow = first.wDayOfWeek;
        static const int dim[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        int days = dim[st.wMonth - 1];
        if (st.wMonth == 2 && ((st.wYear % 4 == 0 && st.wYear % 100 != 0) || st.wYear % 400 == 0)) days = 29;
        for (int d = 1; d <= days; ++d) {
            int cell = startDow + d - 1;
            int row = cell / 7, col = cell % 7;
            float x0 = gx + col * gw, y0 = top + rowH * (row + 1);
            bool today = d == st.wDay;
            if (today) gfx::Circle(c, x0 + gw / 2, y0 + rowH / 2, 8 * s, red);
            gfx::Text(c, std::to_wstring(d), fd, D2D1::RectF(x0, y0, x0 + gw, y0 + rowH), today ? gfx::White(1) : (col == 0 ? sub : fg),
                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }
        break;
    }
    case kClock: {
        gfx::Fill(c, rr, dark ? gfx::Rgba(18, 22, 40, 0.42f) : gfx::Rgba(20, 30, 60, 0.30f));
        gfx::GlassOverlay(c, rr, true, 0.9f);
        wchar_t t[16] = {}, d[64] = {};
        GetTimeFormatEx(L"pt-BR", TIME_NOSECONDS, &st, L"HH:mm", t, 16);
        GetDateFormatEx(L"pt-BR", 0, &st, L"dddd, d 'de' MMMM", d, 64, nullptr);
        std::wstring ds = d;
        if (!ds.empty()) ds[0] = towupper(ds[0]);
        gfx::Text(c, t, gfx::Font(66 * s, DWRITE_FONT_WEIGHT_SEMI_LIGHT), D2D1::RectF(0, 8 * s, W_, H_ - 40 * s), gfx::White(0.97f),
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        std::wstring city = cfg::Str(L"Weather", L"City", L"");
        std::wstring line = city.empty() ? ds : city + L"  ·  " + ds;
        gfx::Text(c, line, gfx::Font(13 * s, DWRITE_FONT_WEIGHT_MEDIUM), D2D1::RectF(p, H_ - 42 * s, W_ - p, H_ - 16 * s), gfx::White(0.72f), DWRITE_TEXT_ALIGNMENT_CENTER);
        break;
    }
    case kWeather: {
        const Weather& wx = g_weather;
        bool day = !wx.ok || wx.day;
        ComPtr<ID2D1GradientStopCollection> sc;
        D2D1_GRADIENT_STOP gs[2] = { { 0.f, day ? gfx::Rgba(58, 130, 220, 0.62f) : gfx::Rgba(24, 34, 74, 0.70f) },
                                     { 1.f, day ? gfx::Rgba(28, 86, 176, 0.62f) : gfx::Rgba(10, 14, 38, 0.70f) } };
        c->CreateGradientStopCollection(gs, 2, &sc);
        ComPtr<ID2D1LinearGradientBrush> br;
        c->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, H_)), sc.Get(), &br);
        c->FillRoundedRectangle(rr, br.Get());
        gfx::GlassOverlay(c, rr, true, 0.9f);
        D2D1_COLOR_F wf = gfx::White(0.97f), ws = gfx::White(0.75f);
        std::wstring city = cfg::Str(L"Weather", L"City", L"Clima");
        gfx::Text(c, city + L" ➤", gfx::Font(14 * s, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(p, p - 4 * s, W_ * 0.62f, p + 16 * s), wf);
        if (!wx.ok) {
            gfx::Text(c, L"Carregando…", gfx::Font(13 * s), D2D1::RectF(p, p + 20 * s, W_ - p, p + 50 * s), ws);
            break;
        }
        wchar_t tb[64];
        swprintf_s(tb, L"%d°", (int)std::lround(wx.temp));
        gfx::Text(c, tb, gfx::Font(40 * s, DWRITE_FONT_WEIGHT_LIGHT), D2D1::RectF(p - 2 * s, p + 10 * s, W_ * 0.6f, p + 58 * s), wf,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        gfx::Emoji(c, WEmoji(wx.code, wx.day), 20 * s, D2D1::RectF(W_ - p - 30 * s, p - 4 * s, W_ - p, p + 24 * s));
        gfx::Text(c, WText(wx.code), gfx::Font(12.5f * s, DWRITE_FONT_WEIGHT_MEDIUM), D2D1::RectF(W_ * 0.45f, p + 22 * s, W_ - p, p + 40 * s), wf,
                  DWRITE_TEXT_ALIGNMENT_TRAILING);
        swprintf_s(tb, L"Máx.: %d°  Mín.: %d°", (int)std::lround(wx.tmax), (int)std::lround(wx.tmin));
        gfx::Text(c, tb, gfx::Font(12 * s, DWRITE_FONT_WEIGHT_MEDIUM), D2D1::RectF(W_ * 0.4f, p + 40 * s, W_ - p, p + 56 * s), ws, DWRITE_TEXT_ALIGNMENT_TRAILING);
        int n = std::min(6, (int)wx.hours.size());
        float cw = (W_ - 2 * p) / std::max(1, n), y0 = H_ - 66 * s;
        gfx::Line(c, p, y0 - 4 * s, W_ - p, y0 - 4 * s, gfx::White(0.18f), 1.f, false);
        for (int i = 0; i < n; ++i) {
            auto& hh = wx.hours[i];
            float x0 = p + i * cw;
            gfx::Text(c, hh.label, gfx::Font(11 * s, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x0, y0, x0 + cw, y0 + 16 * s), ws, DWRITE_TEXT_ALIGNMENT_CENTER);
            gfx::Emoji(c, WEmoji(hh.code, hh.day), 15 * s, D2D1::RectF(x0, y0 + 16 * s, x0 + cw, y0 + 38 * s));
            swprintf_s(tb, L"%d°", (int)std::lround(hh.t));
            gfx::Text(c, tb, gfx::Font(12.5f * s, DWRITE_FONT_WEIGHT_SEMI_BOLD), D2D1::RectF(x0, y0 + 38 * s, x0 + cw, y0 + 54 * s), wf, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        break;
    }
    }
    w.surf.End(w.x, w.y);
}

void Widgets::FetchWeather() {
    std::wstring lat = cfg::Str(L"Weather", L"Lat"), lon = cfg::Str(L"Weather", L"Lon");
    if (lat.empty() || lon.empty()) return;
    HWND target = timerWnd_;
    std::thread([lat, lon, target]() {
        std::wstring path = L"/v1/forecast?latitude=" + lat + L"&longitude=" + lon +
                            L"&current=temperature_2m,weather_code,is_day&hourly=temperature_2m,weather_code,is_day"
                            L"&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=2";
        std::string body = net::Get(L"https://api.open-meteo.com" + path);
        auto* w = new Weather();
        net::JVal root;
        if (!body.empty() && net::ParseJson(body, root) && root.t == net::JVal::Obj) {
            const net::JVal* cur = root.Get("current");
            const net::JVal* hourly = root.Get("hourly");
            const net::JVal* daily = root.Get("daily");
            if (cur) {
                w->ok = true;
                w->temp = JNum(cur->Get("temperature_2m"));
                w->code = (int)JNum(cur->Get("weather_code"));
                w->day = JNum(cur->Get("is_day")) > 0.5f;
                std::string now = cur->Get("time") ? cur->Get("time")->s : "";
                if (daily) {
                    auto* mx = daily->Get("temperature_2m_max");
                    auto* mn = daily->Get("temperature_2m_min");
                    if (mx && !mx->a.empty()) w->tmax = (float)mx->a[0].n;
                    if (mn && !mn->a.empty()) w->tmin = (float)mn->a[0].n;
                }
                if (hourly) {
                    auto* tt = hourly->Get("time");
                    auto* tp = hourly->Get("temperature_2m");
                    auto* cd = hourly->Get("weather_code");
                    auto* dy = hourly->Get("is_day");
                    if (tt && tp && cd) {
                        size_t start = 0;
                        std::string hourNow = now.substr(0, 13);
                        for (size_t i = 0; i < tt->a.size(); ++i)
                            if (tt->a[i].s.substr(0, 13) == hourNow) { start = i; break; }
                        for (size_t i = start; i < tt->a.size() && w->hours.size() < 6; ++i) {
                            Weather::H h;
                            std::string hh = tt->a[i].s.size() >= 13 ? tt->a[i].s.substr(11, 2) : "";
                            h.label = i == start ? L"Agora" : std::wstring(hh.begin(), hh.end());
                            h.t = i < tp->a.size() ? (float)tp->a[i].n : 0;
                            h.code = i < cd->a.size() ? (int)cd->a[i].n : 0;
                            h.day = dy && i < dy->a.size() ? dy->a[i].n > 0.5 : true;
                            w->hours.push_back(h);
                        }
                    }
                }
            }
        }
        if (!PostMessageW(target, WM_MD_WEATHER, 0, (LPARAM)w)) delete w;
    }).detach();
}

void Widgets::OnWeather(void* data) {
    auto* w = (Weather*)data;
    if (w->ok) g_weather = *w;
    delete w;
    for (auto& x : ws_) if (x->kind == kWeather) Draw(*x);
}

// Mantém os widgets logo acima da área de trabalho (abaixo de todas as janelas).
void Widgets::KeepAtDesktop(bool raise) {
    for (auto& w : ws_) {
        if (raise) SetWindowPos(w->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        else SetWindowPos(w->hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void Widgets::OnForeground(HWND fg) {
    if (ws_.empty() || !fg) return;
    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 64);
    bool desktop = !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW");
    if (!desktop) {
        if (!Owns(fg) && !IsOwnWindow(fg)) KeepAtDesktop(false);
        return;
    }
    // "Mostrar área de trabalho": só sobe os widgets se nenhuma janela de app estiver visível por cima deles
    for (HWND h : apps::TaskWindows()) {
        if (IsIconic(h)) continue;
        RECT r, a;
        GetWindowRect(h, &r);
        for (auto& w : ws_) {
            RECT b{ w->x, w->y, w->x + w->w, w->y + w->h };
            if (IntersectRect(&a, &r, &b)) return;
        }
    }
    KeepAtDesktop(true);
}

LRESULT CALLBACK Widgets::Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    auto* w = (W*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!w) return DefWindowProcW(h, m, wp, lp);
    Widgets& self = g_widgets;
    switch (m) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_WINDOWPOSCHANGING: {
        auto* wpos = (WINDOWPOS*)lp;
        if (!(wpos->flags & SWP_NOZORDER) && wpos->hwndInsertAfter != HWND_BOTTOM && wpos->hwndInsertAfter != HWND_TOP) wpos->flags |= SWP_NOZORDER;
        break;
    }
    case WM_TIMER:
        if (wp == T_MINUTE) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            SetTimer(h, T_MINUTE, (60 - st.wSecond) * 1000 - st.wMilliseconds + 50, nullptr);
            for (auto& x : self.ws_) if (x->kind != kWeather) self.Draw(*x);
        } else if (wp == T_WEATHER) {
            self.FetchWeather();
        } else if (wp == T_FIRSTWEATHER) {
            KillTimer(h, T_FIRSTWEATHER);
            self.FetchWeather();
        }
        return 0;
    case WM_MD_WEATHER:
        self.OnWeather((void*)lp);
        return 0;
    case WM_LBUTTONDOWN:
        w->dragging = true;
        w->moved = false;
        GetCursorPos(&w->grab);
        w->grab.x -= w->x;
        w->grab.y -= w->y;
        SetCapture(h);
        return 0;
    case WM_MOUSEMOVE:
        if (w->dragging) {
            POINT p;
            GetCursorPos(&p);
            int nx = p.x - w->grab.x, ny = p.y - w->grab.y;
            if (std::abs(nx - w->x) > 3 || std::abs(ny - w->y) > 3 || w->moved) {
                w->moved = true;
                w->x = nx;
                w->y = ny;
                SetWindowPos(h, nullptr, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (w->dragging) {
            w->dragging = false;
            ReleaseCapture();
            if (w->moved) {
                MONITORINFO mi{ sizeof(mi) };
                GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
                wchar_t b[64];
                swprintf_s(b, L"%.0f,%.0f", (w->x - mi.rcWork.left) / self.s_, (w->y - mi.rcWork.top) / self.s_);
                cfg::Set(L"Widgets", (std::wstring(L"Pos") + kNames[w->kind]).c_str(), b);
            } else {
                switch (w->kind) {
                case kWeather:
                    if (!apps::Launch(L"Microsoft.BingWeather_8wekyb3d8bbwe!App")) OpenAjustes(L"dock");
                    break;
                case kClock: apps::Launch(L"Microsoft.WindowsAlarms_8wekyb3d8bbwe!App"); break;
                default: OpenPath(L"ms-settings:dateandtime"); break;
                }
            }
        }
        return 0;
    case WM_RBUTTONUP: {
        POINT p;
        GetCursorPos(&p);
        std::vector<PMItem> items;
        PMItem a; a.text = L"Atualizar clima"; a.id = 1; items.push_back(a);
        PMItem b; b.text = L"Restaurar posições"; b.id = 2; items.push_back(b);
        PMItem c; c.text = L"Ocultar widgets"; c.id = 3; items.push_back(c);
        PMItem d; d.text = L"Ajustes dos widgets…"; d.id = 4; items.push_back(d);
        int r = PopupMenu(items, p.x, p.y);
        if (r == 1) self.FetchWeather();
        else if (r == 2) {
            for (auto& x : self.ws_) cfg::Set(L"Widgets", (std::wstring(L"Pos") + kNames[x->kind]).c_str(), L"");
            self.Relayout();
        } else if (r == 3) {
            cfg::SetInt(L"Widgets", L"Enabled", 0);
            PostMessageW(g_ctrl, WM_MD_RELOAD, RL_WIDGETS, 0);   // destrói fora do próprio procedimento da janela
        } else if (r == 4) {
            OpenAjustes(L"dock");
        }
        return 0;
    }
    }
    return DefWindowProcW(h, m, wp, lp);
}
