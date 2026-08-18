#include "ui/ContextMenu.h"

#include "ui/Commands.h"
#include "ui/Strings.h"

// Builds the popup menu, tracks it synchronously and returns the selection.
int ShowDownloadsContextMenu(HWND owner, int x, int y) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_CTX_OPEN, Str(STR_CTX_OPEN));
    AppendMenuW(menu, MF_STRING, ID_CTX_OPEN_FOLDER, Str(STR_CTX_OPEN_FOLDER));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_FILE_START, Str(STR_TB_RESUME));
    AppendMenuW(menu, MF_STRING, ID_FILE_STOP, Str(STR_TB_STOP));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_FILE_REMOVE, Str(STR_TB_REMOVE));

    int command = static_cast<int>(TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, x, y, 0, owner, nullptr));

    DestroyMenu(menu);
    return command;
}
