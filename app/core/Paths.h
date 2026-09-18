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

// `<data>/downloads.json`, the queue between two sessions.
std::wstring DownloadsFile();

// `<data>/sources.json`, what the browser extension knows of the sources.
std::wstring SourcesFile();

// `<data>/schedule.json`, when the queues start and stop on their own.
std::wstring ScheduleFile();

// `<data>/follows.json`, the animes whose new episodes are fetched on their own.
std::wstring FollowsFile();

// `<data>/host`, the manifests the browsers read to find the native host.
std::wstring HostDir();

// `<data>/posters`, one image per anime of the queue.
std::wstring PostersDir();

// `<data>/parts/<id>`, where the pieces of one download wait to be assembled.
std::wstring PartsDir(unsigned long long id);

// Deletes a folder and everything under it.
void RemoveTree(const std::wstring& path);

// The Downloads folder of the user profile.
std::wstring UserDownloadsDir();

// Creates a folder and every missing parent.
bool EnsureDir(const std::wstring& path);

}  // namespace paths
