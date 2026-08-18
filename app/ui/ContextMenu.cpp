#include "ui/ContextMenu.h"

#include "ui/Commands.h"

// Builds the popup menu, tracks it synchronously and returns the selection.
int ShowDownloadsContextMenu(HWND owner, int x, int y) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_CTX_OPEN, L"Ouvrir");
    AppendMenuW(menu, MF_STRING, ID_CTX_OPEN_FOLDER, L"Ouvrir le dossier");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_FILE_START, L"Reprendre");
    AppendMenuW(menu, MF_STRING, ID_FILE_STOP, L"Arrêter");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_FILE_REMOVE, L"Supprimer");

    int command = static_cast<int>(TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, x, y, 0, owner, nullptr));

    DestroyMenu(menu);
    return command;
}
