#pragma once

#include <string>

// What the user chose once and expects to find again, kept in `settings.json`.
struct Settings {
    bool folderIcons = false;
    std::string folderTemplate = "none";  // the id of a folder-icon recipe
    bool aniyomi = false;                 // write cover.jpg and .nomedia in each folder
    std::string theme = "system";         // "dark", "light" or "system"
    std::string language = "fr";          // "fr" or "en"
    std::string toolbarSkin;              // the name of an IDM skin; empty for the icon font
};

namespace settings {

Settings Load();
void Save(const Settings& settings);

}  // namespace settings
