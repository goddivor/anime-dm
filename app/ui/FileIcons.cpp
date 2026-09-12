#include "ui/FileIcons.h"

#include <shellapi.h>

#include <algorithm>
#include <map>

namespace {

// The extension of a path, lowercased and with its dot; empty when there is
// none. A name without extension shares the icon of every plain file.
std::wstring ExtensionOf(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    size_t slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
        return std::wstring();
    }
    std::wstring extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    return extension;
}

}  // namespace

// The small image list of the shell, asked for once.
HIMAGELIST fileicons::SmallList() {
    static HIMAGELIST list = [] {
        SHFILEINFOW info = {};
        return reinterpret_cast<HIMAGELIST>(
            SHGetFileInfoW(L"file", FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                           SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
    }();
    return list;
}

// The index of the icon of a file name, from a cache kept by extension.
int fileicons::IndexOf(const std::wstring& path) {
    if (SmallList() == nullptr) {
        return -1;
    }
    static std::map<std::wstring, int> known;
    std::wstring extension = ExtensionOf(path);
    auto seen = known.find(extension);
    if (seen != known.end()) {
        return seen->second;
    }

    SHFILEINFOW info = {};
    std::wstring name = L"file" + extension;
    int index = SHGetFileInfoW(name.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                               SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON) != 0
                    ? info.iIcon
                    : -1;
    known.emplace(extension, index);
    return index;
}

// The side of one icon of the shell list.
int fileicons::Size() {
    int width = 0;
    int height = 0;
    if (SmallList() == nullptr || !ImageList_GetIconSize(SmallList(), &width, &height)) {
        return GetSystemMetrics(SM_CXSMICON);
    }
    return width;
}
