cmake_minimum_required(VERSION 3.20)

project(anime-dm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "anime-dm (C++) targets the Win32 API and is Windows-only.")
endif()

add_executable(anime-dm WIN32
    app/main.cpp
    app/core/Addon.cpp
    app/core/AddonStore.cpp
    app/core/Cipher.cpp
    app/core/Digest.cpp
    app/core/Download.cpp
    app/core/Downloader.cpp
    app/core/Playlist.cpp
    app/core/Queue.cpp
    app/core/Transfer.cpp
    app/core/Http.cpp
    app/core/Image.cpp
    app/core/Paths.cpp
    app/core/Text.cpp
    app/ui/ConfirmDialog.cpp
    app/ui/Format.cpp
    app/ui/MainWindow.cpp
    app/ui/MenuBar.cpp
    app/ui/Paint.cpp
    app/ui/PosterDialog.cpp
    app/ui/Toolbar.cpp
    app/ui/IconFactory.cpp
    app/ui/Sidebar.cpp
    app/ui/AddonConfigDialog.cpp
    app/ui/AddonsDialog.cpp
    app/ui/ContextMenu.cpp
    app/ui/AddDialog.cpp
    app/ui/AddSelection.cpp
    app/ui/HelpDialogs.cpp
    app/ui/NoticeDialog.cpp
    app/ui/SearchDialog.cpp
    app/ui/Strings.cpp
    app/ui/Theme.cpp
    app/ui/SettingsDialog.cpp
    app/ui/DownloadsView.cpp
    app/resources/app.rc
)

target_include_directories(anime-dm PRIVATE app)

target_compile_definitions(anime-dm PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(anime-dm PRIVATE /W4 /utf-8 /permissive-)
endif()

target_link_libraries(anime-dm PRIVATE comctl32 gdiplus dwmapi uxtheme winhttp bcrypt shlwapi ole32 msimg32)

# MinGW needs -municode to accept wWinMain as the entry point; MSVC detects it on its own.
if(MINGW)
    target_compile_options(anime-dm PRIVATE -municode)
    target_link_options(anime-dm PRIVATE -municode)
endif()

# Console tool that exercises the addon ABI without the application.
add_executable(addon-smoke
    tools/addon_smoke.cpp
    app/core/Addon.cpp
    app/core/Http.cpp
    app/core/Text.cpp
)

target_include_directories(addon-smoke PRIVATE app)

target_compile_definitions(addon-smoke PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(addon-smoke PRIVATE winhttp)

# Console tool that runs the download engine on one episode.
add_executable(download-smoke
    tools/download_smoke.cpp
    app/core/Addon.cpp
    app/core/AddonStore.cpp
    app/core/Cipher.cpp
    app/core/Digest.cpp
    app/core/Download.cpp
    app/core/Downloader.cpp
    app/core/Http.cpp
    app/core/Paths.cpp
    app/core/Playlist.cpp
    app/core/Text.cpp
    app/core/Transfer.cpp
)

target_include_directories(download-smoke PRIVATE app)

target_compile_definitions(download-smoke PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(download-smoke PRIVATE winhttp bcrypt ole32 shell32)

if(MINGW)
    target_compile_options(download-smoke PRIVATE -municode)
    target_link_options(download-smoke PRIVATE -municode)
endif()
