#pragma once

#include <string>

// Everything the application keeps between sessions lives under one folder of
// the user profile.
namespace paths {

// `%APPDATA%\anime-dm`, created on demand. Empty when it cannot be created.
std::wstring DataDir();

// `<data>/addons`, one subfolder per installed source.
std::wstring AddonsDir();

// `<data>/settings.json`.
std::wstring SettingsFile();

// Creates a folder and every missing parent.
bool EnsureDir(const std::wstring& path);

}  // namespace paths
