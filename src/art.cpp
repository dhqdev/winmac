#include "art.h"

namespace art {
using namespace gfx;
namespace {
const float kPi = 3.14159265f;

// Gotinha em coordenadas unitárias: ponta em (0,-1), base redonda com centro (0,0.12) e raio 0.66.
ID2D1PathGeometry* DropPath() {
    static ComPtr<ID2D1PathGeometry> g;
    if (g) return g.Get();
    d2d->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> k;
    g->Open(&k);
    k->BeginFigure(D2D1::Point2F(0, -1.f), D2D1_FIGURE_BEGIN_FILLED);
    k->AddBezier(D2D1::BezierSegment(D2D1::Point2F(0.22f, -0.72f), D2D1::Point2F(0.66f, -0.40f), D2D1::Point2F(0.66f, 0.12f)));
    k->AddArc(D2D1::ArcSegment(D2D1::Point2F(0, 0.78f), D2D1::SizeF(0.66f, 0.66f), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    k->AddArc(D2D1::ArcSegment(D2D1::Point2F(-0.66f, 0.12f), D2D1::SizeF(0.66f, 0.66f), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    k->AddBezier(D2D1::BezierSegment(D2D1::Point2F(-0.66f, -0.40f), D2D1::Point2F(-0.22f, -0.72f), D2D1::Point2F(0, -1.f)));
    k->EndFigure(D2D1_FIGURE_END_CLOSED);
    k->Close();
    return g.Get();
}

ComPtr<ID2D1StrokeStyle> RoundStroke() {
    static ComPtr<ID2D1StrokeStyle> rs;
    if (!rs) d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND), nullptr, 0, &rs);
    return rs;
}

void Curve(ID2D1DeviceContext* c, D2D1_POINT_2F a, D2D1_POINT_2F ctl, D2D1_POINT_2F b, D2D1_COLOR_F col, float w, bool fill = false) {
    ComPtr<ID2D1PathGeometry> g;
    d2d->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> k;
    g->Open(&k);
    k->BeginFigure(a, fill ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    k->AddQuadraticBezier(D2D1::QuadraticBezierSegment(ctl, b));
    k->EndFigure(fill ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    k->Close();
    auto br = Brush(c, col);
    if (fill) c->FillGeometry(g.Get(), br.Get());
    else c->DrawGeometry(g.Get(), br.Get(), w, RoundStroke().Get());
}

void Ellipse(ID2D1DeviceContext* c, float x, float y, float rx, float ry, D2D1_COLOR_F col) {
    c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), rx, ry), Brush(c, col).Get());
}

void Arm(ID2D1DeviceContext* c, const D2D1_MATRIX_3X2_F& base, float sx, float sy, float angleDeg) {
    c->SetTransform(D2D1::Matrix3x2F::Rotation(angleDeg) * D2D1::Matrix3x2F::Translation(sx, sy) * base);
    auto rr = D2D1::Ellipse(D2D1::Point2F(0.10f, 0), 0.15f, 0.095f);
    auto br = Gradient(c, D2D1::Point2F(0, -0.1f), D2D1::Point2F(0, 0.1f), { { 0.f, Rgba(130, 205, 255) }, { 1.f, Rgba(80, 140, 250) } });
    c->FillEllipse(rr, br.Get());
    c->DrawEllipse(rr, Brush(c, Rgba(40, 60, 150, 0.35f)).Get(), 0.02f);
    c->SetTransform(base);
}
}  // namespace

void Pingo(ID2D1DeviceContext* c, float cx, float cy, float size, const Pose& p) {
    D2D1_MATRIX_3X2_F old;
    c->GetTransform(&old);
    float u = size / 1.8f;
    float breath = std::sin(p.t * 2.2f);
    float hop = p.hop;
    // sombra no chão (não pula junto)
    D2D1_MATRIX_3X2_F ground = D2D1::Matrix3x2F::Scale(u, u) * D2D1::Matrix3x2F::Translation(cx, cy + 0.11f * u) * old;
    if (p.shadow) {
        c->SetTransform(ground);
        Ellipse(c, 0, 0.86f, 0.46f * (1 - hop * 0.35f), 0.06f, Black(0.16f * (1 - hop * 0.5f)));
    }
    // respira (estica e achata a partir da base) e pula
    float sy = 1 + 0.025f * breath, sx = 1 - 0.02f * breath;
    D2D1_MATRIX_3X2_F M = D2D1::Matrix3x2F::Translation(0, -0.78f) * D2D1::Matrix3x2F::Scale(sx, sy) * D2D1::Matrix3x2F::Translation(0, 0.78f - hop * 0.45f) * ground;
    c->SetTransform(M);
    // mãozinhas (atrás do corpo, aparecem dos lados; acenando, a direita vem para a frente)
    bool waving = p.arms && p.wave > 0.05f;
    if (p.arms) {
        Arm(c, M, -0.58f, 0.30f, 180 - 28);
        if (!waving) Arm(c, M, 0.58f, 0.30f, 28);
    }
    auto* drop = DropPath();
    // corpo de vidro líquido
    auto body = Gradient(c, D2D1::Point2F(0, -1), D2D1::Point2F(0, 0.78f),
                         { { 0.f, Rgba(160, 235, 255) }, { 0.45f, Rgba(72, 162, 255) }, { 1.f, Rgba(122, 96, 246) } });
    c->FillGeometry(drop, body.Get());
    auto glow = Radial(c, D2D1::Point2F(-0.18f, -0.12f), 0.95f, 0.95f, { { 0.f, White(0.30f) }, { 0.6f, White(0.06f) }, { 1.f, White(0) } });
    c->FillGeometry(drop, glow.Get());
    auto refl = Radial(c, D2D1::Point2F(0, 0.64f), 0.44f, 0.14f, { { 0.f, Rgba(215, 250, 255, 0.55f) }, { 1.f, White(0) } });
    c->FillEllipse(D2D1::Ellipse(D2D1::Point2F(0, 0.60f), 0.42f, 0.13f), refl.Get());
    c->DrawGeometry(drop, Brush(c, Rgba(36, 54, 150, 0.38f)).Get(), 0.028f);
    auto rim = Gradient(c, D2D1::Point2F(-0.6f, -0.9f), D2D1::Point2F(0.6f, 0.8f),
                        { { 0.f, White(0.95f) }, { 0.45f, White(0.25f) }, { 1.f, White(0.55f) } });
    c->DrawGeometry(drop, rim.Get(), 0.022f);
    // brilhos especulares
    c->SetTransform(D2D1::Matrix3x2F::Rotation(28) * D2D1::Matrix3x2F::Translation(-0.31f, -0.30f) * M);
    Ellipse(c, 0, 0, 0.075f, 0.18f, White(0.85f));
    c->SetTransform(M);
    // rostinho
    D2D1_COLOR_F ink = Rgba(28, 32, 74);
    float ph = std::fmod(p.t + 0.7f, 3.7f);
    float blink = ph < 0.16f ? std::max(0.08f, std::fabs(ph / 0.16f * 2 - 1)) : 1.f;
    float ex = 0.235f, ey = 0.12f;
    auto openEye = [&](float x, float bigger) {
        float ry = 0.14f * bigger * blink;
        Ellipse(c, x, ey, 0.105f * bigger, ry, ink);
        if (blink > 0.5f) {
            Ellipse(c, x + 0.036f, ey - 0.05f * bigger, 0.042f * bigger, 0.042f * bigger, White(0.95f));
            Ellipse(c, x - 0.03f, ey + 0.045f * bigger, 0.018f * bigger, 0.018f * bigger, White(0.8f));
        }
    };
    auto happyEye = [&](float x) { Curve(c, D2D1::Point2F(x - 0.09f, ey + 0.03f), D2D1::Point2F(x, ey - 0.09f), D2D1::Point2F(x + 0.09f, ey + 0.03f), ink, 0.042f); };
    auto sleepyEye = [&](float x) { Curve(c, D2D1::Point2F(x - 0.09f, ey), D2D1::Point2F(x, ey + 0.08f), D2D1::Point2F(x + 0.09f, ey), ink, 0.038f); };
    switch (p.face) {
    case Face::Wink: openEye(-ex, 1.f); happyEye(ex); break;
    case Face::Excited: openEye(-ex, 1.12f); openEye(ex, 1.12f); break;
    case Face::Sleepy: sleepyEye(-ex); sleepyEye(ex); break;
    default: openEye(-ex, 1.f); openEye(ex, 1.f); break;
    }
    Ellipse(c, -0.42f, 0.31f, 0.10f, 0.055f, Rgba(255, 110, 170, 0.50f));
    Ellipse(c, 0.42f, 0.31f, 0.10f, 0.055f, Rgba(255, 110, 170, 0.50f));
    if (p.face == Face::Excited) {
        Curve(c, D2D1::Point2F(-0.11f, 0.27f), D2D1::Point2F(0, 0.50f), D2D1::Point2F(0.11f, 0.27f), ink, 0, true);
        Ellipse(c, 0, 0.365f, 0.05f, 0.028f, Rgba(255, 120, 150));
    } else {
        Curve(c, D2D1::Point2F(-0.085f, 0.29f), D2D1::Point2F(0, 0.39f), D2D1::Point2F(0.085f, 0.29f), ink, 0.036f);
    }
    if (waving) {
        // mãozinha acenando: sai da lateral do corpo e balança
        float ang = 10 - 70 * p.wave + std::sin(p.t * 9) * 20 * p.wave;
        c->SetTransform(D2D1::Matrix3x2F::Rotation(ang) * D2D1::Matrix3x2F::Translation(0.52f, 0.24f) * M);
        auto rr = D2D1::Ellipse(D2D1::Point2F(0.13f, 0), 0.15f, 0.088f);
        auto br = Gradient(c, D2D1::Point2F(0, -0.09f), D2D1::Point2F(0, 0.09f), { { 0.f, Rgba(120, 200, 255) }, { 1.f, Rgba(84, 140, 250) } });
        c->FillEllipse(rr, br.Get());
        c->DrawEllipse(rr, Brush(c, Rgba(40, 60, 150, 0.22f)).Get(), 0.016f);
    }
    c->SetTransform(old);
}

void Logo(ID2D1DeviceContext* c, float cx, float cy, float size, D2D1_COLOR_F col) {
    static ComPtr<ID2D1PathGeometry> g;
    if (!g) {
        ComPtr<ID2D1EllipseGeometry> e1, e2;
        d2d->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(-0.24f, 0.14f), 0.12f, 0.16f), &e1);
        d2d->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(0.24f, 0.14f), 0.12f, 0.16f), &e2);
        ID2D1Geometry* eyes[2] = { e1.Get(), e2.Get() };
        ComPtr<ID2D1GeometryGroup> grp;
        d2d->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, eyes, 2, &grp);
        d2d->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> k;
        g->Open(&k);
        DropPath()->CombineWithGeometry(grp.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, k.Get());
        k->Close();
    }
    D2D1_MATRIX_3X2_F old;
    c->GetTransform(&old);
    float u = size / 1.8f;
    c->SetTransform(D2D1::Matrix3x2F::Scale(u, u) * D2D1::Matrix3x2F::Translation(cx, cy + 0.11f * u) * old);
    c->FillGeometry(g.Get(), Brush(c, col).Get());
    c->SetTransform(old);
}

