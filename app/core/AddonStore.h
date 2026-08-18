#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

class Http;

// One entry of the store index.
struct StoreEntry {
    std::string id;
    std::string name;
    std::string lang;
    std::string version;
    std::string library;  // path of the library, relative to the index
    std::string icon;     // path of the icon, relative to the index
    std::string sha256;
    uint32_t abi = 0;
    bool nsfw = false;
    bool installed = false;
    std::string installedVersion;
};

// One source installed on disk.
struct InstalledAddon {
    std::string id;
    std::string name;
    std::string lang;
    std::string version;
    bool nsfw = false;
};

// The registry of installed sources and the store they come from.
//
// The index URL is fixed in the application on purpose: the store serves native
// libraries, so it only ever answers to its author's repository.
class AddonStore {
public:
    explicit AddonStore(Http& http) : http_(http) {}

    static const char* IndexUrl();

    // --- registry on disk ---
    std::vector<InstalledAddon> Installed() const;
    std::optional<InstalledAddon> Read(const std::string& id) const;
    bool Remove(const std::string& id) const;
    std::wstring LibraryPath(const std::string& id) const;
    std::wstring IconPath(const std::string& id) const;
    std::map<std::string, std::string> ReadConfig(const std::string& id) const;
    bool WriteConfig(const std::string& id, const std::map<std::string, std::string>& config) const;

    // --- store ---
    // Reads the index and marks what is already installed.
    std::vector<StoreEntry> Fetch(std::string* error) const;
    // Reads the icon of an entry: from disk when installed, from the store
    // otherwise. Empty when it has none.
    std::vector<uint8_t> IconBytes(const StoreEntry& entry) const;

    // Downloads a library, checks its digest, then writes it with its metadata.
    std::optional<InstalledAddon> Install(const StoreEntry& entry, std::string* error) const;

private:
    std::wstring EntryDir(const std::string& id) const;

    Http& http_;
};
