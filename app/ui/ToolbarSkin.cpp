#include "ui/ToolbarSkin.h"

#include <objidl.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>

// GDI+ headers still reference the min/max macros that NOMINMAX removes.
using std::max;
using std::min;

#include <gdiplus.h>

#include "core/Paths.h"
#include "core/Text.h"
#include "third_party/json.hpp"
#include "ui/IconFactory.h"

namespace {

constexpr int kIdmButtons = 12;

// Which IDM button each of our buttons borrows; -1 when IDM has none.
constexpr int kIdmSlot[ICON_COUNT] = {
    0,   // Add URL
    1,   // Resume
    2,   // Stop
    3,   // Stop All
    4,   // Delete
    5,   // Delete Completed
    6,   // Options
    7,   // Scheduler
    -1,  // Addons
    -1,  // Search
};

// The folder of the executable.
std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring file(path);
    size_t cut = file.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring() : file.substr(0, cut);
}

// Resolves a path of the descriptor against the folder it lives in.
std::wstring Beside(const std::filesystem::path& descriptor, const std::string& relative) {
    if (relative.empty()) {
        return std::wstring();
    }
    std::wstring wide = Widen(relative);
    std::replace(wide.begin(), wide.end(), L'/', L'\\');
    return (descriptor.parent_path() / wide).wstring();
}

// Reads the `pack.json` of a sprite pack folder; false when there is none.
bool ReadPack(const std::filesystem::path& folder, ToolbarSkin* skin) {
    std::ifstream in(folder / L"pack.json", std::ios::binary);
    if (!in) {
        return false;
    }
    nlohmann::json meta = nlohmann::json::parse(in, nullptr, false);
    if (!meta.is_object()) {
        return false;
    }
    skin->kind = ToolbarSkin::Kind::Sprites;
    skin->name = Widen(meta.value("name", std::string()));
    if (skin->name.empty()) {
        skin->name = folder.filename().wstring();
    }
    skin->folder = folder.wstring();
    skin->isDefault = meta.value("default", false);
    return true;
}

// Reads one `.tbi` descriptor; nothing when it names no large strip.
bool ReadDescriptor(const std::filesystem::path& file, ToolbarSkin* skin) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        values[line.substr(0, equals)] = line.substr(equals + 1);
    }
    if (values["large"].empty()) {
        return false;
    }
    skin->name = Widen(values["name"]);
    if (skin->name.empty()) {
        skin->name = file.stem().wstring();
    }
    skin->large = Beside(file, values["large"]);
    skin->largeHot = Beside(file, values["largeHot"]);
    skin->largeDisabled = Beside(file, values["largeDisabled"]);
    skin->hdpi = Beside(file, values["hdpi"]);
    skin->hdpiHot = Beside(file, values["hdpiHot"]);
    return true;
}

// A strip decoded into raw pixels, with its transparent colour.
struct Strip {
    std::vector<uint32_t> pixels;  // BGRA, row after row
    int width = 0;
    int height = 0;
    uint32_t transparent = 0;
};

// Reads a BMP strip; false when the file is missing or unreadable.
bool ReadStrip(const std::wstring& path, Strip* strip) {
    if (path.empty()) {
        return false;
    }
    Gdiplus::Bitmap source(path.c_str(), FALSE);
    if (source.GetLastStatus() != Gdiplus::Ok || source.GetWidth() == 0) {
        return false;
    }
    strip->width = static_cast<int>(source.GetWidth());
    strip->height = static_cast<int>(source.GetHeight());
    strip->pixels.resize(static_cast<size_t>(strip->width) * strip->height);

    Gdiplus::BitmapData data = {};
    Gdiplus::Rect whole(0, 0, strip->width, strip->height);
    if (source.LockBits(&whole, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) !=
        Gdiplus::Ok) {
        return false;
    }
    for (int y = 0; y < strip->height; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(
            static_cast<const uint8_t*>(data.Scan0) + y * data.Stride);
        std::copy(row, row + strip->width, strip->pixels.begin() + y * strip->width);
    }
    source.UnlockBits(&data);
    strip->transparent = strip->pixels.front() & 0x00FFFFFF;
    return true;
}

// Copies one cell of a strip into a premultiplied surface, the transparent
// colour becoming clear; `alpha` fades the whole cell for a disabled state.
HBITMAP CellBitmap(const Strip& strip, int slot, int cellWidth, int alpha) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = cellWidth;
    info.bmiHeader.biHeight = -strip.height;
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
    auto* out = static_cast<uint32_t*>(bits);
    for (int y = 0; y < strip.height; ++y) {
        for (int x = 0; x < cellWidth; ++x) {
            uint32_t pixel = strip.pixels[y * strip.width + slot * cellWidth + x] & 0x00FFFFFF;
            uint32_t value = 0;
            if (pixel != strip.transparent) {
                uint32_t r = ((pixel >> 16) & 0xFF) * alpha / 255;
                uint32_t g = ((pixel >> 8) & 0xFF) * alpha / 255;
                uint32_t b = (pixel & 0xFF) * alpha / 255;
                value = (static_cast<uint32_t>(alpha) << 24) | (r << 16) | (g << 8) | b;
            }
            out[y * cellWidth + x] = value;
        }
    }
    return bitmap;
}

