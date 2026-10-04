#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Gives the folder of an anime an icon made from its poster, the way the Tauri
// application did: the same ImageMagick recipes, the same layer images, an
// `.ico` cached by poster and recipe, then `desktop.ini` and the attributes
// Explorer expects.
namespace foldericon {

// Why an icon could not be applied.
enum class Error {
    None,
    NoMagick,         // ImageMagick is not installed and not bundled
    NoAssets,         // the layer images are missing next to the executable
    UnknownTemplate,
    Render,           // ImageMagick refused; `detail` carries what it said
    Disk,
};

// The ids of the recipes, in the order the interface lists them.
std::vector<std::string> TemplateIds();

// Where ImageMagick is, empty when it cannot be found.
std::wstring MagickPath();

// Composes the icon of a poster with a recipe and applies it to a folder,
// which is created when missing.
Error Apply(const std::wstring& folder, const std::vector<uint8_t>& poster,
            const std::string& templateId, std::string* detail);

// Writes what Aniyomi's local source reads: cover.jpg and an empty .nomedia.
bool AdaptForAniyomi(const std::wstring& folder, const std::vector<uint8_t>& poster);

// Whether a folder already carries the Aniyomi files.
bool HasAniyomiFiles(const std::wstring& folder);

// Makes the icon of a folder dressed by an earlier version follow the folder
// when it is renamed, moved or copied; a folder already right is left alone.
void Repair(const std::wstring& folder);

// Removes a folder that holds nothing but what dresses it (icon, desktop.ini,
// the Aniyomi files); true when the folder is gone. A folder that holds
// anything else is left untouched.
bool RemoveIfBare(const std::wstring& folder);

}  // namespace foldericon
