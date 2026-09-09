#include "core/Settings.h"

#include <windows.h>

#include <filesystem>
#include <fstream>

#include "core/Paths.h"
#include "third_party/json.hpp"

namespace settings {

// Reads the file back; the defaults stand in for whatever is missing.
Settings Load() {
    Settings settings;
    std::wstring path = paths::SettingsFile();
    if (path.empty()) {
        return settings;
    }
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return settings;
    }
    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (!root.is_object()) {
        return settings;
    }
    settings.folderIcons = root.value("folderIcons", settings.folderIcons);
    settings.folderTemplate = root.value("folderTemplate", settings.folderTemplate);
    settings.aniyomi = root.value("aniyomiAdapt", settings.aniyomi);
    settings.theme = root.value("theme", settings.theme);
    settings.language = root.value("lang", settings.language);
    if (settings.folderTemplate.empty()) {
        settings.folderTemplate = "none";
    }
    return settings;
}

// Writes the file, whole.
void Save(const Settings& settings) {
    std::wstring path = paths::SettingsFile();
    if (path.empty()) {
        return;
    }
    nlohmann::json root = {
        {"folderIcons", settings.folderIcons},
        {"folderTemplate", settings.folderTemplate},
        {"aniyomiAdapt", settings.aniyomi},
        {"theme", settings.theme},
        {"lang", settings.language},
    };
    std::wstring temp = path + L".tmp";
    {
        std::ofstream file(std::filesystem::path(temp), std::ios::binary | std::ios::trunc);
        if (!file) {
            return;
        }
        file << root.dump(2);
    }
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

}  // namespace settings
