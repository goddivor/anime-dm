#include "ui/Format.h"

#include <cmath>
#include <cstdio>

#include "ui/Strings.h"

namespace {

constexpr wchar_t kDash[] = L"—";
constexpr const wchar_t* kUnits[] = {L"B", L"KB", L"MB", L"GB"};

// A number with one decimal, using the separator of the active language.
std::wstring Decimal(double value) {
    wchar_t text[32] = {};
    swprintf(text, 32, L"%.1f", value);
    if (ActiveLanguage() == Language::French) {
        for (wchar_t* c = text; *c != L'\0'; ++c) {
            if (*c == L'.') {
                *c = L',';
            }
        }
    }
    return text;
}

// A byte count scaled to the largest unit that keeps it readable.
std::wstring Scaled(double bytes, const wchar_t* suffix) {
    int unit = 0;
    while (bytes >= 1024.0 && unit < 3) {
        bytes /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return std::to_wstring(static_cast<uint64_t>(bytes)) + L" " + kUnits[0] + suffix;
    }
    return Decimal(bytes) + L" " + kUnits[unit] + suffix;
}

}  // namespace

namespace format {

// What a cell shows when it has nothing to say.
const wchar_t* Dash() {
    return kDash;
}

// "12,3 MB"; a dash when zero.
std::wstring Size(uint64_t bytes) {
    return bytes == 0 ? kDash : Scaled(static_cast<double>(bytes), L"");
}

// "1,2 MB/s"; a dash when zero.
std::wstring Speed(double bytesPerSecond) {
    return bytesPerSecond <= 0.0 ? kDash : Scaled(bytesPerSecond, L"/s");
}

// "3 min 12 s"; a dash when negative.
std::wstring Duration(double seconds) {
    if (seconds < 0.0 || !std::isfinite(seconds)) {
        return kDash;
    }
    uint64_t s = static_cast<uint64_t>(seconds);
    if (s >= 3600) {
        return std::to_wstring(s / 3600) + L" h " + std::to_wstring((s % 3600) / 60) + L" min";
    }
    if (s >= 60) {
        return std::to_wstring(s / 60) + L" min " + std::to_wstring(s % 60) + L" s";
    }
    return std::to_wstring(s) + L" s";
}

// "2026-09-08 14:05"; a dash when zero.
std::wstring Date(std::time_t when) {
    if (when == 0) {
        return kDash;
    }
    std::tm local = {};
    if (localtime_s(&local, &when) != 0) {
        return kDash;
    }
    wchar_t text[32] = {};
    swprintf(text, 32, L"%04d-%02d-%02d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1,
             local.tm_mday, local.tm_hour, local.tm_min);
    return text;
}

}  // namespace format