static D2D1_ROUNDED_RECT Squircle(D2D1_RECT_F r) {
    float w = r.right - r.left;
    return RR(r.left, r.top, r.right, r.bottom, w * 0.2237f);
}

void AppIcon(ID2D1DeviceContext* c, D2D1_RECT_F r) {
    float w = r.right - r.left, h = r.bottom - r.top;
    auto sq = Squircle(r);
    auto bg = Gradient(c, D2D1::Point2F(r.left, r.top), D2D1::Point2F(r.right, r.bottom),
                       { { 0.f, Rgba(236, 246, 255) }, { 0.55f, Rgba(205, 222, 255) }, { 1.f, Rgba(222, 205, 255) } });
    c->FillRoundedRectangle(sq, bg.Get());
    // manchas de cor (aurora) dentro do ícone
    ComPtr<ID2D1RoundedRectangleGeometry> clip;
    d2d->CreateRoundedRectangleGeometry(sq, &clip);
    c->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
    auto b1 = Radial(c, D2D1::Point2F(r.left + w * 0.15f, r.top + h * 0.1f), w * 0.55f, w * 0.55f, { { 0.f, Rgba(120, 220, 255, 0.55f) }, { 1.f, Rgba(120, 220, 255, 0) } });
    c->FillRectangle(r, b1.Get());
    auto b2 = Radial(c, D2D1::Point2F(r.right - w * 0.1f, r.bottom - h * 0.12f), w * 0.6f, w * 0.6f, { { 0.f, Rgba(255, 140, 210, 0.45f) }, { 1.f, Rgba(255, 140, 210, 0) } });
    c->FillRectangle(r, b2.Get());
    c->PopLayer();
    if (w >= 40) GlassOverlay(c, sq, false, 0.9f);
    else Stroke(c, sq, Black(0.18f), 1.f);
    Pose p;
    p.t = 0.9f;
    p.arms = w >= 48;
    p.shadow = w >= 32;
    Pingo(c, (r.left + r.right) / 2, (r.top + r.bottom) / 2 + h * 0.03f, h * 0.70f, p);
}

