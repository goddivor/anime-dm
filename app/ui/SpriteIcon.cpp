#include "ui/SpriteIcon.h"

#include <objidl.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

// GDI+ headers still reference the min/max macros that NOMINMAX removes.
using std::max;
using std::min;

#include <gdiplus.h>

#include "third_party/json.hpp"

namespace {

// The folder of the executable.
std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring file(path);
    size_t cut = file.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring() : file.substr(0, cut);
}

// Reads `#RRGGBB` into 0xRRGGBB; false when the text is not a colour.
bool ParseColour(const std::string& text, uint32_t* colour) {
    if (text.size() != 7 || text[0] != '#') {
        return false;
    }
    char* end = nullptr;
    unsigned long value = std::strtoul(text.c_str() + 1, &end, 16);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    *colour = static_cast<uint32_t>(value);
    return true;
}

}  // namespace

// Reads the BMP, cuts it into frames and clears the colour key.
bool Sprite::Load(const std::wstring& bmpPath) {
    frames_.clear();
    if (bmpPath.empty()) {
        return false;
    }
    Gdiplus::Bitmap source(bmpPath.c_str(), FALSE);
    if (source.GetLastStatus() != Gdiplus::Ok || source.GetWidth() == 0) {
        return false;
    }
    int width = static_cast<int>(source.GetWidth());
    int height = static_cast<int>(source.GetHeight());

    Gdiplus::BitmapData data = {};
    Gdiplus::Rect whole(0, 0, width, height);
    if (source.LockBits(&whole, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) !=
        Gdiplus::Ok) {
        return false;
    }
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(
            static_cast<const uint8_t*>(data.Scan0) + y * data.Stride);
        std::copy(row, row + width, pixels.begin() + static_cast<size_t>(y) * width);
    }
    source.UnlockBits(&data);

    frameWidth_ = height;
    frameHeight_ = height;
    int count = width / std::max(1, height);
    uint32_t key = pixels.front() & 0x00FFFFFF;
    durationMs_ = 120;

    std::filesystem::path descriptor(bmpPath);
    descriptor.replace_extension(L".json");
    std::ifstream file(descriptor, std::ios::binary);
    if (file) {
        nlohmann::json meta = nlohmann::json::parse(file, nullptr, false);
        if (meta.is_object()) {
            frameWidth_ = meta.value("frameWidth", frameWidth_);
            frameHeight_ = meta.value("frameHeight", frameHeight_);
            count = meta.value("frameCount", count);
            durationMs_ = meta.value("durationMs", durationMs_);
            ParseColour(meta.value("colourKey", std::string()), &key);
        }
    }
    if (frameWidth_ <= 0 || frameHeight_ <= 0 || frameHeight_ > height) {
        return false;
    }
    count = std::min(count, width / frameWidth_);

    for (int frame = 0; frame < count; ++frame) {
        std::vector<uint32_t> cell(static_cast<size_t>(frameWidth_) * frameHeight_);
        for (int y = 0; y < frameHeight_; ++y) {
            for (int x = 0; x < frameWidth_; ++x) {
                uint32_t pixel = pixels[static_cast<size_t>(y) * width + frame * frameWidth_ + x];
                cell[static_cast<size_t>(y) * frameWidth_ + x] =
                    (pixel & 0x00FFFFFF) == key ? 0 : (pixel | 0xFF000000);
            }
        }
        frames_.push_back(std::move(cell));
    }
    return !frames_.empty();
}

// Renders one frame fitted into a cell, centred, the colour key made clear.
HBITMAP Sprite::Render(int frame, int width, int height) const {
    if (frames_.empty() || width <= 0 || height <= 0) {
        return nullptr;
    }
    frame = std::clamp(frame, 0, FrameCount() - 1);

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (bitmap == nullptr) {
        return nullptr;
    }

    std::vector<uint32_t> copy = frames_[static_cast<size_t>(frame)];
    Gdiplus::Bitmap source(frameWidth_, frameHeight_, frameWidth_ * 4, PixelFormat32bppARGB,
                           reinterpret_cast<BYTE*>(copy.data()));
    Gdiplus::Bitmap surface(width, height, width * 4, PixelFormat32bppPARGB,
                            static_cast<BYTE*>(bits));
    Gdiplus::Graphics graphics(&surface);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    float scale = std::min(static_cast<float>(width) / static_cast<float>(frameWidth_),
                           static_cast<float>(height) / static_cast<float>(frameHeight_));
    float drawnWidth = static_cast<float>(frameWidth_) * scale;
    float drawnHeight = static_cast<float>(frameHeight_) * scale;
    graphics.DrawImage(&source, Gdiplus::RectF((static_cast<float>(width) - drawnWidth) / 2.0f,
                                               (static_cast<float>(height) - drawnHeight) / 2.0f,
                                               drawnWidth, drawnHeight));
    graphics.Flush();
    return bitmap;
}

// The path of a sprite shipped in `resources\sprites`.
std::wstring FindSprite(const wchar_t* fileName) {
    std::wstring exe = ExeDir();
    for (const std::wstring& base : {exe, exe + L"\\.."}) {
        std::wstring path = base + L"\\resources\\sprites\\" + fileName;
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return path;
        }
    }
    return std::wstring();
}
