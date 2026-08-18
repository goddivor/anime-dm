#include "ui/IconFactory.h"

#include <objidl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

// GDI+ headers still reference the min/max macros that NOMINMAX removes.
using std::max;
using std::min;

#include <gdiplus.h>

namespace {

using Gdiplus::Color;
using Gdiplus::Graphics;
using Gdiplus::GraphicsPath;
using Gdiplus::Pen;
using Gdiplus::PointF;

constexpr int kToolbarSize = 24;
constexpr int kCategorySize = 16;
constexpr float kGrid = 24.0f;
constexpr float kTwoPi = 6.283185307f;
constexpr float kDegrees = 57.295779513f;

COLORREF g_stroke = RGB(0x00, 0x00, 0x00);

// Returns the monochrome stroke colour the current image list is drawn with.
Color StrokeColor() {
    return Color(0xFF, GetRValue(g_stroke), GetGValue(g_stroke), GetBValue(g_stroke));
}

// Applies the rounded caps and joins lucide draws its icons with.
void Round(Pen& pen) {
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
}

// Walks an SVG path string, skipping the separators between numbers.
struct Cursor {
    const char* at;

    void Skip() {
        while (*at == ' ' || *at == ',' || *at == '\t' || *at == '\n' || *at == '\r') {
            ++at;
        }
    }

    bool HasNumber() {
        Skip();
        return *at == '-' || *at == '+' || *at == '.' || (*at >= '0' && *at <= '9');
    }