void SettingsIcon(ID2D1DeviceContext* c, D2D1_RECT_F r) {
    float w = r.right - r.left;
    float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    auto sq = Squircle(r);
    auto bg = Gradient(c, D2D1::Point2F(0, r.top), D2D1::Point2F(0, r.bottom), { { 0.f, Rgba(160, 160, 166) }, { 1.f, Rgba(86, 86, 92) } });
    c->FillRoundedRectangle(sq, bg.Get());
    // engrenagem
    const int N = 12;
    float ro = w * 0.38f, ri = w * 0.31f, step = 2 * kPi / N;
    ComPtr<ID2D1PathGeometry> g;
    d2d->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> k;
    g->Open(&k);
    for (int i = 0; i < N; ++i) {
        float a = i * step;
        D2D1_POINT_2F pts[4] = {
            D2D1::Point2F(cx + ri * std::cos(a - step * 0.32f), cy + ri * std::sin(a - step * 0.32f)),
            D2D1::Point2F(cx + ro * std::cos(a - step * 0.17f), cy + ro * std::sin(a - step * 0.17f)),
            D2D1::Point2F(cx + ro * std::cos(a + step * 0.17f), cy + ro * std::sin(a + step * 0.17f)),
            D2D1::Point2F(cx + ri * std::cos(a + step * 0.32f), cy + ri * std::sin(a + step * 0.32f)),
        };
        if (i == 0) k->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
        else k->AddLine(pts[0]);
        k->AddLines(pts + 1, 3);
    }
    k->EndFigure(D2D1_FIGURE_END_CLOSED);
    k->Close();
    auto gear = Gradient(c, D2D1::Point2F(0, cy - ro), D2D1::Point2F(0, cy + ro), { { 0.f, Rgba(244, 244, 248) }, { 1.f, Rgba(176, 176, 184) } });
    c->FillGeometry(g.Get(), gear.Get());
    c->DrawGeometry(g.Get(), Brush(c, Black(0.25f)).Get(), std::max(0.6f, w * 0.012f));
    Circle(c, cx, cy, w * 0.20f, Rgba(70, 70, 76));
    Circle(c, cx, cy, w * 0.13f, Rgba(196, 196, 204));
    Circle(c, cx, cy, w * 0.065f, Rgba(90, 90, 96));
    if (w >= 40) GlassOverlay(c, sq, true, 0.6f);
    else Stroke(c, sq, Black(0.2f), 1.f);
}

