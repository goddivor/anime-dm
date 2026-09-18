#include "core/Bridge.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>

#include "core/Addon.h"
#include "core/AddonStore.h"
#include "core/BridgeProtocol.h"
#include "core/Paths.h"
#include "core/Text.h"
#include "third_party/json.hpp"

namespace {

constexpr wchar_t kHostExe[] = L"adm-host.exe";

// The folder of the running executable.
std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring file(path);
    size_t cut = file.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring() : file.substr(0, cut);
}

// Writes a text file whole; false when it cannot.
bool WriteText(const std::wstring& path, const std::string& text) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << text;
    return true;
}

// Sets a string value of a key under HKCU, creating the key; an empty name
// sets the default value.
void SetRegistryString(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    HKEY handle = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &handle, nullptr) != ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle);
}

void SetRegistryDefault(const std::wstring& key, const std::wstring& value) {
    SetRegistryString(key, nullptr, value);
}

// Removes one value of a key under HKCU, when it exists.
void DeleteRegistryValue(const std::wstring& key, const wchar_t* name) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_SET_VALUE, &handle) != ERROR_SUCCESS) {
        return;
    }
    RegDeleteValueW(handle, name);
    RegCloseKey(handle);
}

bool IsFile(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Removes a key under HKCU, when it exists.
void DeleteRegistryKey(const std::wstring& key) {
    RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
}

// The key of each browser under HKCU: `<key>\NativeMessagingHosts` is where
// it looks for hosts, `<key>\Extensions` where it reads the extensions to
// install. Firefox reads its own kind of manifest, and takes its extensions
// as values under `Software\Mozilla\Firefox\Extensions`.
struct KnownBrowser {
    bridge::Browser browser;
    const wchar_t* key;
    bool firefox;
};
const KnownBrowser kBrowsers[] = {
    {{"chrome", L"Google Chrome"}, L"Software\\Google\\Chrome", false},
    {{"edge", L"Microsoft Edge"}, L"Software\\Microsoft\\Edge", false},
    {{"brave", L"Brave"}, L"Software\\BraveSoftware\\Brave-Browser", false},
    {{"chromium", L"Chromium"}, L"Software\\Chromium", false},
    {{"vivaldi", L"Vivaldi"}, L"Software\\Vivaldi", false},
    {{"opera", L"Opera"}, L"Software\\Opera Software", false},
    {{"firefox", L"Mozilla Firefox"}, L"Software\\Mozilla", true},
};
constexpr wchar_t kFirefoxInstallKey[] = L"Software\\Mozilla\\Firefox\\Extensions";

// Asks a browser to install the extension, or takes the request back. A
// Chromium browser installs from the Web Store at its next start and asks
// the user to enable it; Firefox does the same with the signed package,
// which is only declared when it ships with the application.
void RegisterExtension(const KnownBrowser& known, const std::wstring& exe, bool wanted) {
    if (known.firefox) {
        std::wstring package = exe + L"\\" + bridge::kFirefoxPackage;
        std::wstring id = Widen(bridge::kFirefoxExtensionId);
        if (wanted && IsFile(package)) {
            SetRegistryString(kFirefoxInstallKey, id.c_str(), package);
        } else {
            DeleteRegistryValue(kFirefoxInstallKey, id.c_str());
        }
        return;
    }
    std::wstring key =
        std::wstring(known.key) + L"\\Extensions\\" + Widen(bridge::kChromiumExtensionId);
    if (wanted) {
        SetRegistryString(key, L"update_url", bridge::kChromiumUpdateUrl);
    } else {
        DeleteRegistryKey(key);
    }
}

}  // namespace

namespace bridge {

// Writes what the extension needs to know about the sources and the panel.
void WriteSources(const AddonStore& store, Http& http, const Panel& panel) {
    nlohmann::json sources = nlohmann::json::array();
    for (const InstalledAddon& installed : store.Installed()) {
        AddonMetadata meta;
        std::unique_ptr<Addon> addon =
            Addon::Load(store.LibraryPath(installed.id), http, store.ReadConfig(installed.id));
        if (addon) {
            meta = addon->Meta();
        }
        sources.push_back({{"id", installed.id},
                           {"name", installed.name},
                           {"lang", installed.lang},
                           {"site", meta.baseUrl},
                           {"animePattern", meta.animePattern},
                           {"episodePattern", meta.episodePattern},
                           {"animeFromEpisode", meta.animeFromEpisode}});
    }
    std::wstring path = paths::SourcesFile();
    if (!path.empty()) {
        nlohmann::json root = {
            {"sources", sources},
            {"panel", {{"mode", panel.mode}, {"onPage", panel.onPage}, {"onLinks", panel.onLinks}}},
        };
        WriteText(path, root.dump(2));
    }
}

// Every browser the application knows.
const std::vector<Browser>& Browsers() {
    static const std::vector<Browser> browsers = [] {
        std::vector<Browser> list;
        for (const KnownBrowser& known : kBrowsers) {
            list.push_back(known.browser);
        }
        return list;
    }();
    return browsers;
}

// The ids of every browser.
std::vector<std::string> AllBrowsers() {
    std::vector<std::string> ids;
    for (const KnownBrowser& known : kBrowsers) {
        ids.push_back(known.browser.id);
    }
    return ids;
}

// Declares the host to the chosen browsers and withdraws it from the others.
void RegisterHost(const std::vector<std::string>& enabled) {
    std::wstring dir = paths::HostDir();
    std::wstring exe = ExeDir();
    if (dir.empty() || exe.empty()) {
        return;
    }
    std::string hostPath = Narrow(exe + L"\\" + kHostExe);
    std::string name = kHostName;

    // The Chromium family names the extension by its origin, Firefox by its id.
    nlohmann::json chromium = {
        {"name", name},
        {"description", "Anime Download Manager"},
        {"path", hostPath},
        {"type", "stdio"},
        {"allowed_origins",
         {std::string("chrome-extension://") + kChromiumExtensionId + "/"}},
    };
    nlohmann::json firefox = {
        {"name", name},
        {"description", "Anime Download Manager"},
        {"path", hostPath},
        {"type", "stdio"},
        {"allowed_extensions", {std::string(kFirefoxExtensionId)}},
    };

    std::wstring chromiumFile = dir + L"\\" + Widen(name) + L".json";
    std::wstring firefoxFile = dir + L"\\" + Widen(name) + L".firefox.json";
    if (!WriteText(chromiumFile, chromium.dump(2)) || !WriteText(firefoxFile, firefox.dump(2))) {
        return;
    }
    for (const KnownBrowser& known : kBrowsers) {
        std::wstring key =
            std::wstring(known.key) + L"\\NativeMessagingHosts\\" + Widen(name);
        bool wanted = std::find(enabled.begin(), enabled.end(), known.browser.id) != enabled.end();
        if (wanted) {
            SetRegistryDefault(key, known.firefox ? firefoxFile : chromiumFile);
        } else {
            DeleteRegistryKey(key);
        }
        RegisterExtension(known, exe, wanted);
    }
}

// Opens the store page in whatever browser the user made the default.
void OpenStorePage() {
    ShellExecuteW(nullptr, L"open", kChromiumStoreUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace bridge
