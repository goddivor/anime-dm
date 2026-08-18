#include "ui/MenuBar.h"

#include "ui/Commands.h"
#include "ui/Strings.h"

namespace {

struct Entry {
    int command;
    const wchar_t* text;
};

// Appends a flat list of entries, a zero command standing for a separator.
void AppendEntries(HMENU menu, const Entry* entries, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].command == 0) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        } else {
            AppendMenuW(menu, MF_STRING, entries[i].command, entries[i].text);
        }
    }
}

// Builds a popup from a flat list of entries.
HMENU BuildPopup(const Entry* entries, size_t count) {
    HMENU menu = CreatePopupMenu();
    AppendEntries(menu, entries, count);
    return menu;
}

// Appends a nested popup under the given label.
void AppendSubMenu(HMENU parent, StringId label, const Entry* entries, size_t count) {
    HMENU sub = BuildPopup(entries, count);
    AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), Str(label));
}

// Builds the tasks drop-down.
HMENU BuildTasksMenu() {
    const Entry head[] = {
        {ID_TASK_ADD, Str(STR_TASK_ADD)},
        {ID_TASK_MANUAL, Str(STR_TASK_MANUAL)},
        {ID_TASK_BATCH, Str(STR_TASK_BATCH)},
        {0, nullptr},
    };
    const Entry exports[] = {
        {ID_TASK_EXPORT_ADM, Str(STR_EXPORT_ADM)},
        {ID_TASK_EXPORT_TXT, Str(STR_EXPORT_TXT)},
        {ID_TASK_EXPORT_JSON, Str(STR_EXPORT_JSON)},
        {ID_TASK_EXPORT_SHEET, Str(STR_EXPORT_SHEET)},
    };
    const Entry imports[] = {
        {ID_TASK_IMPORT_ADM, Str(STR_IMPORT_ADM)},
        {ID_TASK_IMPORT_TXT, Str(STR_IMPORT_TXT)},
        {ID_TASK_IMPORT_JSON, Str(STR_IMPORT_JSON)},
        {ID_TASK_IMPORT_SHEET, Str(STR_IMPORT_SHEET)},
    };

    HMENU menu = BuildPopup(head, ARRAYSIZE(head));
    AppendSubMenu(menu, STR_TASK_EXPORT, exports, ARRAYSIZE(exports));
    AppendSubMenu(menu, STR_TASK_IMPORT, imports, ARRAYSIZE(imports));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TASK_QUIT, Str(STR_TASK_QUIT));
    return menu;
}

// Builds the file drop-down.
HMENU BuildFileMenu() {
    const Entry entries[] = {
        {ID_FILE_START, Str(STR_FILE_START)},
        {ID_FILE_STOP, Str(STR_FILE_STOP)},
        {ID_FILE_REDOWNLOAD, Str(STR_FILE_REDOWNLOAD)},
        {0, nullptr},
        {ID_FILE_REMOVE, Str(STR_FILE_REMOVE)},
    };
    return BuildPopup(entries, ARRAYSIZE(entries));
}

// Builds the downloads drop-down.
HMENU BuildDownloadMenu() {
    const Entry head[] = {
        {ID_DOWNLOAD_STOP_ALL, Str(STR_DL_STOP_ALL)},
        {ID_DOWNLOAD_REMOVE_COMPLETED, Str(STR_DL_REMOVE_COMPLETED)},
        {ID_DOWNLOAD_DELETE_ALL, Str(STR_DL_DELETE_ALL)},
        {ID_DOWNLOAD_SEARCH, Str(STR_DL_SEARCH)},
        {0, nullptr},
        {ID_DOWNLOAD_SCHEDULE, Str(STR_DL_SCHEDULE)},
    };
    const Entry startQueue[] = {
        {ID_QUEUE_START_MAIN, Str(STR_QUEUE_MAIN)},
        {ID_QUEUE_START_SCHEDULER, Str(STR_QUEUE_SCHEDULER)},
    };
    const Entry stopQueue[] = {
        {ID_QUEUE_STOP_MAIN, Str(STR_QUEUE_MAIN)},
        {ID_QUEUE_STOP_SCHEDULER, Str(STR_QUEUE_SCHEDULER)},
    };
    const Entry limiter[] = {
        {ID_LIMITER_ENABLE, Str(STR_LIMITER_ENABLE)},
        {ID_LIMITER_DISABLE, Str(STR_LIMITER_DISABLE)},
        {ID_LIMITER_SETTINGS, Str(STR_LIMITER_SETTINGS)},
    };

    HMENU menu = BuildPopup(head, ARRAYSIZE(head));
    AppendSubMenu(menu, STR_DL_START_QUEUE, startQueue, ARRAYSIZE(startQueue));
    AppendSubMenu(menu, STR_DL_STOP_QUEUE, stopQueue, ARRAYSIZE(stopQueue));
    AppendSubMenu(menu, STR_LIMITER, limiter, ARRAYSIZE(limiter));
    AppendMenuW(menu, MF_STRING, ID_DOWNLOAD_BOOSTER, Str(STR_BOOSTER));
    return menu;
}