void SectionIcon(ID2D1DeviceContext* c, D2D1_RECT_F r, D2D1_COLOR_F bg, const wchar_t* glyph) {
    float w = r.right - r.left;
    auto rr = RR(r.left, r.top, r.right, r.bottom, w * 0.24f);
    Fill(c, rr, bg);
    auto hi = Gradient(c, D2D1::Point2F(0, r.top), D2D1::Point2F(0, r.bottom), { { 0.f, White(0.22f) }, { 1.f, White(0) } });
    c->FillRoundedRectangle(rr, hi.Get());
    Glyph(c, glyph, w * 0.56f, r, White(1));
}

void WifiBars(ID2D1DeviceContext* c, float cx, float cy, float s, int quality, D2D1_COLOR_F col) {
    for (int i = 0; i < 3; ++i) {
        float r = (3.6f + i * 3.4f) * s;
        float a0 = -kPi * 0.78f, a1 = -kPi * 0.22f;
        ComPtr<ID2D1PathGeometry> g;
        d2d->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> k;
        g->Open(&k);
        k->BeginFigure(D2D1::Point2F(cx + r * std::cos(a0), cy + r * std::sin(a0)), D2D1_FIGURE_BEGIN_HOLLOW);
        k->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + r * std::cos(a1), cy + r * std::sin(a1)), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        k->EndFigure(D2D1_FIGURE_END_OPEN);
        k->Close();
        bool lit = quality >= (i == 0 ? 0 : i == 1 ? 34 : 67);
        D2D1_COLOR_F cc = col;
        if (!lit) cc.a *= 0.3f;
        c->DrawGeometry(g.Get(), Brush(c, cc).Get(), 1.9f * s, RoundStroke().Get());
    }
    Circle(c, cx, cy - 0.2f * s, 1.5f * s, col);
}

