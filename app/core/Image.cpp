#include "core/Image.h"

#include <objidl.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstring>

// GDI+ headers still reference the min/max macros that NOMINMAX removes.
using std::max;
using std::min;

#include <gdiplus.h>

namespace {

// Creates the top-down 32-bit surface GDI+ and the image lists share.
HBITMAP CreateSurface(int width, int height, void** bits) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    return bitmap;
}

}  // namespace

namespace image {

// A fully transparent square.
HBITMAP Transparent(int size) {
    void* bits = nullptr;
    HBITMAP bitmap = CreateSurface(size, size, &bits);
    if (bitmap != nullptr && bits != nullptr) {
        std::memset(bits, 0, static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    }
    return bitmap;
}

// Decodes an encoded image and scales it to the requested size, keeping an
// alpha channel.
HBITMAP Decode(const std::vector<uint8_t>& bytes, int width, int height) {
    if (bytes.empty() || width <= 0 || height <= 0) {
        return nullptr;
    }

    IStream* stream = SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size()));
    if (stream == nullptr) {
        return nullptr;
    }

    Gdiplus::Bitmap source(stream, FALSE);
    stream->Release();
    if (source.GetLastStatus() != Gdiplus::Ok) {
        return nullptr;
    }

    void* bits = nullptr;
    HBITMAP bitmap = CreateSurface(width, height, &bits);
    if (bitmap == nullptr) {
        return nullptr;
    }

    Gdiplus::Bitmap surface(width, height, width * 4, PixelFormat32bppPARGB,
                            static_cast<BYTE*>(bits));
    Gdiplus::Graphics graphics(&surface);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.DrawImage(&source, 0, 0, width, height);
    graphics.Flush();

    return bitmap;
}

// Decodes an encoded image inside a box, keeping its proportions.
HBITMAP Fit(const std::vector<uint8_t>& bytes, int width, int height) {
    if (bytes.empty() || width <= 0 || height <= 0) {
        return nullptr;
    }

    IStream* stream = SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size()));
    if (stream == nullptr) {
        return nullptr;
    }
    Gdiplus::Bitmap source(stream, FALSE);
    stream->Release();
    if (source.GetLastStatus() != Gdiplus::Ok) {
        return nullptr;
    }

    float scale = min(static_cast<float>(width) / static_cast<float>(source.GetWidth()),
                      static_cast<float>(height) / static_cast<float>(source.GetHeight()));
    int fitted = max(1, static_cast<int>(static_cast<float>(source.GetWidth()) * scale));
    int tall = max(1, static_cast<int>(static_cast<float>(source.GetHeight()) * scale));
    return Decode(bytes, fitted, tall);
}

// Fills a box with the source, cropping what overflows, with rounded corners.
HBITMAP Cover(const std::vector<uint8_t>& bytes, int width, int height, int radius) {
    if (bytes.empty() || width <= 0 || height <= 0) {
        return nullptr;
    }

    IStream* stream = SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size()));
    if (stream == nullptr) {
        return nullptr;
    }
    Gdiplus::Bitmap source(stream, FALSE);
    stream->Release();
    if (source.GetLastStatus() != Gdiplus::Ok || source.GetWidth() == 0 ||
        source.GetHeight() == 0) {
        return nullptr;
    }

    void* bits = nullptr;
    HBITMAP bitmap = CreateSurface(width, height, &bits);
    if (bitmap == nullptr) {
        return nullptr;
    }

    float scale = max(static_cast<float>(width) / static_cast<float>(source.GetWidth()),
                      static_cast<float>(height) / static_cast<float>(source.GetHeight()));
    float drawnWidth = static_cast<float>(source.GetWidth()) * scale;
    float drawnHeight = static_cast<float>(source.GetHeight()) * scale;
    float left = (static_cast<float>(width) - drawnWidth) / 2.0f;
    float top = (static_cast<float>(height) - drawnHeight) / 2.0f;

    Gdiplus::Bitmap surface(width, height, width * 4, PixelFormat32bppPARGB,
                            static_cast<BYTE*>(bits));
    Gdiplus::Graphics graphics(&surface);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::GraphicsPath clip;
    float diameter = static_cast<float>(radius) * 2.0f;
    float w = static_cast<float>(width);
    float h = static_cast<float>(height);
    if (radius > 0) {
        clip.AddArc(0.0f, 0.0f, diameter, diameter, 180.0f, 90.0f);
        clip.AddArc(w - diameter, 0.0f, diameter, diameter, 270.0f, 90.0f);
        clip.AddArc(w - diameter, h - diameter, diameter, diameter, 0.0f, 90.0f);
        clip.AddArc(0.0f, h - diameter, diameter, diameter, 90.0f, 90.0f);
        clip.CloseFigure();
    } else {
        clip.AddRectangle(Gdiplus::RectF(0.0f, 0.0f, w, h));
    }
    graphics.SetClip(&clip);
    graphics.DrawImage(&source, Gdiplus::RectF(left, top, drawnWidth, drawnHeight));
    graphics.Flush();
    return bitmap;
}

// Decodes an encoded image into a square, ready for an image list.
HBITMAP DecodeSquare(const std::vector<uint8_t>& bytes, int size) {
    return Decode(bytes, size, size);
}

}  // namespace image