// Builds the view drop-down.
HMENU BuildViewMenu() {
    const Entry head[] = {
        {ID_VIEW_ADDONS, Str(STR_VIEW_ADDONS)},
        {0, nullptr},
        {ID_VIEW_CATEGORIES, Str(STR_VIEW_CATEGORIES)},
    };
    const Entry sort[] = {
        {ID_SORT_DATE_ADDED, Str(STR_SORT_DATE_ADDED)},
        {ID_SORT_NAME, Str(STR_SORT_NAME)},
        {ID_SORT_SIZE, Str(STR_SORT_SIZE)},
        {ID_SORT_STATUS, Str(STR_SORT_STATUS)},
        {ID_SORT_TIME_LEFT, Str(STR_SORT_TIME_LEFT)},
        {ID_SORT_SPEED, Str(STR_SORT_SPEED)},
        {ID_SORT_LAST_TRY, Str(STR_SORT_LAST_TRY)},
        {ID_SORT_LOCATION, Str(STR_SORT_LOCATION)},
        {ID_SORT_ADDRESS, Str(STR_SORT_ADDRESS)},
        {ID_SORT_PARENT_PAGE, Str(STR_SORT_PARENT_PAGE)},
    };
    const Entry toolbar[] = {
        {ID_TOOLBAR_CUSTOMIZE, Str(STR_TOOLBAR_CUSTOMIZE)},
        {ID_TOOLBAR_INTERFACE, Str(STR_TOOLBAR_INTERFACE)},
    };
    const Entry mode[] = {
        {ID_MODE_DARK, Str(STR_MODE_DARK)},
        {ID_MODE_LIGHT, Str(STR_MODE_LIGHT)},
        {ID_MODE_SYSTEM, Str(STR_MODE_SYSTEM)},
    };
    const Entry font[] = {
        {ID_FONT_SELECT, Str(STR_FONT_SELECT)},
        {ID_FONT_RESET, Str(STR_FONT_RESET)},
    };
    const Entry language[] = {
        {ID_LANG_EN, L"English"},
        {ID_LANG_FR, L"Français"},
    };

    HMENU menu = BuildPopup(head, ARRAYSIZE(head));
    AppendSubMenu(menu, STR_VIEW_SORT, sort, ARRAYSIZE(sort));
    AppendSubMenu(menu, STR_VIEW_TOOLBAR, toolbar, ARRAYSIZE(toolbar));
    AppendMenuW(menu, MF_STRING, ID_VIEW_COLUMNS, Str(STR_VIEW_COLUMNS));
    AppendSubMenu(menu, STR_VIEW_MODE, mode, ARRAYSIZE(mode));
    AppendSubMenu(menu, STR_VIEW_FONT, font, ARRAYSIZE(font));
    AppendSubMenu(menu, STR_VIEW_LANGUAGE, language, ARRAYSIZE(language));
    return menu;
}

// Builds the help drop-down.
HMENU BuildHelpMenu() {
    const Entry head[] = {
        {ID_HELP_HELP, Str(STR_HELP_HELP)},
        {ID_HELP_SHORTCUTS, Str(STR_HELP_SHORTCUTS)},
        {ID_HELP_UPDATE, Str(STR_HELP_UPDATE)},
    };
    const Entry about[] = {
        {ID_HELP_ABOUT, Str(STR_HELP_ABOUT)},
        {ID_HELP_AUTHORS, Str(STR_HELP_AUTHORS)},
        {ID_HELP_LICENSE, Str(STR_HELP_LICENSE)},
        {ID_HELP_CREDITS, Str(STR_HELP_CREDITS)},
    };

    HMENU menu = BuildPopup(head, ARRAYSIZE(head));
    AppendSubMenu(menu, STR_HELP_ABOUT, about, ARRAYSIZE(about));
    return menu;
}

}  // namespace

// Assembles the top-level menu bar and installs it on the window.
void MenuBar::AttachTo(HWND window) {
    bar_ = CreateMenu();
    AppendMenuW(bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildTasksMenu()), Str(STR_MENU_TASKS));
    AppendMenuW(bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildFileMenu()), Str(STR_MENU_FILE));
    AppendMenuW(bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildDownloadMenu()),
                Str(STR_MENU_DOWNLOAD));
    AppendMenuW(bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildViewMenu()), Str(STR_MENU_VIEW));
    AppendMenuW(bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildHelpMenu()), Str(STR_MENU_HELP));
    SetMenu(window, bar_);
}

// Rebuilds the whole bar in the active language.
void MenuBar::Rebuild(HWND window) {
    HMENU previous = bar_;
    AttachTo(window);
    if (previous != nullptr) {
        DestroyMenu(previous);
    }
    DrawMenuBar(window);
}

// Ticks the categories entry when the panel is visible.
void MenuBar::SetCategoriesChecked(bool checked) {
    CheckMenuItem(bar_, ID_VIEW_CATEGORIES,
                  MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

// Moves the bullet to the active colour mode.
void MenuBar::SetTheme(int commandId) {
    CheckMenuRadioItem(bar_, ID_MODE_DARK, ID_MODE_SYSTEM, commandId, MF_BYCOMMAND);
}

// Moves the bullet to the active interface language.
void MenuBar::SetLanguage(int commandId) {
    CheckMenuRadioItem(bar_, ID_LANG_EN, ID_LANG_FR, commandId, MF_BYCOMMAND);
}
