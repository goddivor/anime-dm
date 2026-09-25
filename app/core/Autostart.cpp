#include "core/Autostart.h"

#include <windows.h>

#include <string>

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValue[] = L"AnimeDownloadManager";

// The running executable, quoted for a command line.
std::wstring QuotedExe() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return L"\"" + std::wstring(path) + L"\"";
}

}  // namespace

namespace autostart {

// Whether the Run key names the application.
bool Enabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    bool found = RegQueryValueExW(key, kValue, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return found;
}

// Writes or removes the value; the path is refreshed each time, in case the
// executable moved.
void Set(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return;
    }
    if (enabled) {
        std::wstring line = QuotedExe() + L" " + kTraySwitch;
        RegSetValueExW(key, kValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(line.c_str()),
                       static_cast<DWORD>((line.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kValue);
    }
    RegCloseKey(key);
}

}  // namespace autostart
