#pragma once

#include <string>

#include <windows.h>

// Small drawing helpers the owner-drawn controls share. They go through GDI+ so
// the rounded shapes come out smooth, which the plain GDI rectangles do not.
namespace paint {

// Fills a rounded rectangle and outlines it.
void RoundedRect(HDC dc, const RECT& bounds, float radius, COLORREF fill, COLORREF border,
                 float borderWidth = 1.0f);

// Writes a single line of text, centred or left-aligned, in the given colour.
void Label(HDC dc, const RECT& bounds, const std::wstring& text, COLORREF colour, UINT format);

}  // namespace paint
