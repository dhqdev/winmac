#include "gfx.h"
#include "config.h"

namespace gfx {
ComPtr<ID2D1Factory1> d2d;
ComPtr<IDWriteFactory> dw;
ComPtr<IWICImagingFactory> wic;
static bool s_dark = true;
static int s_accent = 0;
static float s_glass = 1.f;
static bool s_reduce = false;
static std::wstring s_family = L"Segoe UI";
std::wstring s_dumpDir;
void SetDumpDir(const std::wstring& dir) { s_dumpDir = dir; }

struct ACCENT_POLICY { int AccentState; int AccentFlags; DWORD GradientColor; int AnimationId; };
struct WCAD { int Attribute; PVOID Data; SIZE_T Size; };

bool Init() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr, (void**)d2d.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)dw.GetAddressOf()))) return false;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    ComPtr<IDWriteFontCollection> coll;
    if (SUCCEEDED(dw->GetSystemFontCollection(&coll, TRUE))) {
        UINT32 idx = 0; BOOL exists = FALSE;
        coll->FindFamilyName(L"Inter", &idx, &exists);
        if (exists) s_family = L"Inter";
        else {
            coll->FindFamilyName(L"Inter Variable", &idx, &exists);
            if (exists) s_family = L"Inter Variable";
        }
    }
    ReloadTheme();
    return true;
}

float Scale() {
    HMONITOR m = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    UINT dx = 96, dy = 96;
    if (FAILED(GetDpiForMonitor(m, MDT_EFFECTIVE_DPI, &dx, &dy))) dx = 96;
    return dx / 96.f;
}

void ReloadTheme() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        s_dark = (v == 0);
    s_accent = std::clamp(cfg::Int(L"Appearance", L"Accent", 0), 0, kAccentCount - 1);
    s_glass = std::clamp(cfg::Int(L"Appearance", L"Glass", 100), 20, 160) / 100.f;
    s_reduce = cfg::Int(L"Appearance", L"ReduceTransparency", 0) != 0;
}
bool DarkMode() { return s_dark; }
D2D1_COLOR_F AccentOf(int i) {
    static const int dk[8][3] = { { 10, 132, 255 }, { 191, 90, 242 }, { 255, 55, 95 }, { 255, 69, 58 }, { 255, 159, 10 }, { 255, 204, 0 }, { 48, 209, 88 }, { 142, 142, 147 } };
    static const int lt[8][3] = { { 0, 122, 255 }, { 175, 82, 222 }, { 255, 45, 85 }, { 255, 59, 48 }, { 255, 149, 0 }, { 230, 180, 0 }, { 52, 199, 89 }, { 128, 128, 133 } };
    i = std::clamp(i, 0, kAccentCount - 1);
    auto& a = s_dark ? dk[i] : lt[i];
    return Rgba(a[0], a[1], a[2]);
}
D2D1_COLOR_F Accent() { return AccentOf(s_accent); }
float GlassStrength() { return s_glass; }
bool ReduceTransparency() { return s_reduce; }
D2D1_COLOR_F Panel(bool dark, float ad, float al) {
    float a = dark ? ad : al;
    if (s_reduce) a = 0.94f;
    return dark ? Rgba(30, 30, 34, a) : Rgba(246, 246, 248, a);
}
const wchar_t* UiFamily() { return s_family.c_str(); }

void SetBlur(HWND h, bool on, int state, DWORD abgr) {
    static auto f = (BOOL(WINAPI*)(HWND, WCAD*))GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    if (!f) return;
    ACCENT_POLICY ap{ on ? state : 0, on ? 2 : 0, abgr, 0 };
    WCAD d{ 19, &ap, sizeof(ap) };
    f(h, &d);
}

void SetRoundRegion(HWND h, int x, int y, int w, int hgt, int radius) {
    HRGN rg = CreateRoundRectRgn(x, y, x + w + 1, y + hgt + 1, radius * 2, radius * 2);
    if (!SetWindowRgn(h, rg, TRUE)) DeleteObject(rg);
}

IDWriteTextFormat* Font(float px, DWRITE_FONT_WEIGHT w, const wchar_t* family) {
    static std::map<std::wstring, ComPtr<IDWriteTextFormat>> cache;
    std::wstring fam = family ? family : s_family;
    wchar_t key[160];
    swprintf_s(key, L"%s|%d|%d", fam.c_str(), (int)(px * 10), (int)w);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(dw->CreateTextFormat(fam.c_str(), nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, px, L"pt-br", &f)))
        dw->CreateTextFormat(L"Segoe UI", nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, px, L"pt-br", &f);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    cache[key] = f;
    return f.Get();
}

