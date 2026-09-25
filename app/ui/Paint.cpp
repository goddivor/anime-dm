#include "ui/Paint.h"

#include <objidl.h>

#include <algorithm>

// GDI+ headers still reference the min/max macros that NOMINMAX removes.
using std::max;
using std::min;

#include <gdiplus.h>

namespace {

Gdiplus::Color Of(COLORREF colour) {
    return Gdiplus::Color(0xFF, GetRValue(colour), GetGValue(colour), GetBValue(colour));
}

}  // namespace

namespace paint {

// Fills a rounded rectangle and outlines it.
void RoundedRect(HDC dc, const RECT& bounds, float radius, COLORREF fill, COLORREF border,
                 float borderWidth) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    float x = static_cast<float>(bounds.left);
    float y = static_cast<float>(bounds.top);
    float width = static_cast<float>(bounds.right - bounds.left) - borderWidth;
    float height = static_cast<float>(bounds.bottom - bounds.top) - borderWidth;
    float diameter = radius * 2.0f;

    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(x + width - diameter, y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(x, y + height - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();

    Gdiplus::SolidBrush brush(Of(fill));
    graphics.FillPath(&brush, &path);

    if (borderWidth > 0.0f) {
        Gdiplus::Pen pen(Of(border), borderWidth);
        graphics.DrawPath(&pen, &path);
    }
}

// Writes a single line of text in the given colour.
void Label(HDC dc, const RECT& bounds, const std::wstring& text, COLORREF colour, UINT format) {
    RECT area = bounds;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colour);
    DrawTextW(dc, text.c_str(), -1, &area, format | DT_SINGLELINE | DT_NOPREFIX);
}

// Draws the chevron that folds a tree row, its strokes smoothed.
void Chevron(HDC dc, POINT centre, int size, bool open, COLORREF colour) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(Of(colour), std::max(1.2f, static_cast<float>(size) / 6.0f));
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    float x = static_cast<float>(centre.x) + 0.5f;
    float y = static_cast<float>(centre.y) + 0.5f;
    float half = static_cast<float>(size) / 2.0f;
    float quarter = half / 2.0f;
    Gdiplus::PointF points[3];
    if (open) {
        points[0] = Gdiplus::PointF(x - half, y - quarter);
        points[1] = Gdiplus::PointF(x, y + quarter);
        points[2] = Gdiplus::PointF(x + half, y - quarter);
    } else {
        points[0] = Gdiplus::PointF(x - quarter, y - half);
        points[1] = Gdiplus::PointF(x + quarter, y);
        points[2] = Gdiplus::PointF(x - quarter, y + half);
    }
    graphics.DrawLines(&pen, points, 3);
}

}  // namespace paint
