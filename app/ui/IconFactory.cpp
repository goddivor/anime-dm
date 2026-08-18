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
using Gdiplus::Pen;
using Gdiplus::PointF;
using Gdiplus::SolidBrush;

constexpr int kToolbarSize = 32;
constexpr int kCategorySize = 16;

const Color kGreen(0xFF, 0x3F, 0xA4, 0x4B);
const Color kRed(0xFF, 0xD3, 0x2F, 0x2F);
const Color kGray(0xFF, 0x5F, 0x6A, 0x74);
const Color kTeal(0xFF, 0x00, 0x96, 0x88);
const Color kPurple(0xFF, 0x7B, 0x1F, 0xA2);
const Color kBlue(0xFF, 0x19, 0x76, 0xD2);
const Color kAmber(0xFF, 0xF0, 0xB4, 0x29);
const Color kAmberDark(0xFF, 0xC8, 0x8B, 0x10);

// Draws a polyline through the given points with the supplied pen.
void Polyline(Graphics& g, const Pen& pen, const PointF* points, int count) {
    g.DrawLines(&pen, points, count);
}

// Draws the stacked outline that marks the "apply to every item" variants.
void DrawStackedRing(Graphics& g, const Color& color, float alpha) {
    Color faded(static_cast<BYTE>(alpha * 255), color.GetR(), color.GetG(), color.GetB());
    Pen back(faded, 2.0f);
    g.DrawEllipse(&back, 9.0f, 2.0f, 21.0f, 21.0f);
}

// Renders one 32x32 toolbar glyph onto a cleared, anti-aliased surface.
void DrawToolbarGlyph(Graphics& g, int icon) {
    switch (icon) {
    case ICON_ADD: {
        SolidBrush brush(kGreen);
        g.FillRectangle(&brush, 13.0f, 5.0f, 6.0f, 22.0f);
        g.FillRectangle(&brush, 5.0f, 13.0f, 22.0f, 6.0f);
        break;
    }
    case ICON_RESUME: {
        Pen pen(kGreen, 2.0f);
        g.DrawEllipse(&pen, 3.0f, 3.0f, 26.0f, 26.0f);
        g.DrawLine(&pen, 16.0f, 9.0f, 16.0f, 20.0f);
        PointF chevron[3] = {PointF(11.0f, 15.0f), PointF(16.0f, 21.0f), PointF(21.0f, 15.0f)};
        Polyline(g, pen, chevron, 3);
        break;
    }
    case ICON_STOP: {
        Pen pen(kRed, 2.0f);
        g.DrawEllipse(&pen, 3.0f, 3.0f, 26.0f, 26.0f);
        SolidBrush brush(kRed);
        g.FillRectangle(&brush, 11.5f, 11.5f, 9.0f, 9.0f);
        break;
    }
    case ICON_RESUME_ALL: {
        DrawStackedRing(g, kGreen, 0.45f);
        Pen pen(kGreen, 2.0f);
        g.DrawEllipse(&pen, 2.0f, 9.0f, 21.0f, 21.0f);
        PointF top[3] = {PointF(8.0f, 16.0f), PointF(12.5f, 21.0f), PointF(17.0f, 16.0f)};
        PointF bottom[3] = {PointF(8.0f, 21.5f), PointF(12.5f, 26.0f), PointF(17.0f, 21.5f)};
        Polyline(g, pen, top, 3);
        Polyline(g, pen, bottom, 3);
        break;
    }
    case ICON_STOP_ALL: {
        DrawStackedRing(g, kRed, 0.45f);
        Pen pen(kRed, 2.0f);
        g.DrawEllipse(&pen, 2.0f, 9.0f, 21.0f, 21.0f);
        SolidBrush brush(kRed);
        g.FillRectangle(&brush, 9.0f, 16.0f, 7.5f, 7.5f);
        break;
    }
    case ICON_REMOVE: {
        Pen pen(kGray, 2.0f);
        g.DrawLine(&pen, 6.0f, 9.0f, 26.0f, 9.0f);
        PointF handle[4] = {PointF(12.0f, 9.0f), PointF(12.0f, 5.5f), PointF(20.0f, 5.5f),
                            PointF(20.0f, 9.0f)};
        Polyline(g, pen, handle, 4);
        PointF body[4] = {PointF(9.0f, 11.0f), PointF(10.5f, 26.5f), PointF(21.5f, 26.5f),
                          PointF(23.0f, 11.0f)};
        Polyline(g, pen, body, 4);
        g.DrawLine(&pen, 13.5f, 14.0f, 13.9f, 23.0f);
        g.DrawLine(&pen, 18.5f, 14.0f, 18.1f, 23.0f);
        break;
    }
    case ICON_SETTINGS: {
        SolidBrush brush(kTeal);
        for (int tooth = 0; tooth < 8; ++tooth) {
            g.ResetTransform();
            g.TranslateTransform(16.0f, 16.0f);
            g.RotateTransform(tooth * 45.0f);
            g.FillRectangle(&brush, -2.0f, -15.0f, 4.0f, 6.0f);
        }
        g.ResetTransform();
        Pen pen(kTeal, 3.0f);
        g.DrawEllipse(&pen, 7.5f, 7.5f, 17.0f, 17.0f);
        Pen inner(kTeal, 2.0f);
        g.DrawEllipse(&inner, 12.5f, 12.5f, 7.0f, 7.0f);
        break;
    }
    case ICON_SCHEDULE: {
        Pen pen(kPurple, 2.0f);
        g.DrawEllipse(&pen, 4.0f, 4.0f, 24.0f, 24.0f);
        g.DrawLine(&pen, 16.0f, 16.0f, 16.0f, 9.5f);
        g.DrawLine(&pen, 16.0f, 16.0f, 21.0f, 18.5f);
        break;
    }
    case ICON_DOWNLOADS: {
        Pen pen(kBlue, 2.0f);
        g.DrawLine(&pen, 16.0f, 4.0f, 16.0f, 18.0f);
        PointF chevron[3] = {PointF(10.0f, 12.5f), PointF(16.0f, 19.0f), PointF(22.0f, 12.5f)};
        Polyline(g, pen, chevron, 3);
        PointF tray[4] = {PointF(5.0f, 21.0f), PointF(5.0f, 27.0f), PointF(27.0f, 27.0f),
                          PointF(27.0f, 21.0f)};
        Polyline(g, pen, tray, 4);
        break;
    }
    case ICON_ADDONS: {
        Pen pen(kBlue, 2.0f);
        g.DrawRectangle(&pen, 4.0f, 4.0f, 10.0f, 10.0f);
        g.DrawRectangle(&pen, 18.0f, 4.0f, 10.0f, 10.0f);
        g.DrawRectangle(&pen, 4.0f, 18.0f, 10.0f, 10.0f);
        SolidBrush brush(kBlue);
        g.FillRectangle(&brush, 18.0f, 18.0f, 10.0f, 10.0f);
        break;
    }
    default:
        break;
    }
}

