#pragma once

#include <string>
#include <vector>

// What the user chose once and expects to find again, kept in `settings.json`.
struct Settings {
    bool folderIcons = false;
    std::string folderTemplate = "none";  // the id of a folder-icon recipe
    bool aniyomi = false;                 // write cover.jpg and .nomedia in each folder
    std::string theme = "system";         // "dark", "light" or "system"
    std::string language = "fr";          // "fr" or "en"
    std::string toolbarSkin;              // a skin name, "fluent" for the icon font, empty for the default pack
    bool clipboardUrl = true;             // paste the link of the clipboard into the add window
    bool closeToTray = true;              // the close box hides the window beside the clock
    bool rememberPath = false;            // reuse the folder below for the next downloads
    std::string savePath;                 // that folder, in UTF-8
    bool startWithWindows = false;        // run when the user signs in
    std::vector<std::string> browsers;    // the ids of the browsers the host is declared to
    int maxRunning = 3;                   // videos downloaded at once
    int connections = 8;                  // connections one video may open
    std::string panelMode = "full";       // the browser panel, "full" with its label or "mini"
    bool panelOnPage = true;              // show it on the page of an anime or an episode
    bool panelOnLinks = true;             // show it over the links that lead to one
    std::vector<int> columnWidths;        // the file list's columns, empty for their defaults
    std::vector<int> columns;             // the columns shown, left to right, empty for all
    std::string fontFace;                 // the font of the interface, empty for the system's
    int fontSize = 0;                     // its size, in tenths of a point
    int fontWeight = 400;
    bool fontItalic = false;
    int windowX = 0;                      // the restored frame of the main window, in pixels,
    int windowY = 0;                      // a zero width for the default one
    int windowWidth = 0;
    int windowHeight = 0;
    bool windowMaximized = false;
    int sidebarWidth = 0;                 // the categories panel, zero for its default width
    bool sidebarVisible = true;
};

namespace settings {

// The bounds of the two limits of the download engine.
constexpr int kMinRunning = 1;
constexpr int kMaxRunning = 10;
constexpr int kMinConnections = 1;
constexpr int kMaxConnections = 16;

Settings Load();
void Save(const Settings& settings);

}  // namespace settings
