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

// Decodes an encoded image into a square, ready for an image list.
HBITMAP DecodeSquare(const std::vector<uint8_t>& bytes, int size) {
    return Decode(bytes, size, size);
}

}  // namespace image
