#include "ui/IconFactory.h"

namespace {
constexpr int kIconSize = 16;
constexpr COLORREF kMask = RGB(255, 0, 255);
constexpr COLORREF kGreen = RGB(46, 160, 67);
constexpr COLORREF kRed = RGB(207, 52, 52);
constexpr COLORREF kGray = RGB(80, 80, 80);

// Fills a rectangle with a solid colour.
void FillBox(HDC dc, int left, int top, int right, int bottom, COLORREF color) {
    RECT rect = {left, top, right, bottom};
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

// Fills a polygon with a solid colour and matching outline.
void FillPoly(HDC dc, const POINT* points, int count, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, brush));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    Polygon(dc, points, count);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

// Draws two thick strokes forming an X.
void DrawCross(HDC dc, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, 4, 4, nullptr);
    LineTo(dc, 12, 12);
    MoveToEx(dc, 12, 4, nullptr);
    LineTo(dc, 4, 12);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

// Draws two slider rails with knobs (settings glyph).
void DrawSliders(HDC dc, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, 3, 6, nullptr);
    LineTo(dc, 13, 6);
    MoveToEx(dc, 3, 11, nullptr);
    LineTo(dc, 13, 11);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    FillBox(dc, 9, 4, 12, 8, color);
    FillBox(dc, 4, 9, 7, 13, color);
}

// Renders a single glyph onto the prepared (mask-filled) device context.
void DrawGlyph(HDC dc, int icon) {
    switch (icon) {
    case ICON_ADD:
        FillBox(dc, 7, 3, 9, 13, kGreen);
        FillBox(dc, 3, 7, 13, 9, kGreen);
        break;
    case ICON_RESUME: {
        POINT triangle[3] = {{5, 3}, {5, 13}, {12, 8}};
        FillPoly(dc, triangle, 3, kGreen);
        break;
    }
    case ICON_STOP:
        FillBox(dc, 4, 4, 12, 12, kRed);
        break;
    case ICON_REMOVE:
        DrawCross(dc, kRed);
        break;
    case ICON_SETTINGS:
        DrawSliders(dc, kGray);
        break;
    default:
        break;
    }
}
}  // namespace

// Builds the toolbar image list by drawing each glyph over a mask colour.
HIMAGELIST CreateToolbarImageList() {
    HIMAGELIST list = ImageList_Create(kIconSize, kIconSize, ILC_COLOR24 | ILC_MASK, ICON_COUNT, 0);
    HDC screen = GetDC(nullptr);

    for (int icon = 0; icon < ICON_COUNT; ++icon) {
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, kIconSize, kIconSize);
        HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(dc, bitmap));

        FillBox(dc, 0, 0, kIconSize, kIconSize, kMask);
        DrawGlyph(dc, icon);

        SelectObject(dc, oldBitmap);
        ImageList_AddMasked(list, bitmap, kMask);

        DeleteObject(bitmap);
        DeleteDC(dc);
    }

    ReleaseDC(nullptr, screen);
    return list;
}
