#include "ui/IconFactory.h"

#include <objidl.h>

#include <algorithm>

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
using Gdiplus::SolidBrush;

constexpr int kToolbarSize = 24;
constexpr int kCategorySize = 16;
constexpr float kGrid = 24.0f;

// Returns the monochrome stroke colour, matching the current system text colour.
Color StrokeColor() {
    COLORREF system = GetSysColor(COLOR_BTNTEXT);
    return Color(0xFF, GetRValue(system), GetGValue(system), GetBValue(system));
}

// Applies the rounded caps and joins that give the glyphs their lucide look.
void Round(Pen& pen) {
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
}

// Strokes an open polyline through the given points.
void Stroke(Graphics& g, Pen& pen, const PointF* points, int count) {
    g.DrawLines(&pen, points, count);
}

// Draws the eight teeth and two rings that make up the options gear.
void DrawGear(Graphics& g, Pen& pen) {
    for (int tooth = 0; tooth < 8; ++tooth) {
        Gdiplus::Matrix saved;
        g.GetTransform(&saved);
        g.TranslateTransform(12.0f, 12.0f);
        g.RotateTransform(tooth * 45.0f);
        g.DrawLine(&pen, 0.0f, -7.5f, 0.0f, -10.5f);
        g.SetTransform(&saved);
    }
    g.DrawEllipse(&pen, 4.5f, 4.5f, 15.0f, 15.0f);
    g.DrawEllipse(&pen, 8.5f, 8.5f, 7.0f, 7.0f);
}

// Draws the puzzle piece used by the addons button.
void DrawPuzzle(Graphics& g, Pen& pen) {
    GraphicsPath path;
    path.AddLine(5.0f, 5.0f, 9.0f, 5.0f);
    path.AddArc(9.0f, 1.5f, 6.0f, 7.0f, 180.0f, 180.0f);
    path.AddLine(15.0f, 5.0f, 19.0f, 5.0f);
    path.AddLine(19.0f, 5.0f, 19.0f, 19.0f);
    path.AddLine(19.0f, 19.0f, 5.0f, 19.0f);
    path.AddLine(5.0f, 19.0f, 5.0f, 15.0f);
    path.AddArc(1.5f, 9.0f, 7.0f, 6.0f, 90.0f, 180.0f);
    path.CloseFigure();
    g.DrawPath(&pen, &path);
}

// Renders one toolbar glyph on the 24-unit lucide grid.
void DrawToolbarGlyph(Graphics& g, int icon) {
    Pen pen(StrokeColor(), 2.0f);
    Round(pen);

    switch (icon) {
    case ICON_ADD_URL:
        g.DrawLine(&pen, 5.0f, 12.0f, 19.0f, 12.0f);
        g.DrawLine(&pen, 12.0f, 5.0f, 12.0f, 19.0f);
        break;
    case ICON_RESUME: {
        PointF play[3] = {PointF(7.0f, 4.0f), PointF(20.0f, 12.0f), PointF(7.0f, 20.0f)};
        g.DrawPolygon(&pen, play, 3);
        break;
    }
    case ICON_STOP:
        g.DrawEllipse(&pen, 2.0f, 2.0f, 20.0f, 20.0f);
        g.DrawRectangle(&pen, 9.0f, 9.0f, 6.0f, 6.0f);
        break;
    case ICON_STOP_ALL:
        g.DrawEllipse(&pen, 2.0f, 2.0f, 20.0f, 20.0f);
        g.DrawLine(&pen, 15.0f, 9.0f, 9.0f, 15.0f);
        g.DrawLine(&pen, 9.0f, 9.0f, 15.0f, 15.0f);
        break;
    case ICON_REMOVE: {
        g.DrawLine(&pen, 3.0f, 6.0f, 21.0f, 6.0f);
        PointF body[6] = {PointF(19.0f, 6.0f),  PointF(19.0f, 20.0f), PointF(17.0f, 22.0f),
                          PointF(7.0f, 22.0f),  PointF(5.0f, 20.0f),  PointF(5.0f, 6.0f)};
        Stroke(g, pen, body, 6);
        PointF lid[6] = {PointF(8.0f, 6.0f),  PointF(8.0f, 4.0f),  PointF(10.0f, 2.0f),
                         PointF(14.0f, 2.0f), PointF(16.0f, 4.0f), PointF(16.0f, 6.0f)};
        Stroke(g, pen, lid, 6);
        g.DrawLine(&pen, 10.0f, 11.0f, 10.0f, 17.0f);
        g.DrawLine(&pen, 14.0f, 11.0f, 14.0f, 17.0f);
        break;
    }
    case ICON_REMOVE_ALL:
        g.DrawLine(&pen, 3.0f, 6.0f, 16.0f, 6.0f);
        g.DrawLine(&pen, 3.0f, 12.0f, 11.0f, 12.0f);
        g.DrawLine(&pen, 3.0f, 18.0f, 11.0f, 18.0f);
        g.DrawLine(&pen, 17.0f, 10.0f, 21.0f, 14.0f);
        g.DrawLine(&pen, 21.0f, 10.0f, 17.0f, 14.0f);
        break;
    case ICON_OPTIONS:
        DrawGear(g, pen);
        break;
    case ICON_SCHEDULE:
        g.DrawLine(&pen, 10.0f, 2.0f, 14.0f, 2.0f);
        g.DrawEllipse(&pen, 4.0f, 6.0f, 16.0f, 16.0f);
        g.DrawLine(&pen, 12.0f, 14.0f, 15.0f, 11.0f);
        break;
    case ICON_ADDONS:
        DrawPuzzle(g, pen);
        break;
    default:
        break;
    }
}

