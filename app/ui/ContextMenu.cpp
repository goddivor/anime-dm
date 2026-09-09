#include "ui/ContextMenu.h"

#include "ui/Commands.h"
#include "ui/Strings.h"
#include "ui/TemplateNames.h"

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

// Builds the anime menu, tracks it synchronously and returns the selection.
int ShowAnimeContextMenu(HWND owner, int x, int y, const AnimeMenuOptions& options) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_ANIME_OPEN, Str(STR_CTX_OPEN));
    AppendMenuW(menu, MF_STRING, ID_ANIME_OPEN_FOLDER, Str(STR_CTX_OPEN_FOLDER));
    if (options.offerAniyomi) {
        AppendMenuW(menu, MF_STRING, ID_ANIME_ANIYOMI, Str(STR_ANIME_ANIYOMI));
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU templates = CreatePopupMenu();
    for (size_t i = 0; i < options.templates.size(); ++i) {
        UINT flags = MF_STRING;
        if (options.templates[i] == options.currentTemplate) {
            flags |= MF_CHECKED;
        }
        AppendMenuW(templates, flags, ID_ICON_TEMPLATE_FIRST + static_cast<UINT_PTR>(i),
                    Str(TemplateName(options.templates[i])));
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(templates), Str(STR_ICON_CHANGE));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_ANIME_DELETE, Str(STR_ANIME_DELETE));

    int command = static_cast<int>(TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, x, y, 0, owner, nullptr));

    DestroyMenu(menu);
    return command;
}
