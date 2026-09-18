#include "ui/ContextMenu.h"

#include "core/Text.h"
#include "ui/Commands.h"
#include "ui/Strings.h"
#include "ui/TemplateNames.h"

// Builds the popup menu, tracks it synchronously and returns the selection.
// Resume opens on the players of the episode: picking one tells the engine
// which to try, since the one chosen at first may have nothing to give, or
// only a truncated copy; a finished episode can be fetched again this way.
int ShowDownloadsContextMenu(HWND owner, int x, int y, const DownloadMenuOptions& options) {
    // An entry that cannot act on the selection is greyed, as on the toolbar.
    auto when = [](bool enabled) { return enabled ? MF_STRING : (MF_STRING | MF_GRAYED); };

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, when(options.canOpen), ID_CTX_OPEN, Str(STR_CTX_OPEN));
    AppendMenuW(menu, MF_STRING, ID_CTX_OPEN_FOLDER, Str(STR_CTX_OPEN_FOLDER));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU players = CreatePopupMenu();
    AppendMenuW(players, options.currentPlayer.empty() ? (MF_STRING | MF_CHECKED) : MF_STRING,
                ID_CTX_PLAYER_AUTO, Str(STR_CTX_PLAYER_AUTO));
    AppendMenuW(players, MF_SEPARATOR, 0, nullptr);
    if (options.players.empty()) {
        AppendMenuW(players, MF_STRING | MF_GRAYED, 0, Str(STR_CTX_PLAYERS_UNKNOWN));
    }
    for (size_t i = 0; i < options.players.size(); ++i) {
        UINT flags = MF_STRING;
        if (options.players[i] == options.currentPlayer) {
            flags |= MF_CHECKED;
        }
        AppendMenuW(players, flags, ID_PLAYER_FIRST + static_cast<UINT_PTR>(i),
                    Widen(options.players[i]).c_str());
    }
    AppendMenuW(menu, MF_POPUP | (options.canResume ? 0 : MF_GRAYED),
                reinterpret_cast<UINT_PTR>(players), Str(STR_TB_RESUME));
    AppendMenuW(menu, when(options.canStop), ID_FILE_STOP, Str(STR_TB_STOP));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU queues = CreatePopupMenu();
    AppendMenuW(queues, options.inScheduler ? MF_STRING : (MF_STRING | MF_CHECKED),
                ID_CTX_QUEUE_MAIN, Str(STR_QUEUE_MAIN));
    AppendMenuW(queues, options.inScheduler ? (MF_STRING | MF_CHECKED) : MF_STRING,
                ID_CTX_QUEUE_SCHEDULER, Str(STR_QUEUE_SCHEDULER));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(queues), Str(STR_CTX_QUEUE));
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
    AppendMenuW(menu, MF_STRING | (options.followed ? MF_CHECKED : 0), ID_ANIME_FOLLOW,
                Str(STR_ANIME_FOLLOW));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_ANIME_DELETE, Str(STR_ANIME_DELETE));

    int command = static_cast<int>(TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, x, y, 0, owner, nullptr));

    DestroyMenu(menu);
    return command;
}