D2D1_SIZE_F Measure(const std::wstring& s, IDWriteTextFormat* f, float maxW) {
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, maxW, 10000.f, &l))) return { 0, 0 };
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return { m.widthIncludingTrailingWhitespace, m.height };
}

ComPtr<ID2D1SolidColorBrush> Brush(ID2D1DeviceContext* c, D2D1_COLOR_F col) {
    ComPtr<ID2D1SolidColorBrush> b;
    c->CreateSolidColorBrush(col, &b);
    return b;
}

void Text(ID2D1DeviceContext* c, const std::wstring& s, IDWriteTextFormat* f, D2D1_RECT_F r, D2D1_COLOR_F col,
          DWRITE_TEXT_ALIGNMENT a, DWRITE_PARAGRAPH_ALIGNMENT pa, bool ellipsis) {
    if (s.empty()) return;
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, std::max(1.f, r.right - r.left), std::max(1.f, r.bottom - r.top), &l))) return;
    l->SetTextAlignment(a);
    l->SetParagraphAlignment(pa);
    if (ellipsis) {
        DWRITE_TRIMMING t{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        ComPtr<IDWriteInlineObject> sign;
        if (SUCCEEDED(dw->CreateEllipsisTrimmingSign(l.Get(), &sign))) l->SetTrimming(&t, sign.Get());
    }
    auto b = Brush(c, col);
    c->DrawTextLayout(D2D1::Point2F(r.left, r.top), l.Get(), b.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

void Emoji(ID2D1DeviceContext* c, const std::wstring& s, float px, D2D1_RECT_F r) {
    Text(c, s, Font(px, DWRITE_FONT_WEIGHT_REGULAR, L"Segoe UI Emoji"), r, White(1), DWRITE_TEXT_ALIGNMENT_CENTER,
         DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
}

// Converte HBITMAP (de ícone) para bitmap WIC PBGRA, detectando se o alfa já vem pré-multiplicado.
static ComPtr<IWICBitmapSource> FromHBitmap(HBITMAP hb) {
    BITMAP bm{};
    if (!GetObject(hb, sizeof(bm), &bm)) return nullptr;
    int w = bm.bmWidth, h = abs(bm.bmHeight);
    if (w <= 0 || h <= 0) return nullptr;
    std::vector<BYTE> px((size_t)w * h * 4);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    int ok = GetDIBits(dc, hb, 0, h, px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (!ok) return nullptr;
    bool anyAlpha = false, straight = false;
    for (size_t i = 0; i < px.size(); i += 4) {
        BYTE a = px[i + 3];
        if (a) anyAlpha = true;
        if (px[i] > a || px[i + 1] > a || px[i + 2] > a) straight = true;
    }
    if (!anyAlpha) {
        for (size_t i = 0; i < px.size(); i += 4) px[i + 3] = 255;
    } else if (straight) {
        for (size_t i = 0; i < px.size(); i += 4) {
            unsigned a = px[i + 3];
            px[i] = (BYTE)(px[i] * a / 255);
            px[i + 1] = (BYTE)(px[i + 1] * a / 255);
            px[i + 2] = (BYTE)(px[i + 2] * a / 255);
        }
    }
    ComPtr<IWICBitmap> b;
    if (FAILED(wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA, w * 4, (UINT)px.size(), px.data(), &b))) return nullptr;
    return b;
}

ComPtr<IWICBitmapSource> LoadShellImage(const std::wstring& name, int px, bool iconOnly) {
    ComPtr<IShellItemImageFactory> f;
    if (FAILED(SHCreateItemFromParsingName(name.c_str(), nullptr, IID_PPV_ARGS(&f)))) return nullptr;
    HBITMAP hb = nullptr;
    SIIGBF flags = SIIGBF_BIGGERSIZEOK | (iconOnly ? SIIGBF_ICONONLY : SIIGBF_RESIZETOFIT);
    if (FAILED(f->GetImage(SIZE{ px, px }, flags, &hb)) || !hb) {
        if (!iconOnly || FAILED(f->GetImage(SIZE{ px, px }, SIIGBF_BIGGERSIZEOK, &hb)) || !hb) return nullptr;
    }
    auto r = FromHBitmap(hb);
    DeleteObject(hb);
    return r;
}

ComPtr<IWICBitmapSource> Downscale(IWICBitmapSource* src, int px) {
    if (!src) return nullptr;
    UINT w = 0, h = 0;
    src->GetSize(&w, &h);
    if ((int)std::max(w, h) <= px) return src;
    float s = (float)px / std::max(w, h);
    ComPtr<IWICBitmapScaler> sc;
    wic->CreateBitmapScaler(&sc);
    if (FAILED(sc->Initialize(src, std::max(1u, (UINT)std::lround(w * s)), std::max(1u, (UINT)std::lround(h * s)), WICBitmapInterpolationModeHighQualityCubic)))
        return src;
    ComPtr<IWICBitmap> out;
    if (FAILED(wic->CreateBitmapFromSource(sc.Get(), WICBitmapCacheOnLoad, &out))) return src;
    return out;
}

ComPtr<IWICBitmapSource> FromHIcon(HICON h) {
    if (!h) return nullptr;
    ICONINFO ii{};
    if (GetIconInfo(h, &ii)) {
        ComPtr<IWICBitmapSource> r;
        if (ii.hbmColor) {
            BITMAP bm{};
            GetObject(ii.hbmColor, sizeof(bm), &bm);
            if (bm.bmBitsPixel == 32) r = FromHBitmap(ii.hbmColor);
        }
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        if (r) return r;
    }
    ComPtr<IWICBitmap> b;
    if (FAILED(wic->CreateBitmapFromHICON(h, &b))) return nullptr;
    ComPtr<IWICFormatConverter> cv;
    wic->CreateFormatConverter(&cv);
    if (FAILED(cv->Initialize(b.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return nullptr;
    ComPtr<IWICBitmap> out;
    wic->CreateBitmapFromSource(cv.Get(), WICBitmapCacheOnLoad, &out);
    return out;
}

ComPtr<IWICBitmapSource> LoadImageFile(const std::wstring& path, int maxW, int maxH) {
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) return nullptr;
    ComPtr<IWICBitmapFrameDecode> fr;
    if (FAILED(dec->GetFrame(0, &fr))) return nullptr;
    UINT w = 0, h = 0;
    fr->GetSize(&w, &h);
    ComPtr<IWICBitmapSource> src = fr;
    float s = std::min(1.f, std::max((float)maxW / w, (float)maxH / h));  // cobre a área pedida
    if (s < 1.f) {
        ComPtr<IWICBitmapScaler> sc;
        wic->CreateBitmapScaler(&sc);
        sc->Initialize(fr.Get(), std::max(1u, (UINT)(w * s)), std::max(1u, (UINT)(h * s)), WICBitmapInterpolationModeFant);
        src = sc;
    }
    ComPtr<IWICFormatConverter> cv;
    wic->CreateFormatConverter(&cv);
    if (FAILED(cv->Initialize(src.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return nullptr;
    ComPtr<IWICBitmap> out;
    wic->CreateBitmapFromSource(cv.Get(), WICBitmapCacheOnLoad, &out);
    return out;
}

void Fill(ID2D1DeviceContext* c, const D2D1_ROUNDED_RECT& rr, D2D1_COLOR_F col) { c->FillRoundedRectangle(rr, Brush(c, col).Get()); }
void FillRect(ID2D1DeviceContext* c, const D2D1_RECT_F& r, D2D1_COLOR_F col) { c->FillRectangle(r, Brush(c, col).Get()); }
void Stroke(ID2D1DeviceContext* c, const D2D1_ROUNDED_RECT& rr, D2D1_COLOR_F col, float w) { c->DrawRoundedRectangle(rr, Brush(c, col).Get(), w); }
void Circle(ID2D1DeviceContext* c, float x, float y, float r, D2D1_COLOR_F col) { c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), r, r), Brush(c, col).Get()); }
void Line(ID2D1DeviceContext* c, float x1, float y1, float x2, float y2, D2D1_COLOR_F col, float w, bool round) {
    static ComPtr<ID2D1StrokeStyle> rs;
    if (!rs) {
        D2D1_STROKE_STYLE_PROPERTIES p = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        d2d->CreateStrokeStyle(p, nullptr, 0, &rs);
    }
    c->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), Brush(c, col).Get(), w, round ? rs.Get() : nullptr);
}

static D2D1_ROUNDED_RECT Inset(const D2D1_ROUNDED_RECT& rr, float d) {
    return D2D1::RoundedRect(D2D1::RectF(rr.rect.left + d, rr.rect.top + d, rr.rect.right - d, rr.rect.bottom - d),
                             std::max(0.f, rr.radiusX - d), std::max(0.f, rr.radiusY - d));
}

ComPtr<ID2D1LinearGradientBrush> Gradient(ID2D1DeviceContext* c, D2D1_POINT_2F a, D2D1_POINT_2F b,
                                          std::initializer_list<D2D1_GRADIENT_STOP> stops) {
    std::vector<D2D1_GRADIENT_STOP> v(stops);
    ComPtr<ID2D1GradientStopCollection> col;
    c->CreateGradientStopCollection(v.data(), (UINT32)v.size(), &col);
    ComPtr<ID2D1LinearGradientBrush> br;
    c->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(a, b), col.Get(), &br);
    return br;
}

// Sombra suave fora do retângulo (anéis concêntricos: não escurece o vidro por dentro e é barata).
void Shadow(ID2D1DeviceContext* c, const D2D1_ROUNDED_RECT& rr, float size, float alpha, float dy) {
    const auto& R = rr.rect;
    int n = std::max(3, (int)(size / 1.5f));
    float step = size / n;
    auto br = Brush(c, Black(0));
    for (int k = 0; k < n; ++k) {
        float t = (k + 0.5f) / n;
        float a = alpha * 0.55f * (1 - t) * (1 - t);
        float d = step * (k + 0.5f);
        float off = dy * (0.3f + 0.7f * t);
        br->SetColor(Black(a));
        D2D1_ROUNDED_RECT e = D2D1::RoundedRect(D2D1::RectF(R.left - d, R.top - d + off * 0.4f, R.right + d, R.bottom + d + off),
                                                rr.radiusX + d, rr.radiusY + d);
        c->DrawRoundedRectangle(e, br.Get(), step);
    }
}

ComPtr<ID2D1RadialGradientBrush> Radial(ID2D1DeviceContext* c, D2D1_POINT_2F center, float rx, float ry,
                                        std::initializer_list<D2D1_GRADIENT_STOP> stops) {
    std::vector<D2D1_GRADIENT_STOP> v(stops);
    ComPtr<ID2D1GradientStopCollection> col;
    c->CreateGradientStopCollection(v.data(), (UINT32)v.size(), &col);
    ComPtr<ID2D1RadialGradientBrush> br;
    c->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(center, D2D1::Point2F(0, 0), rx, ry), col.Get(), &br);
    return br;
}

void Glyph(ID2D1DeviceContext* c, const wchar_t* g, float px, D2D1_RECT_F r, D2D1_COLOR_F col) {
    Text(c, g, Font(px, DWRITE_FONT_WEIGHT_REGULAR, L"Segoe MDL2 Assets"), r, col, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
}

// "Liquid glass": o desfoque vem do DWM; aqui desenhamos o que dá a sensação de lente/vidro espesso:
// brilho difuso vindo de cima, faixa de refração nas bordas com leve dispersão de cor, contorno especular
// em diagonal, reflexo no topo e uma sombra interna fininha embaixo.
void GlassOverlay(ID2D1DeviceContext* c, const D2D1_ROUNDED_RECT& rr, bool dark, float s) {
    s *= s_glass;
    if (s_reduce) s *= 0.45f;
    const auto& R = rr.rect;
    float w = R.right - R.left, h = R.bottom - R.top;
    if (w < 2 || h < 2) return;
    // 1) brilho difuso: luz vindo de cima à esquerda
    auto glow = Radial(c, D2D1::Point2F(R.left + w * 0.28f, R.top - h * 0.1f), w * 0.75f, h * 1.1f,
                       { { 0.f, White(0.16f * s) }, { 0.55f, White(0.04f * s) }, { 1.f, White(0.f) } });
    c->FillRoundedRectangle(rr, glow.Get());
    auto sheen = Gradient(c, D2D1::Point2F(0, R.top), D2D1::Point2F(0, R.bottom),
                          { { 0.f, White(0.09f * s) }, { 0.45f, White(0.02f * s) }, { 1.f, White(0.06f * s) } });
    c->FillRoundedRectangle(rr, sheen.Get());
    // 2) faixa de refração: anéis internos que somem para o centro (espessura da lente)
    int n = std::max(4, (int)(std::min(w, h) * 0.12f));
    n = std::min(n, 18);
    auto ring = Brush(c, White(0));
    for (int k = 0; k < n; ++k) {
        float t = (float)k / n;
        float a = (dark ? 0.08f : 0.11f) * s * (1 - t) * (1 - t);
        ring->SetColor(White(a));
        c->DrawRoundedRectangle(Inset(rr, 1.0f + k), ring.Get(), 1.f);
    }
    // 3) dispersão de cor sutil (ciano em cima à esquerda, rosa embaixo à direita), como a borda de uma lente
    auto disp = Gradient(c, D2D1::Point2F(R.left, R.top), D2D1::Point2F(R.right, R.bottom),
                         { { 0.f, Rgba(150, 225, 255, 0.30f * s) }, { 0.35f, Rgba(255, 255, 255, 0.f) },
                           { 0.65f, Rgba(255, 255, 255, 0.f) }, { 1.f, Rgba(255, 170, 225, 0.24f * s) } });
    c->DrawRoundedRectangle(Inset(rr, 2.0f), disp.Get(), 1.6f);
    // 4) contorno especular em diagonal
    float m = dark ? 0.85f : 1.f;
    auto rim = Gradient(c, D2D1::Point2F(R.left, R.top), D2D1::Point2F(R.right, R.bottom),
                        { { 0.f, White(0.85f * s * m) }, { 0.2f, White(0.28f * s * m) }, { 0.55f, White(0.09f * s * m) },
                          { 0.82f, White(0.24f * s * m) }, { 1.f, White(0.60f * s * m) } });
    c->DrawRoundedRectangle(Inset(rr, 0.6f), rim.Get(), 1.2f);
    // 5) reflexo especular no topo
    auto top = Gradient(c, D2D1::Point2F(0, R.top), D2D1::Point2F(0, R.top + std::min(h * 0.45f, 26.f)),
                        { { 0.f, White(0.55f * s * m) }, { 1.f, White(0.f) } });
    c->DrawRoundedRectangle(Inset(rr, 1.6f), top.Get(), 1.f);
    // 6) sombra interna embaixo (dá volume) e contorno externo finíssimo
    auto bot = Gradient(c, D2D1::Point2F(0, R.bottom - std::min(h * 0.4f, 22.f)), D2D1::Point2F(0, R.bottom),
                        { { 0.f, Black(0.f) }, { 1.f, Black((dark ? 0.16f : 0.07f) * std::min(1.f, s)) } });
    c->DrawRoundedRectangle(Inset(rr, 1.2f), bot.Get(), 1.f);
    Stroke(c, Inset(rr, -0.4f), Black(dark ? 0.30f : 0.11f), 0.8f);
}

ComPtr<IWICBitmap> Offscreen(int w, int h, const std::function<void(ID2D1DeviceContext*)>& draw) {
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmap(std::max(1, w), std::max(1, h), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return nullptr;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) return nullptr;
    ComPtr<ID2D1DeviceContext> dc;
    rt.As(&dc);
    if (!dc) return nullptr;
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    draw(dc.Get());
    dc->EndDraw();
    return bmp;
}

bool SaveImage(IWICBitmapSource* b, const std::wstring& path) {
    if (!b) return false;
    bool jpg = Lower(path).find(L".jpg") != std::wstring::npos;
    ComPtr<IWICStream> st;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> fr;
    ComPtr<IPropertyBag2> bag;
    if (FAILED(wic->CreateStream(&st)) || FAILED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    if (FAILED(wic->CreateEncoder(jpg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng, nullptr, &enc))) return false;
    if (FAILED(enc->Initialize(st.Get(), WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(&fr, &bag))) return false;
    if (jpg && bag) {
        PROPBAG2 opt{};
        opt.pstrName = (LPOLESTR)L"ImageQuality";
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_R4;
        v.fltVal = 0.93f;
        bag->Write(1, &opt, &v);
    }
    if (FAILED(fr->Initialize(bag.Get()))) return false;
    UINT w = 0, h = 0;
    b->GetSize(&w, &h);
    fr->SetSize(w, h);
    WICPixelFormatGUID fmt = jpg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppPBGRA;
    fr->SetPixelFormat(&fmt);
    ComPtr<IWICBitmapSource> src = b;
    if (jpg) {
        ComPtr<IWICFormatConverter> cv;
        wic->CreateFormatConverter(&cv);
        if (FAILED(cv->Initialize(b, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
        src = cv;
    }
    if (FAILED(fr->WriteSource(src.Get(), nullptr)) || FAILED(fr->Commit()) || FAILED(enc->Commit())) return false;
    return true;
}
}  // namespace gfx

// ---------------- Surface ----------------
bool Surface::Ensure(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (!rt_) {
        auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                  D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(gfx::d2d->CreateDCRenderTarget(&props, &rt_))) return false;
        rt_.As(&dc_);
        if (!dc_) { rt_.Reset(); return false; }
        dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        cache_.clear();
        keep_.clear();
    }
    if (w != w_ || h != h_ || !mem_) {
        if (mem_) {
            SelectObject(mem_, old_);
            DeleteObject(dib_);
            DeleteDC(mem_);
        }
        HDC scr = GetDC(nullptr);
        mem_ = CreateCompatibleDC(scr);
        ReleaseDC(nullptr, scr);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        dib_ = CreateDIBSection(mem_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        bits_ = bits;
        old_ = SelectObject(mem_, dib_);
        w_ = w;
        h_ = h;
    }
    RECT rc{ 0, 0, w, h };
    return SUCCEEDED(rt_->BindDC(mem_, &rc));
}

ID2D1DeviceContext* Surface::Begin(int w, int h) {
    if (!Ensure(w, h)) return nullptr;
    dc_->BeginDraw();
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    dc_->Clear(D2D1::ColorF(0, 0, 0, 0));
    return dc_.Get();
}

void Surface::End(int x, int y, BYTE alpha) {
    HRESULT hr = dc_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        Reset();
        return;
    }
    SetAlpha(x, y, alpha);
    if (!gfx::s_dumpDir.empty() && bits_) {
        wchar_t cls[64] = {}, title[64] = {};
        GetClassNameW(hwnd_, cls, 64);
        GetWindowTextW(hwnd_, title, 64);
        std::wstring name = gfx::s_dumpDir + L"\\" + cls + L"_" + title + L"_" + std::to_wstring(x) + L"_" + std::to_wstring(y) + L".png";
        for (size_t i = gfx::s_dumpDir.size() + 1; i < name.size(); ++i)
            if (name[i] == L' ' || name[i] == L':') name[i] = L'_';
        ComPtr<IWICBitmap> b;
        ComPtr<IWICStream> st;
        ComPtr<IWICBitmapEncoder> enc;
        ComPtr<IWICBitmapFrameEncode> fr;
        if (SUCCEEDED(gfx::wic->CreateBitmapFromMemory(w_, h_, GUID_WICPixelFormat32bppPBGRA, w_ * 4, w_ * h_ * 4, (BYTE*)bits_, &b)) &&
            SUCCEEDED(gfx::wic->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(name.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(gfx::wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(st.Get(), WICBitmapEncoderNoCache)) &&
            SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) && SUCCEEDED(fr->Initialize(nullptr))) {
            fr->WriteSource(b.Get(), nullptr);
            fr->Commit();
            enc->Commit();
        }
    }
}

bool Surface::SavePng(const std::wstring& path) {
    if (!bits_ || w_ <= 0) return false;
    ComPtr<IWICBitmap> b;
    if (FAILED(gfx::wic->CreateBitmapFromMemory(w_, h_, GUID_WICPixelFormat32bppPBGRA, w_ * 4, w_ * h_ * 4, (BYTE*)bits_, &b))) return false;
    return gfx::SaveImage(b.Get(), path);
}

void Surface::SetAlpha(int x, int y, BYTE alpha) {
    if (!mem_) return;
    POINT dst{ x, y }, src{ 0, 0 };
    SIZE sz{ w_, h_ };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd_, nullptr, &dst, &sz, mem_, &src, 0, &bf, ULW_ALPHA);
}

ID2D1Bitmap* Surface::Bitmap(IWICBitmapSource* src) {
    if (!src || !dc_) return nullptr;
    auto it = cache_.find(src);
    if (it != cache_.end()) return it->second.Get();
    if (cache_.size() > 400) { cache_.clear(); keep_.clear(); }
    ComPtr<ID2D1Bitmap> b;
    if (FAILED(dc_->CreateBitmapFromWicBitmap(src, nullptr, &b))) return nullptr;
    cache_[src] = b;
    keep_.push_back(src);
    return b.Get();
}

void Surface::DrawBitmap(IWICBitmapSource* src, D2D1_RECT_F dst, float opacity, bool hq) {
    ID2D1Bitmap* b = Bitmap(src);
    if (!b) return;
    dc_->DrawBitmap(b, &dst, opacity, hq ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC : D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
}

void Surface::Reset() {
    cache_.clear();
    keep_.clear();
    dc_.Reset();
    rt_.Reset();
    if (mem_) {
        SelectObject(mem_, old_);
        DeleteObject(dib_);
        DeleteDC(mem_);
        mem_ = nullptr;
        dib_ = nullptr;
    }
    w_ = h_ = 0;
}