// Renders one category glyph on the same 24-unit grid, scaled down by the caller.
void DrawCategoryGlyph(Graphics& g, int icon) {
    Pen pen(StrokeColor(), 2.0f);
    Round(pen);

    switch (icon) {
    case CAT_FOLDER: {
        PointF folder[7] = {PointF(2.0f, 19.0f), PointF(2.0f, 5.0f),   PointF(9.0f, 5.0f),
                            PointF(11.5f, 8.0f), PointF(22.0f, 8.0f),  PointF(22.0f, 19.0f),
                            PointF(2.0f, 19.0f)};
        Stroke(g, pen, folder, 7);
        break;
    }
    case CAT_ANIME: {
        g.DrawRectangle(&pen, 3.0f, 4.0f, 18.0f, 16.0f);
        PointF hill[3] = {PointF(6.0f, 17.0f), PointF(11.0f, 11.0f), PointF(18.0f, 17.0f)};
        Stroke(g, pen, hill, 3);
        SolidBrush brush(StrokeColor());
        g.FillEllipse(&brush, 14.0f, 7.0f, 3.5f, 3.5f);
        break;
    }
    case CAT_EPISODE: {
        Pen check(Color(0xFF, 0x3F, 0xA4, 0x4B), 2.4f);
        Round(check);
        PointF marks[3] = {PointF(4.0f, 12.5f), PointF(9.5f, 18.0f), PointF(20.0f, 6.0f)};
        Stroke(g, check, marks, 3);
        break;
    }
    case CAT_QUEUE: {
        PointF tray[6] = {PointF(2.0f, 12.0f),  PointF(8.0f, 12.0f),  PointF(10.0f, 15.0f),
                          PointF(14.0f, 15.0f), PointF(16.0f, 12.0f), PointF(22.0f, 12.0f)};
        Stroke(g, pen, tray, 6);
        PointF box[5] = {PointF(2.0f, 12.0f), PointF(2.0f, 20.0f), PointF(22.0f, 20.0f),
                         PointF(22.0f, 12.0f), PointF(19.0f, 4.5f)};
        Stroke(g, pen, box, 5);
        g.DrawLine(&pen, 5.0f, 4.5f, 19.0f, 4.5f);
        g.DrawLine(&pen, 5.0f, 4.5f, 2.0f, 12.0f);
        break;
    }
    case CAT_TIMER:
        g.DrawLine(&pen, 10.0f, 2.0f, 14.0f, 2.0f);
        g.DrawEllipse(&pen, 4.0f, 6.0f, 16.0f, 16.0f);
        g.DrawLine(&pen, 12.0f, 14.0f, 15.0f, 11.0f);
        break;
    default:
        break;
    }
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
HIMAGELIST CreateToolbarImageList() {
    return BuildImageList(kToolbarSize, ICON_COUNT, DrawToolbarGlyph);
}

// Builds the 16x16 glyphs used by the categories tree.
HIMAGELIST CreateCategoryImageList() {
    return BuildImageList(kCategorySize, CAT_COUNT, DrawCategoryGlyph);
}
