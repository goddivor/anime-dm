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

}  // namespace paint
