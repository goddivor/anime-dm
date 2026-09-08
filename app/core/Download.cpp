#include "core/Download.h"

// The file name of an item, without its folder.
std::wstring FileNameOf(const std::wstring& path) {
    size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? path : path.substr(cut + 1);
}
