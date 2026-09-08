#include "core/Paths.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>

namespace {

constexpr wchar_t kFolder[] = L"anime-dm";

}  // namespace

namespace paths {

// Creates a folder and every missing parent.
bool EnsureDir(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    if (CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }
    if (GetLastError() != ERROR_PATH_NOT_FOUND) {
        return false;
    }

    size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring::npos) {
        return false;
    }
    if (!EnsureDir(path.substr(0, cut))) {
        return false;
    }
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// `%APPDATA%\anime-dm`, created on demand.
std::wstring DataDir() {
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) {
        return std::wstring();
    }

    std::wstring dir(roaming);
    CoTaskMemFree(roaming);
    dir += L"\\";
    dir += kFolder;

    return EnsureDir(dir) ? dir : std::wstring();
}

// `<data>/addons`.
std::wstring AddonsDir() {
    std::wstring base = DataDir();
    if (base.empty()) {
        return std::wstring();
    }
    std::wstring dir = base + L"\\addons";
    return EnsureDir(dir) ? dir : std::wstring();
}

// `<data>/settings.json`.
std::wstring SettingsFile() {
    std::wstring base = DataDir();
    return base.empty() ? std::wstring() : base + L"\\settings.json";
}

// `<data>/downloads.json`.
std::wstring DownloadsFile() {
    std::wstring base = DataDir();
    return base.empty() ? std::wstring() : base + L"\\downloads.json";
}

// `<data>/parts/<id>`, created on demand.
std::wstring PartsDir(unsigned long long id) {
    std::wstring base = DataDir();
    if (base.empty()) {
        return std::wstring();
    }
    std::wstring dir = base + L"\\parts\\" + std::to_wstring(id);
    return EnsureDir(dir) ? dir : std::wstring();
}

// Deletes a folder and everything under it.
void RemoveTree(const std::wstring& path) {
    if (path.empty()) {
        return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(std::filesystem::path(path), ignored);
}

// The Downloads folder of the user profile.
std::wstring UserDownloadsDir() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &folder))) {
        return std::wstring();
    }
    std::wstring dir(folder);
    CoTaskMemFree(folder);
    return dir;
}

}  // namespace paths