// Draws the shared folder body used by the closed and open category glyphs.
void DrawFolderBody(Graphics& g, bool open) {
    SolidBrush fill(kAmber);
    Pen edge(kAmberDark, 1.0f);
    PointF back[6] = {PointF(1.0f, 4.0f),  PointF(6.0f, 4.0f),   PointF(7.5f, 6.0f),
                      PointF(14.5f, 6.0f), PointF(14.5f, 13.0f), PointF(1.0f, 13.0f)};
    g.FillPolygon(&fill, back, 6);
    g.DrawPolygon(&edge, back, 6);
    if (open) {
        SolidBrush front(Color(0xFF, 0xFF, 0xD1, 0x77));
        PointF flap[4] = {PointF(2.5f, 13.0f), PointF(4.5f, 7.5f), PointF(15.5f, 7.5f),
                          PointF(13.5f, 13.0f)};
        g.FillPolygon(&front, flap, 4);
        g.DrawPolygon(&edge, flap, 4);
    }
}

// Renders one 16x16 category glyph onto a cleared, anti-aliased surface.
void DrawCategoryGlyph(Graphics& g, int icon) {
    switch (icon) {
    case CAT_FOLDER:
        DrawFolderBody(g, false);
        break;
    case CAT_FOLDER_OPEN:
        DrawFolderBody(g, true);
        break;
    case CAT_VIDEO: {
        Pen pen(kBlue, 1.4f);
        g.DrawRectangle(&pen, 1.5f, 3.0f, 13.0f, 10.0f);
        SolidBrush brush(kBlue);
        PointF play[3] = {PointF(6.5f, 5.5f), PointF(6.5f, 10.5f), PointF(11.0f, 8.0f)};
        g.FillPolygon(&brush, play, 3);
        break;
    }
    case CAT_PENDING: {
        Pen pen(kPurple, 1.4f);
        g.DrawEllipse(&pen, 1.5f, 1.5f, 13.0f, 13.0f);
        g.DrawLine(&pen, 8.0f, 8.0f, 8.0f, 4.5f);
        g.DrawLine(&pen, 8.0f, 8.0f, 11.0f, 9.5f);
        break;
    }
    case CAT_DONE: {
        Pen pen(kGreen, 2.0f);
        PointF check[3] = {PointF(3.0f, 8.5f), PointF(6.5f, 12.0f), PointF(13.0f, 4.5f)};
        Polyline(g, pen, check, 3);
        break;
    }
    case CAT_QUEUE: {
        Pen pen(kGray, 1.6f);
        g.DrawLine(&pen, 5.0f, 4.0f, 14.0f, 4.0f);
        g.DrawLine(&pen, 5.0f, 8.0f, 14.0f, 8.0f);
        g.DrawLine(&pen, 5.0f, 12.0f, 14.0f, 12.0f);
        SolidBrush brush(kGray);
        g.FillRectangle(&brush, 1.5f, 3.0f, 2.0f, 2.0f);
        g.FillRectangle(&brush, 1.5f, 7.0f, 2.0f, 2.0f);
        g.FillRectangle(&brush, 1.5f, 11.0f, 2.0f, 2.0f);
        break;
    }
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

// Builds the 32x32 toolbar glyphs used by the primary action bar.
HIMAGELIST CreateToolbarImageList() {
    return BuildImageList(kToolbarSize, ICON_COUNT, DrawToolbarGlyph);
}

// Builds the 16x16 glyphs used by the categories tree.
HIMAGELIST CreateCategoryImageList() {
    return BuildImageList(kCategorySize, CAT_COUNT, DrawCategoryGlyph);
}
