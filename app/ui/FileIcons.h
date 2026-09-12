#pragma once

#include <string>

#include <windows.h>
#include <commctrl.h>

// The pictures Windows gives a file type, the very ones Explorer shows. They
// live in an image list owned by the shell: the application borrows indices
// into it and never destroys it.
namespace fileicons {

// The small image list of the shell, or null when it cannot be had.
HIMAGELIST SmallList();

// The index of the icon a file name deserves, whether or not the file is on
// disk; -1 when the shell has nothing to say. Answers from a cache kept by
// extension, so a repainted row costs nothing.
int IndexOf(const std::wstring& path);

// The side of one icon of that list, in pixels.
int Size();

}  // namespace fileicons
