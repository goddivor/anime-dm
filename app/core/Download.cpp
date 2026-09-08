#include "core/Download.h"

#include <cwchar>

// The file name of an item, without its folder.
std::wstring FileNameOf(const std::wstring& path) {
    size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? path : path.substr(cut + 1);
}

// Turns any text into a name Windows accepts.
std::wstring SafeFileName(const std::wstring& text) {
    std::wstring clean;
    bool pendingSpace = false;
    for (wchar_t c : text) {
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
            pendingSpace = !clean.empty();
            continue;
        }
        if (pendingSpace) {
            clean += L' ';
            pendingSpace = false;
        }
        clean += (c < 32 || wcschr(L"/\\:*?\"<>|", c) != nullptr) ? L'_' : c;
    }
    while (!clean.empty() && (clean.back() == L'.' || clean.back() == L' ')) {
        clean.pop_back();
    }
    return clean.empty() ? L"video" : clean;
}

// Applies SafeFileName to every segment of a path but its drive.
std::wstring SafePath(const std::wstring& path) {
    std::wstring safe;
    size_t start = 0;
    bool first = true;
    while (start <= path.size()) {
        size_t cut = path.find_first_of(L"\\/", start);
        std::wstring segment = path.substr(start, cut == std::wstring::npos ? std::wstring::npos
                                                                            : cut - start);
        bool drive = first && segment.size() == 2 && segment[1] == L':';
        if (!first) {
            safe += L'\\';
        }
        safe += (drive || segment.empty()) ? segment : SafeFileName(segment);
        first = false;
        if (cut == std::wstring::npos) {
            break;
        }
        start = cut + 1;
    }
    return safe;
}