void Confetti(ID2D1DeviceContext* c, D2D1_RECT_F a, float t) {
    static const D2D1_COLOR_F cols[] = { Rgba(255, 95, 87), Rgba(254, 188, 46), Rgba(40, 200, 64), Rgba(10, 132, 255), Rgba(191, 90, 242), Rgba(255, 55, 140), Rgba(100, 210, 255) };
    float w = a.right - a.left, h = a.bottom - a.top;
    auto br = Brush(c, cols[0]);
    D2D1_MATRIX_3X2_F old;
    c->GetTransform(&old);
    for (int i = 0; i < 70; ++i) {
        unsigned hsh = (unsigned)i * 2654435761u;
        float r1 = (hsh & 0xFFFF) / 65535.f, r2 = ((hsh >> 16) & 0xFFFF) / 65535.f;
        float speed = 0.18f + 0.22f * r2;
        float y = std::fmod(r2 * 1.3f + t * speed, 1.25f) - 0.15f;
        float x = r1 + 0.03f * std::sin(t * 2.4f + i);
        float rot = t * (120 + 200 * r1) + i * 37;
        float sz = (5 + 5 * r2) * (h / 600.f);
        br->SetColor(cols[i % 7]);
        c->SetTransform(D2D1::Matrix3x2F::Rotation(rot) * D2D1::Matrix3x2F::Translation(a.left + x * w, a.top + y * h) * old);
        c->FillRoundedRectangle(RR(-sz / 2, -sz / 4, sz / 2, sz / 4, sz / 6), br.Get());
    }
    c->SetTransform(old);
}

// ---------------- papéis de parede ----------------
const std::vector<Wallpaper>& Wallpapers() {
    static std::vector<Wallpaper> v = { { L"Aurora", true }, { L"Lagoa", false }, { L"Pôr do Sol", false },
                                        { L"Floresta", true }, { L"Grafite", true }, { L"Algodão", false } };
    return v;
}

namespace {
struct Blob { float x, y, r; int cr, cg, cb; float a; };
struct Preset { int top[3], bottom[3]; std::vector<Blob> blobs; };
const Preset& PresetOf(int i) {
    static std::vector<Preset> ps = {
        { { 10, 14, 40 }, { 24, 10, 52 }, { { 0.15f, 0.25f, 0.55f, 40, 200, 220, 0.75f }, { 0.75f, 0.2f, 0.5f, 130, 80, 240, 0.70f }, { 0.55f, 0.85f, 0.6f, 240, 80, 170, 0.55f }, { 0.95f, 0.75f, 0.35f, 60, 120, 255, 0.6f }, { 0.35f, 0.55f, 0.3f, 120, 255, 220, 0.25f } } },
        { { 205, 232, 255 }, { 240, 248, 255 }, { { 0.2f, 0.3f, 0.5f, 120, 200, 255, 0.70f }, { 0.8f, 0.25f, 0.45f, 160, 230, 255, 0.65f }, { 0.6f, 0.9f, 0.6f, 120, 170, 255, 0.55f }, { 0.1f, 0.9f, 0.35f, 200, 240, 255, 0.6f } } },
        { { 255, 196, 140 }, { 120, 60, 150 }, { { 0.2f, 0.35f, 0.5f, 255, 150, 90, 0.75f }, { 0.8f, 0.3f, 0.45f, 255, 100, 140, 0.70f }, { 0.5f, 0.85f, 0.65f, 150, 70, 200, 0.70f }, { 0.95f, 0.85f, 0.3f, 255, 120, 200, 0.5f } } },
        { { 8, 28, 26 }, { 10, 40, 34 }, { { 0.2f, 0.3f, 0.5f, 30, 160, 120, 0.70f }, { 0.8f, 0.25f, 0.45f, 40, 190, 170, 0.6f }, { 0.55f, 0.9f, 0.6f, 20, 110, 90, 0.7f }, { 0.9f, 0.8f, 0.3f, 120, 220, 160, 0.35f } } },
        { { 24, 24, 28 }, { 14, 14, 18 }, { { 0.25f, 0.3f, 0.5f, 70, 80, 110, 0.65f }, { 0.8f, 0.3f, 0.45f, 60, 60, 70, 0.6f }, { 0.55f, 0.9f, 0.6f, 50, 70, 120, 0.55f } } },
        { { 255, 232, 240 }, { 236, 228, 255 }, { { 0.2f, 0.3f, 0.5f, 255, 190, 215, 0.7f }, { 0.8f, 0.25f, 0.45f, 210, 190, 255, 0.7f }, { 0.55f, 0.9f, 0.6f, 190, 220, 255, 0.6f }, { 0.05f, 0.85f, 0.35f, 255, 220, 200, 0.5f } } },
    };
    return ps[std::clamp(i, 0, (int)ps.size() - 1)];
}
}  // namespace

