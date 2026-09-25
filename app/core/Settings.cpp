#include "core/Settings.h"

#include <windows.h>

#include <filesystem>
#include <fstream>

#include "core/Bridge.h"
#include "core/Paths.h"
#include "third_party/json.hpp"

namespace {

// Keeps a limit between its bounds.
int Clamp(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

}  // namespace

namespace settings {

// Reads the file back; the defaults stand in for whatever is missing.
Settings Load() {
    Settings settings;
    settings.browsers = bridge::AllBrowsers();
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
    settings.toolbarSkin = root.value("toolbarSkin", settings.toolbarSkin);
    settings.clipboardUrl = root.value("clipboardUrl", settings.clipboardUrl);
    settings.rememberPath = root.value("rememberPath", settings.rememberPath);
    settings.savePath = root.value("savePath", settings.savePath);
    settings.startWithWindows = root.value("startWithWindows", settings.startWithWindows);
    if (root.contains("browsers") && root["browsers"].is_array()) {
        settings.browsers.clear();
        for (const nlohmann::json& id : root["browsers"]) {
            if (id.is_string()) {
                settings.browsers.push_back(id.get<std::string>());
            }
        }
    }
    if (root.contains("columnWidths") && root["columnWidths"].is_array()) {
        for (const nlohmann::json& width : root["columnWidths"]) {
            settings.columnWidths.push_back(width.is_number_integer() ? width.get<int>() : 0);
        }
    }
    if (root.contains("columns") && root["columns"].is_array()) {
        for (const nlohmann::json& column : root["columns"]) {
            if (column.is_number_integer()) {
                settings.columns.push_back(column.get<int>());
            }
        }
    }
    settings.fontFace = root.value("fontFace", settings.fontFace);
    settings.fontSize = root.value("fontSize", settings.fontSize);
    settings.fontWeight = root.value("fontWeight", settings.fontWeight);
    settings.fontItalic = root.value("fontItalic", settings.fontItalic);
    if (settings.fontSize < 60 || settings.fontSize > 360) {
        settings.fontFace.clear();
    }
    settings.panelMode = root.value("panelMode", settings.panelMode);
    settings.panelOnPage = root.value("panelOnPage", settings.panelOnPage);
    settings.panelOnLinks = root.value("panelOnLinks", settings.panelOnLinks);
    if (settings.panelMode != "mini") {
        settings.panelMode = "full";
    }
    settings.maxRunning = Clamp(root.value("maxRunning", settings.maxRunning), kMinRunning, kMaxRunning);
    settings.connections =
        Clamp(root.value("connections", settings.connections), kMinConnections, kMaxConnections);
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
        {"toolbarSkin", settings.toolbarSkin},
        {"clipboardUrl", settings.clipboardUrl},
        {"rememberPath", settings.rememberPath},
        {"savePath", settings.savePath},
        {"startWithWindows", settings.startWithWindows},
        {"browsers", settings.browsers},
        {"maxRunning", settings.maxRunning},
        {"connections", settings.connections},
        {"panelMode", settings.panelMode},
        {"panelOnPage", settings.panelOnPage},
        {"panelOnLinks", settings.panelOnLinks},
        {"columnWidths", settings.columnWidths},
        {"columns", settings.columns},
        {"fontFace", settings.fontFace},
        {"fontSize", settings.fontSize},
        {"fontWeight", settings.fontWeight},
        {"fontItalic", settings.fontItalic},
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
