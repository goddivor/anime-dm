#pragma once

#include <windows.h>

#include <string>

// One kind of file a picker offers, as "Fichier JSON" and "json".
struct FileKind {
    const wchar_t* label;
    const wchar_t* extension;
};

// Asks where to save a file of the kind, with a suggested name; empty when
// the user gave up.
std::wstring PickSaveFile(HWND owner, const FileKind& kind, const std::wstring& suggested);

// Asks for a file of the kind to open; empty when the user gave up.
std::wstring PickOpenFile(HWND owner, const FileKind& kind);