// Builds one image list from a strip, filling the slots IDM lacks with glyphs.
HIMAGELIST BuildList(const Strip& strip, int cellWidth, int alpha, COLORREF glyph) {
    HIMAGELIST list = ImageList_Create(cellWidth, strip.height, ILC_COLOR32, ICON_COUNT, 0);
    if (list == nullptr) {
        return nullptr;
    }
    for (int icon = 0; icon < ICON_COUNT; ++icon) {
        HBITMAP bitmap = kIdmSlot[icon] >= 0
                             ? CellBitmap(strip, kIdmSlot[icon], cellWidth, alpha)
                             : CreateToolbarGlyph(static_cast<ToolbarIcon>(icon), cellWidth,
                                                  strip.height, glyph);
        if (bitmap != nullptr) {
            ImageList_Add(list, bitmap, nullptr);
            DeleteObject(bitmap);
        }
    }
    return list;
}

}  // namespace

namespace skins {

// The skins found next to the executable and in the user's data folder.
std::vector<ToolbarSkin> Discover() {
    std::vector<ToolbarSkin> found;
    std::wstring data = paths::DataDir();
    std::wstring exe = ExeDir();
    std::vector<std::wstring> folders = {exe + L"\\resources\\toolbar",
                                         exe + L"\\..\\resources\\toolbar"};
    if (!data.empty()) {
        folders.push_back(data + L"\\toolbar");
    }
    for (const std::wstring& folder : folders) {
        std::error_code ignored;
        for (const auto& entry : std::filesystem::directory_iterator(folder, ignored)) {
            if (entry.is_directory(ignored)) {
                ToolbarSkin pack;
                if (ReadPack(entry.path(), &pack)) {
                    found.push_back(std::move(pack));
                }
                continue;
            }
            if (!entry.is_regular_file(ignored)) {
                continue;
            }
            std::wstring extension = entry.path().extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
            if (extension != L".tbi") {
                continue;
            }
            ToolbarSkin skin;
            if (ReadDescriptor(entry.path(), &skin)) {
                found.push_back(std::move(skin));
            }
        }
    }
    std::sort(found.begin(), found.end(), [](const ToolbarSkin& a, const ToolbarSkin& b) {
        if (a.kind != b.kind) {
            return a.kind == ToolbarSkin::Kind::Sprites;
        }
        return lstrcmpiW(a.name.c_str(), b.name.c_str()) < 0;
    });
    // The same pack seen twice, next to the executable and above it, counts once.
    found.erase(std::unique(found.begin(), found.end(),
                            [](const ToolbarSkin& a, const ToolbarSkin& b) {
                                return a.kind == b.kind && a.name == b.name;
                            }),
                found.end());
    return found;
}

// The folder of the default sprite pack.
std::wstring DefaultPackFolder() {
    for (const ToolbarSkin& skin : Discover()) {
        if (skin.kind == ToolbarSkin::Kind::Sprites && skin.isDefault) {
            return skin.folder;
        }
    }
    return std::wstring();
}

// Reads the strips of a skin into image lists laid out for this toolbar.
bool Load(const ToolbarSkin& skin, double scale, COLORREF glyph, COLORREF glyphMuted,
          ToolbarStrips* strips) {
    bool dense = scale >= 1.5 && !skin.hdpi.empty();
    Strip normal;
    if (!ReadStrip(dense ? skin.hdpi : skin.large, &normal) || normal.width < kIdmButtons) {
        return false;
    }
    int cellWidth = normal.width / kIdmButtons;

    Strip hot;
    bool hasHot = ReadStrip(dense ? skin.hdpiHot : skin.largeHot, &hot) && hot.width == normal.width &&
                  hot.height == normal.height;
    Strip disabled;
    bool hasDisabled = !dense && ReadStrip(skin.largeDisabled, &disabled) &&
                       disabled.width == normal.width && disabled.height == normal.height;

    strips->width = cellWidth;
    strips->height = normal.height;
    strips->normal = BuildList(normal, cellWidth, 255, glyph);
    strips->hot = BuildList(hasHot ? hot : normal, cellWidth, 255, glyph);
    strips->disabled = hasDisabled ? BuildList(disabled, cellWidth, 255, glyphMuted)
                                   : BuildList(normal, cellWidth, 96, glyphMuted);
    return strips->normal != nullptr;
}

// Destroys the lists of a skin.
void Release(ToolbarStrips* strips) {
    for (HIMAGELIST* list : {&strips->normal, &strips->hot, &strips->disabled}) {
        if (*list != nullptr) {
            ImageList_Destroy(*list);
            *list = nullptr;
        }
    }
    strips->width = 0;
    strips->height = 0;
}

}  // namespace skins