ComPtr<IWICBitmap> RenderWallpaper(int preset, int w, int h) {
    const Preset& p = PresetOf(preset);
    auto bmp = Offscreen(w, h, [&](ID2D1DeviceContext* c) {
        float W = (float)w, H = (float)h, M = std::max(W, H);
        auto base = Gradient(c, D2D1::Point2F(0, 0), D2D1::Point2F(W * 0.3f, H),
                             { { 0.f, Rgba(p.top[0], p.top[1], p.top[2]) }, { 1.f, Rgba(p.bottom[0], p.bottom[1], p.bottom[2]) } });
        c->FillRectangle(D2D1::RectF(0, 0, W, H), base.Get());
        for (auto& b : p.blobs) {
            auto br = Radial(c, D2D1::Point2F(b.x * W, b.y * H), b.r * M, b.r * M * 0.8f,
                             { { 0.f, Rgba(b.cr, b.cg, b.cb, b.a) }, { 0.55f, Rgba(b.cr, b.cg, b.cb, b.a * 0.35f) }, { 1.f, Rgba(b.cr, b.cg, b.cb, 0) } });
            c->FillRectangle(D2D1::RectF(0, 0, W, H), br.Get());
        }
        // faixa de "vidro" diagonal bem suave
        auto band = Gradient(c, D2D1::Point2F(W * 0.2f, 0), D2D1::Point2F(W * 0.8f, H),
                             { { 0.f, White(0) }, { 0.48f, White(0.06f) }, { 0.52f, White(0.09f) }, { 0.56f, White(0.03f) }, { 1.f, White(0) } });
        c->FillRectangle(D2D1::RectF(0, 0, W, H), band.Get());
    });
    if (!bmp) return nullptr;
    // pontilhado leve: evita "degraus" visíveis no degradê
    WICRect rc{ 0, 0, w, h };
    ComPtr<IWICBitmapLock> lk;
    if (SUCCEEDED(bmp->Lock(&rc, WICBitmapLockWrite, &lk))) {
        UINT sz = 0, stride = 0;
        BYTE* px = nullptr;
        lk->GetDataPointer(&sz, &px);
        lk->GetStride(&stride);
        unsigned seed = 12345;
        for (int y = 0; y < h; ++y) {
            BYTE* row = px + (size_t)y * stride;
            for (int x = 0; x < w; ++x) {
                seed = seed * 1664525u + 1013904223u;
                int n = (int)((seed >> 24) % 5) - 2;
                for (int k = 0; k < 3; ++k) row[x * 4 + k] = (BYTE)std::clamp(row[x * 4 + k] + n, 0, 255);
            }
        }
    }
    return bmp;
}

bool SaveWallpaper(int preset, int w, int h, const std::wstring& path) {
    auto b = RenderWallpaper(preset, w, h);
    return b && SaveImage(b.Get(), path);
}

void ExportArt(const std::wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
    for (int sz : { 256, 128, 64, 48, 40, 32, 24, 20, 16 }) {
        auto b = Offscreen(sz, sz, [&](ID2D1DeviceContext* c) { AppIcon(c, D2D1::RectF(0, 0, (float)sz, (float)sz)); });
        SaveImage(b.Get(), dir + L"\\icon_" + std::to_wstring(sz) + L".png");
    }
    struct F { const wchar_t* n; Face f; float wave; };
    for (auto& f : { F{ L"pingo", Face::Happy, 0 }, F{ L"pingo_wave", Face::Wink, 0.8f }, F{ L"pingo_excited", Face::Excited, 0 } }) {
        auto b = Offscreen(512, 512, [&](ID2D1DeviceContext* c) {
            Pose p;
            p.t = 0.9f;
            p.wave = f.wave;
            p.face = f.f;
            Pingo(c, 256, 256, 400, p);
        });
        SaveImage(b.Get(), dir + L"\\" + f.n + L".png");
    }
    for (int i = 0; i < (int)Wallpapers().size(); ++i) SaveWallpaper(i, 960, 540, dir + L"\\wall_" + std::to_wstring(i) + L".jpg");
}
}  // namespace art