    float Number() {
        Skip();
        char* end = nullptr;
        float value = std::strtof(at, &end);
        at = end;
        return value;
    }
};

// Appends the circular arc joining two points, as the SVG arc command defines
// it. Every arc lucide uses has equal radii and no rotation, so the centre
// follows from the endpoints alone.
void AddArcTo(GraphicsPath& path, PointF from, PointF to, float radius, bool largeArc,
              bool sweep) {
    float halfX = (from.X - to.X) / 2.0f;
    float halfY = (from.Y - to.Y) / 2.0f;
    float span = halfX * halfX + halfY * halfY;
    if (span <= 0.0f) {
        return;
    }

    float squared = radius * radius;
    if (squared < span) {
        radius = std::sqrt(span);
        squared = span;
    }

    float factor = std::sqrt(std::max(0.0f, squared / span - 1.0f));
    if (largeArc == sweep) {
        factor = -factor;
    }

    float centreX = (from.X + to.X) / 2.0f + factor * halfY;
    float centreY = (from.Y + to.Y) / 2.0f - factor * halfX;

    float start = std::atan2(from.Y - centreY, from.X - centreX);
    float end = std::atan2(to.Y - centreY, to.X - centreX);
    float sweepAngle = end - start;
    if (!sweep && sweepAngle > 0.0f) {
        sweepAngle -= kTwoPi;
    }
    if (sweep && sweepAngle < 0.0f) {
        sweepAngle += kTwoPi;
    }

    path.AddArc(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f,
                start * kDegrees, sweepAngle * kDegrees);
}

// Builds a path from the SVG subset lucide relies on: move, line, horizontal,
// vertical, elliptical arc and close.
void BuildPath(GraphicsPath& path, const char* data) {
    Cursor cursor{data};
    PointF current(0.0f, 0.0f);
    PointF origin(0.0f, 0.0f);
    char command = 0;

    while (true) {
        cursor.Skip();
        if (*cursor.at == '\0') {
            return;
        }
        if ((*cursor.at >= 'A' && *cursor.at <= 'Z') || (*cursor.at >= 'a' && *cursor.at <= 'z')) {
            command = *cursor.at;
            ++cursor.at;
        }

        bool relative = command >= 'a';
        char kind = relative ? static_cast<char>(command - 32) : command;

        if (kind == 'Z') {
            path.CloseFigure();
            current = origin;
            continue;
        }
        if (!cursor.HasNumber()) {
            return;
        }

        switch (kind) {
        case 'M': {
            float x = cursor.Number();
            float y = cursor.Number();
            current = relative ? PointF(current.X + x, current.Y + y) : PointF(x, y);
            origin = current;
            path.StartFigure();
            command = relative ? 'l' : 'L';
            break;
        }
        case 'L': {
            float x = cursor.Number();
            float y = cursor.Number();
            PointF next = relative ? PointF(current.X + x, current.Y + y) : PointF(x, y);
            path.AddLine(current, next);
            current = next;
            break;
        }
        case 'H': {
            float x = cursor.Number();
            PointF next = relative ? PointF(current.X + x, current.Y) : PointF(x, current.Y);
            path.AddLine(current, next);
            current = next;
            break;
        }
        case 'V': {
            float y = cursor.Number();
            PointF next = relative ? PointF(current.X, current.Y + y) : PointF(current.X, y);
            path.AddLine(current, next);
            current = next;
            break;
        }
        case 'A': {
            float radius = cursor.Number();
            cursor.Number();
            cursor.Number();
            bool largeArc = cursor.Number() != 0.0f;
            bool sweep = cursor.Number() != 0.0f;
            float x = cursor.Number();
            float y = cursor.Number();
            PointF next = relative ? PointF(current.X + x, current.Y + y) : PointF(x, y);
            AddArcTo(path, current, next, radius, largeArc, sweep);
            current = next;
            break;
        }
        default:
            return;
        }
    }
}

enum class ShapeKind { Path, Circle, RoundRect, Line, Polyline };

struct Shape {
    ShapeKind kind;
    const char* data;
    float values[5];
};

// Appends a rounded rectangle, corner by corner.
void AddRoundRect(GraphicsPath& path, float x, float y, float width, float height, float radius) {
    float diameter = radius * 2.0f;
    path.AddArc(x, y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(x + width - diameter, y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(x, y + height - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

// Appends the open polyline described by a list of coordinates.
void AddPolyline(GraphicsPath& path, const char* data) {
    Cursor cursor{data};
    PointF previous(0.0f, 0.0f);
    bool started = false;
    while (cursor.HasNumber()) {
        float x = cursor.Number();
        float y = cursor.Number();
        PointF point(x, y);
        if (started) {
            path.AddLine(previous, point);
        } else {
            path.StartFigure();
            started = true;
        }
        previous = point;
    }
}

// Strokes every shape of an icon on the 24-unit lucide grid.
void DrawShapes(Graphics& graphics, const Shape* shapes, int count) {
    Pen pen(StrokeColor(), 2.0f);
    Round(pen);

    for (int index = 0; index < count; ++index) {
        const Shape& shape = shapes[index];
        GraphicsPath path;
        switch (shape.kind) {
        case ShapeKind::Path:
            BuildPath(path, shape.data);
            break;
        case ShapeKind::Circle:
            path.AddEllipse(shape.values[0] - shape.values[2], shape.values[1] - shape.values[2],
                            shape.values[2] * 2.0f, shape.values[2] * 2.0f);
            break;
        case ShapeKind::RoundRect:
            AddRoundRect(path, shape.values[0], shape.values[1], shape.values[2], shape.values[3],
                         shape.values[4]);
            break;
        case ShapeKind::Line:
            path.AddLine(shape.values[0], shape.values[1], shape.values[2], shape.values[3]);
            break;
        case ShapeKind::Polyline:
            AddPolyline(path, shape.data);
            break;
        }
        graphics.DrawPath(&pen, &path);
    }
}

#define ADM_PATH(d) {ShapeKind::Path, d, {0, 0, 0, 0, 0}}
#define ADM_CIRCLE(cx, cy, r) {ShapeKind::Circle, nullptr, {cx, cy, r, 0, 0}}
#define ADM_RRECT(x, y, w, h, r) {ShapeKind::RoundRect, nullptr, {x, y, w, h, r}}
#define ADM_LINE(x1, y1, x2, y2) {ShapeKind::Line, nullptr, {x1, y1, x2, y2, 0}}
#define ADM_POLY(p) {ShapeKind::Polyline, p, {0, 0, 0, 0, 0}}

// Icon geometry copied verbatim from the lucide set the React shell uses.
const Shape kPlus[] = {ADM_PATH("M5 12h14"), ADM_PATH("M12 5v14")};

const Shape kPlay[] = {ADM_PATH(
    "M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z")};

const Shape kCircleStop[] = {ADM_CIRCLE(12, 12, 10), ADM_RRECT(9, 9, 6, 6, 1)};

const Shape kOctagonX[] = {
    ADM_PATH("m15 9-6 6"),
    ADM_PATH("M2.586 16.726A2 2 0 0 1 2 15.312V8.688a2 2 0 0 1 .586-1.414l4.688-4.688A2 2 0 0 1 "
             "8.688 2h6.624a2 2 0 0 1 1.414.586l4.688 4.688A2 2 0 0 1 22 8.688v6.624a2 2 0 0 "
             "1-.586 1.414l-4.688 4.688a2 2 0 0 1-1.414.586H8.688a2 2 0 0 1-1.414-.586z"),
    ADM_PATH("m9 9 6 6"),
};

const Shape kTrash[] = {
    ADM_PATH("M10 11v6"),
    ADM_PATH("M14 11v6"),
    ADM_PATH("M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"),
    ADM_PATH("M3 6h18"),
    ADM_PATH("M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"),
};

const Shape kListX[] = {
    ADM_PATH("M16 5H3"),        ADM_PATH("M11 12H3"),      ADM_PATH("M16 19H3"),
    ADM_PATH("m15.5 9.5 5 5"),  ADM_PATH("m20.5 9.5-5 5"),
};

const Shape kSettings[] = {
    ADM_PATH("M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 "
             "2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 "
             "1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 "
             "2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915"),
    ADM_CIRCLE(12, 12, 3),
};

const Shape kTimer[] = {ADM_LINE(10, 2, 14, 2), ADM_LINE(12, 14, 15, 11), ADM_CIRCLE(12, 14, 8)};

const Shape kPuzzle[] = {ADM_PATH(
    "M15.39 4.39a1 1 0 0 0 1.68-.474 2.5 2.5 0 1 1 3.014 3.015 1 1 0 0 0-.474 1.68l1.683 "
    "1.682a2.414 2.414 0 0 1 0 3.414L19.61 15.39a1 1 0 0 1-1.68-.474 2.5 2.5 0 1 0-3.014 3.015 1 "
    "1 0 0 1 .474 1.68l-1.683 1.682a2.414 2.414 0 0 1-3.414 0L8.61 19.61a1 1 0 0 0-1.68.474 2.5 "
    "2.5 0 1 1-3.014-3.015 1 1 0 0 0 .474-1.68l-1.683-1.682a2.414 2.414 0 0 1 0-3.414L4.39 8.61a1 "
    "1 0 0 1 1.68.474 2.5 2.5 0 1 0 3.014-3.015 1 1 0 0 1-.474-1.68l1.683-1.682a2.414 2.414 0 0 1 "
    "3.414 0z")};

const Shape kSearch[] = {ADM_PATH("m21 21-4.34-4.34"), ADM_CIRCLE(11, 11, 8)};

const Shape kFolder[] = {ADM_PATH(
    "M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 "
    "0 0-2 2v13a2 2 0 0 0 2 2Z")};

const Shape kFilm[] = {
    ADM_RRECT(3, 3, 18, 18, 2), ADM_PATH("M7 3v18"),  ADM_PATH("M3 7.5h4"),
    ADM_PATH("M3 12h18"),       ADM_PATH("M3 16.5h4"), ADM_PATH("M17 3v18"),
    ADM_PATH("M17 7.5h4"),      ADM_PATH("M17 16.5h4"),
};

const Shape kInbox[] = {
    ADM_POLY("22 12 16 12 14 15 10 15 8 12 2 12"),
    ADM_PATH("M5.45 5.11 2 12v6a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-6l-3.45-6.89A2 2 0 0 0 16.76 "
             "4H7.24a2 2 0 0 0-1.79 1.11z"),
};

#undef ADM_PATH
#undef ADM_CIRCLE
#undef ADM_RRECT
#undef ADM_LINE
#undef ADM_POLY

struct IconDef {
    const Shape* shapes;
    int count;
};

#define ADM_ICON(a) {a, static_cast<int>(sizeof(a) / sizeof(a[0]))}

const IconDef kToolbarIcons[ICON_COUNT] = {
    ADM_ICON(kPlus),  ADM_ICON(kPlay),     ADM_ICON(kCircleStop), ADM_ICON(kOctagonX),
    ADM_ICON(kTrash), ADM_ICON(kListX),    ADM_ICON(kSettings),   ADM_ICON(kTimer),
    ADM_ICON(kPuzzle), ADM_ICON(kSearch),
};

const IconDef kCategoryIcons[CAT_COUNT] = {
    ADM_ICON(kFolder),
    ADM_ICON(kFilm),
    ADM_ICON(kInbox),
    ADM_ICON(kTimer),
};

#undef ADM_ICON

// Renders one toolbar glyph.
void DrawToolbarGlyph(Graphics& graphics, int icon) {
    DrawShapes(graphics, kToolbarIcons[icon].shapes, kToolbarIcons[icon].count);
}

// Renders one category glyph.
void DrawCategoryGlyph(Graphics& graphics, int icon) {
    DrawShapes(graphics, kCategoryIcons[icon].shapes, kCategoryIcons[icon].count);
}

// Creates a top-down 32-bit DIB section that GDI+ and ImageList can share.
HBITMAP CreateArgbSurface(int size, void** bits) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    return bitmap;
}

// Builds an alpha-blended image list by drawing each glyph with GDI+.
HIMAGELIST BuildImageList(int size, int count, void (*draw)(Graphics&, int)) {
    HIMAGELIST list = ImageList_Create(size, size, ILC_COLOR32, count, 0);
    if (list == nullptr) {
        return nullptr;
    }

    for (int index = 0; index < count; ++index) {
        void* bits = nullptr;
        HBITMAP bitmap = CreateArgbSurface(size, &bits);
        if (bitmap == nullptr) {
            continue;
        }

        Gdiplus::Bitmap surface(size, size, size * 4, PixelFormat32bppPARGB,
                                static_cast<BYTE*>(bits));
        Graphics graphics(&surface);
        graphics.Clear(Color(0, 0, 0, 0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        float scale = size / kGrid;
        graphics.ScaleTransform(scale, scale);
        draw(graphics, index);
        graphics.Flush();

        ImageList_Add(list, bitmap, nullptr);
        DeleteObject(bitmap);
    }

    return list;
}

}  // namespace

// Starts GDI+ so the icon factories can render anti-aliased shapes.
GdiPlusRuntime::GdiPlusRuntime() {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token_, &input, nullptr);
}

// Shuts the GDI+ runtime down once the application exits.
GdiPlusRuntime::~GdiPlusRuntime() {
    if (token_ != 0) {
        Gdiplus::GdiplusShutdown(token_);
        token_ = 0;
    }
}

// Builds the 24x24 toolbar glyphs used by the primary action bar.
HIMAGELIST CreateToolbarImageList(COLORREF stroke) {
    g_stroke = stroke;
    return BuildImageList(kToolbarSize, ICON_COUNT, DrawToolbarGlyph);
}

// Builds the 16x16 glyphs used by the categories tree.
HIMAGELIST CreateCategoryImageList(COLORREF stroke) {
    g_stroke = stroke;
    return BuildImageList(kCategorySize, CAT_COUNT, DrawCategoryGlyph);
}
