#include "core/AddonStore.h"

#include <windows.h>

#include <fstream>

#include "core/Digest.h"
#include "core/Http.h"
#include "core/Paths.h"
#include "core/Text.h"
#include "third_party/json.hpp"

namespace {

// The store only ever answers to its author's repository: it serves native
// libraries, so the address is not something the user can point elsewhere.
constexpr char kIndexUrl[] =
    "https://raw.githubusercontent.com/goddivor/anime-dm-addons/repo-win32/index.min.json";

// Resolves a path of the index against the index address.
std::string Resolve(const std::string& indexUrl, const std::string& relative) {
    if (relative.rfind("http://", 0) == 0 || relative.rfind("https://", 0) == 0) {
        return relative;
    }
    size_t cut = indexUrl.find_last_of('/');
    return cut == std::string::npos ? relative : indexUrl.substr(0, cut + 1) + relative;
}

std::string Field(const nlohmann::json& object, const char* key) {
    auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool Flag(const nlohmann::json& object, const char* key) {
    auto it = object.find(key);
    return it != object.end() && it->is_boolean() && it->get<bool>();
}

std::optional<nlohmann::json> ReadJsonFile(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    nlohmann::json parsed = nlohmann::json::parse(file, nullptr, false);
    return parsed.is_discarded() ? std::nullopt : std::optional<nlohmann::json>(parsed);
}

bool WriteFile(const std::wstring& path, const void* data, size_t size) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return file.good();
}

// Removes a folder and everything it holds.
bool RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW found = {};
    HANDLE search = FindFirstFileW((dir + L"\\*").c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = found.cFileName;
            if (name == L"." || name == L"..") {
                continue;
            }
            std::wstring child = dir + L"\\" + name;
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveTree(child);
            } else {
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    return RemoveDirectoryW(dir.c_str()) != 0;
}

void Fail(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

}  // namespace

const char* AddonStore::IndexUrl() {
    return kIndexUrl;
}

std::wstring AddonStore::EntryDir(const std::string& id) const {
    std::wstring root = paths::AddonsDir();
    return root.empty() ? std::wstring() : root + L"\\" + Widen(id);
}

std::wstring AddonStore::LibraryPath(const std::string& id) const {
    std::wstring dir = EntryDir(id);
    return dir.empty() ? std::wstring() : dir + L"\\addon.dll";
}

std::wstring AddonStore::IconPath(const std::string& id) const {
    std::wstring dir = EntryDir(id);
    return dir.empty() ? std::wstring() : dir + L"\\icon.png";
}

// Reads the metadata written next to a library.
std::optional<InstalledAddon> AddonStore::Read(const std::string& id) const {
    std::wstring dir = EntryDir(id);
    if (dir.empty()) {
        return std::nullopt;
    }
    std::optional<nlohmann::json> meta = ReadJsonFile(dir + L"\\meta.json");
    if (!meta || !meta->is_object()) {
        return std::nullopt;
    }

    InstalledAddon addon;
    addon.id = Field(*meta, "id");
    addon.name = Field(*meta, "name");
    addon.lang = Field(*meta, "lang");
    addon.version = Field(*meta, "version");
    addon.nsfw = Flag(*meta, "nsfw");
    return addon.id.empty() ? std::nullopt : std::optional<InstalledAddon>(addon);
}

// Walks the registry folder, one subfolder per source.
std::vector<InstalledAddon> AddonStore::Installed() const {
    std::vector<InstalledAddon> out;
    std::wstring root = paths::AddonsDir();
    if (root.empty()) {
        return out;
    }

    WIN32_FIND_DATAW found = {};
    HANDLE search = FindFirstFileW((root + L"\\*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        std::wstring name = found.cFileName;
        if (name == L"." || name == L".." || !(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        if (std::optional<InstalledAddon> addon = Read(Narrow(name))) {
            out.push_back(*addon);
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return out;
}

// Deletes a source and everything it wrote.
bool AddonStore::Remove(const std::string& id) const {
    std::wstring dir = EntryDir(id);
    return !dir.empty() && RemoveTree(dir);
}

// Reads the settings the user chose for a source.
std::map<std::string, std::string> AddonStore::ReadConfig(const std::string& id) const {
    std::map<std::string, std::string> config;
    std::wstring dir = EntryDir(id);
    if (dir.empty()) {
        return config;
    }
    std::optional<nlohmann::json> stored = ReadJsonFile(dir + L"\\config.json");
    if (stored && stored->is_object()) {
        for (auto& [key, value] : stored->items()) {
            if (value.is_string()) {
                config[key] = value.get<std::string>();
            }
        }
    }
    return config;
}

// Writes the settings the user chose for a source.
bool AddonStore::WriteConfig(const std::string& id,
                             const std::map<std::string, std::string>& config) const {
    std::wstring dir = EntryDir(id);
    if (dir.empty() || !paths::EnsureDir(dir)) {
        return false;
    }
    std::string text = nlohmann::json(config).dump(2);
    return WriteFile(dir + L"\\config.json", text.data(), text.size());
}

// Reads the index and marks what is already installed.
std::vector<StoreEntry> AddonStore::Fetch(std::string* error) const {
    std::vector<StoreEntry> out;

    std::optional<std::string> body = http_.GetText(kIndexUrl);
    if (!body) {
        Fail(error, "the store could not be reached");
        return out;
    }

    nlohmann::json index = nlohmann::json::parse(*body, nullptr, false);
    if (index.is_discarded() || !index.is_object()) {
        Fail(error, "the store index is malformed");
        return out;
    }
    auto addons = index.find("addons");
    if (addons == index.end() || !addons->is_array()) {
        Fail(error, "the store index holds no addon");
        return out;
    }

    std::map<std::string, InstalledAddon> installed;
    for (const InstalledAddon& addon : Installed()) {
        installed[addon.id] = addon;
    }

    for (const nlohmann::json& item : *addons) {
        if (!item.is_object()) {
            continue;
        }
        StoreEntry entry;
        entry.id = Field(item, "id");
        if (entry.id.empty()) {
            continue;
        }
        entry.name = Field(item, "name");
        entry.lang = Field(item, "lang");
        entry.version = Field(item, "version");
        entry.library = Field(item, "dll");
        entry.icon = Field(item, "icon");
        entry.sha256 = Field(item, "sha256");
        entry.nsfw = Flag(item, "nsfw");
        auto abi = item.find("abi");
        entry.abi = abi != item.end() && abi->is_number_unsigned() ? abi->get<uint32_t>() : 0;

        auto known = installed.find(entry.id);
        entry.installed = known != installed.end();
        if (entry.installed) {
            entry.installedVersion = known->second.version;
        }
        out.push_back(std::move(entry));
    }
    return out;
}

// Downloads a library, checks its digest, then writes it with its metadata.
std::optional<InstalledAddon> AddonStore::Install(const StoreEntry& entry,
                                                  std::string* error) const {
    if (entry.library.empty()) {
        Fail(error, "the entry names no library");
        return std::nullopt;
    }

    std::optional<std::vector<uint8_t>> library =
        http_.GetBytes(Resolve(kIndexUrl, entry.library));
    if (!library || library->empty()) {
        Fail(error, "the library could not be downloaded");
        return std::nullopt;
    }

    // A native library runs with the user's rights: it is never written to disk
    // before its digest matches what the index announces.
    if (entry.sha256.empty()) {
        Fail(error, "the entry announces no digest");
        return std::nullopt;
    }
    if (digest::Sha256Hex(*library) != entry.sha256) {
        Fail(error, "the library does not match its digest");
        return std::nullopt;
    }

    std::wstring dir = EntryDir(entry.id);
    if (dir.empty() || !paths::EnsureDir(dir)) {
        Fail(error, "the installation folder could not be created");
        return std::nullopt;
    }
    if (!WriteFile(dir + L"\\addon.dll", library->data(), library->size())) {
        Fail(error, "the library could not be written");
        return std::nullopt;
    }

    if (!entry.icon.empty()) {
        if (std::optional<std::vector<uint8_t>> icon =
                http_.GetBytes(Resolve(kIndexUrl, entry.icon))) {
            WriteFile(dir + L"\\icon.png", icon->data(), icon->size());
        }
    }

    InstalledAddon addon;
    addon.id = entry.id;
    addon.name = entry.name;
    addon.lang = entry.lang;
    addon.version = entry.version;
    addon.nsfw = entry.nsfw;

    nlohmann::json meta = {{"id", addon.id},
                           {"name", addon.name},
                           {"lang", addon.lang},
                           {"version", addon.version},
                           {"nsfw", addon.nsfw}};
    std::string text = meta.dump(2);
    if (!WriteFile(dir + L"\\meta.json", text.data(), text.size())) {
        Fail(error, "the metadata could not be written");
        return std::nullopt;
    }
    return addon;
}
