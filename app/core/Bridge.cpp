#include "core/Bridge.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
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

// Sets the default value of a key under HKCU, creating the key.
void SetRegistryDefault(const std::wstring& key, const std::wstring& value) {
    HKEY handle = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &handle, nullptr) != ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(handle, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle);
}

// Where each browser family looks for native hosts, under HKCU.
const wchar_t* const kChromiumKeys[] = {
    L"Software\\Google\\Chrome\\NativeMessagingHosts",
    L"Software\\Microsoft\\Edge\\NativeMessagingHosts",
    L"Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts",
    L"Software\\Chromium\\NativeMessagingHosts",
    L"Software\\Vivaldi\\NativeMessagingHosts",
    L"Software\\Opera Software\\NativeMessagingHosts",
};
constexpr wchar_t kFirefoxKey[] = L"Software\\Mozilla\\NativeMessagingHosts";

}  // namespace

namespace bridge {

// Writes the id, name, language and site of every installed source.
void WriteSources(const AddonStore& store, Http& http) {
    nlohmann::json sources = nlohmann::json::array();
    for (const InstalledAddon& installed : store.Installed()) {
        std::string site;
        std::unique_ptr<Addon> addon =
            Addon::Load(store.LibraryPath(installed.id), http, store.ReadConfig(installed.id));
        if (addon) {
            site = addon->Meta().baseUrl;
        }
        sources.push_back({{"id", installed.id},
                           {"name", installed.name},
                           {"lang", installed.lang},
                           {"site", site}});
    }
    std::wstring path = paths::SourcesFile();
    if (!path.empty()) {
        WriteText(path, nlohmann::json({{"sources", sources}}).dump(2));
    }
}

// Declares the native messaging host to every browser that reads the registry.
void RegisterHost() {
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
    for (const wchar_t* key : kChromiumKeys) {
        SetRegistryDefault(std::wstring(key) + L"\\" + Widen(name), chromiumFile);
    }
    SetRegistryDefault(std::wstring(kFirefoxKey) + L"\\" + Widen(name), firefoxFile);
}

}  // namespace bridge
