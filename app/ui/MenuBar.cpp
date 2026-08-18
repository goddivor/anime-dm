#include "ui/MenuBar.h"

#include "ui/Commands.h"

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
void AppendSubMenu(HMENU parent, const wchar_t* label, const Entry* entries, size_t count) {
    HMENU sub = BuildPopup(entries, count);
    AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), label);
}

constexpr Entry kExport[] = {
    {ID_TASK_EXPORT_ADM, L"Vers un fichier d'exportation d'ADM"},
    {ID_TASK_EXPORT_TXT, L"Vers un fichier texte (.txt)"},
    {ID_TASK_EXPORT_JSON, L"Vers un fichier JSON (.json)"},
    {ID_TASK_EXPORT_SHEET, L"Vers un tableur (.xlsx, .csv, .ods)"},
};

constexpr Entry kImport[] = {
    {ID_TASK_IMPORT_ADM, L"Depuis un fichier d'exportation d'ADM"},
    {ID_TASK_IMPORT_TXT, L"Depuis un fichier texte (.txt)"},
    {ID_TASK_IMPORT_JSON, L"Depuis un fichier JSON (.json)"},
    {ID_TASK_IMPORT_SHEET, L"Depuis un tableur (.xlsx, .csv, .ods)"},
};

constexpr Entry kStartQueue[] = {
    {ID_QUEUE_START_MAIN, L"File principale"},
    {ID_QUEUE_START_SCHEDULER, L"File du planificateur"},
};

constexpr Entry kStopQueue[] = {
    {ID_QUEUE_STOP_MAIN, L"File principale"},
    {ID_QUEUE_STOP_SCHEDULER, L"File du planificateur"},
};

constexpr Entry kLimiter[] = {
    {ID_LIMITER_ENABLE, L"Activer"},
    {ID_LIMITER_DISABLE, L"Désactiver"},
    {ID_LIMITER_SETTINGS, L"Paramètres"},
};

constexpr Entry kSort[] = {
    {ID_SORT_DATE_ADDED, L"Par ordre d'ajout"},
    {ID_SORT_NAME, L"Par nom de fichier"},
    {ID_SORT_SIZE, L"Par taille"},
    {ID_SORT_STATUS, L"Par statut"},
    {ID_SORT_TIME_LEFT, L"Par temps restant"},
    {ID_SORT_SPEED, L"Par vitesse"},
    {ID_SORT_LAST_TRY, L"Par date du dernier essai"},
    {ID_SORT_LOCATION, L"Par emplacement"},
    {ID_SORT_ADDRESS, L"Par adresse"},
    {ID_SORT_PARENT_PAGE, L"Par page web parente"},
};

constexpr Entry kToolbar[] = {
    {ID_TOOLBAR_CUSTOMIZE, L"Personnaliser la barre d'outils"},
    {ID_TOOLBAR_INTERFACE, L"Interface"},
};

constexpr Entry kMode[] = {
    {ID_MODE_DARK, L"Sombre"},
    {ID_MODE_LIGHT, L"Claire"},
    {ID_MODE_SYSTEM, L"Système"},
};

constexpr Entry kFont[] = {
    {ID_FONT_SELECT, L"Sélectionner la police"},
    {ID_FONT_RESET, L"Rétablir la police par défaut"},
};

constexpr Entry kLanguage[] = {
    {ID_LANG_EN, L"English"},
    {ID_LANG_FR, L"Français"},
};

constexpr Entry kAbout[] = {
    {ID_HELP_ABOUT, L"À propos"},
    {ID_HELP_AUTHORS, L"Auteurs"},
    {ID_HELP_LICENSE, L"Licence"},
    {ID_HELP_CREDITS, L"Crédits"},
};

// Builds the "Tâches" drop-down.
HMENU BuildTasksMenu() {
    constexpr Entry head[] = {
        {ID_TASK_ADD, L"Ajouter nouveau téléchargement\tCtrl+N"},
        {ID_TASK_MANUAL, L"Téléchargement manuel"},
        {ID_TASK_BATCH, L"Téléchargement par lot depuis presse-papiers\tCtrl+Maj+V"},
        {0, nullptr},
    };
    HMENU menu = BuildPopup(head, ARRAYSIZE(head));
    AppendSubMenu(menu, L"Exporter", kExport, ARRAYSIZE(kExport));
    AppendSubMenu(menu, L"Importer", kImport, ARRAYSIZE(kImport));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TASK_QUIT, L"Quitter");
    return menu;
}

// Builds the "Fichier" drop-down.
HMENU BuildFileMenu() {
    constexpr Entry entries[] = {
        {ID_FILE_START, L"Démarrer le téléchargement"},
        {ID_FILE_STOP, L"Arrêter le téléchargement"},
        {ID_FILE_REDOWNLOAD, L"Re-télécharger"},
        {0, nullptr},
        {ID_FILE_REMOVE, L"Supprimer\tSuppr"},
    };
    return BuildPopup(entries, ARRAYSIZE(entries));
}

// Builds the "Téléchargement" drop-down.
HMENU BuildDownloadMenu() {
    constexpr Entry head[] = {
        {ID_DOWNLOAD_STOP_ALL, L"Tout arrêter"},
        {ID_DOWNLOAD_REMOVE_COMPLETED, L"Supprimer les terminés"},
        {ID_DOWNLOAD_DELETE_ALL, L"Tout supprimer"},
        {ID_DOWNLOAD_SEARCH, L"Rechercher\tCtrl+F"},
        {0, nullptr},
        {ID_DOWNLOAD_SCHEDULE, L"Planifier"},
    };
    HMENU menu = BuildPopup(head, ARRAYSIZE(head));
    AppendSubMenu(menu, L"Démarrer file d'attente", kStartQueue, ARRAYSIZE(kStartQueue));
    AppendSubMenu(menu, L"Arrêter file d'attente", kStopQueue, ARRAYSIZE(kStopQueue));
    AppendSubMenu(menu, L"Limiteur de vitesse", kLimiter, ARRAYSIZE(kLimiter));
    AppendMenuW(menu, MF_STRING, ID_DOWNLOAD_BOOSTER, L"Booster de vitesse");
    return menu;
}

// Builds the "Affichage" drop-down.
HMENU BuildViewMenu() {
    constexpr Entry head[] = {
        {ID_VIEW_ADDONS, L"Addon Store"},
        {0, nullptr},
        {ID_VIEW_CATEGORIES, L"Panneau Catégories"},
    };
    HMENU menu = BuildPopup(head, ARRAYSIZE(head));
    AppendSubMenu(menu, L"Classer les fichiers", kSort, ARRAYSIZE(kSort));
    AppendSubMenu(menu, L"Barre d'outils", kToolbar, ARRAYSIZE(kToolbar));
    AppendMenuW(menu, MF_STRING, ID_VIEW_COLUMNS, L"Personnaliser les colonnes");
    AppendSubMenu(menu, L"Mode", kMode, ARRAYSIZE(kMode));
    AppendSubMenu(menu, L"Police", kFont, ARRAYSIZE(kFont));
    AppendSubMenu(menu, L"Langue", kLanguage, ARRAYSIZE(kLanguage));
    return menu;
}

// Builds the "Aide" drop-down.
HMENU BuildHelpMenu() {
    constexpr Entry head[] = {
        {ID_HELP_HELP, L"Aide\tF1"},
        {ID_HELP_SHORTCUTS, L"Raccourcis"},
        {ID_HELP_UPDATE, L"Mise à jour rapide"},
    };
    HMENU menu = BuildPopup(head, ARRAYSIZE(head));
    AppendSubMenu(menu, L"À propos", kAbout, ARRAYSIZE(kAbout));
    return menu;
}

}  // namespace

// Assembles the top-level menu bar and installs it on the window.
void MenuBar::AttachTo(HWND window) {
    bar_ = CreateMenu();
    AppendMenuW(bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildTasksMenu()), L"Tâches");
    AppendMenuW(bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildFileMenu()), L"Fichier");
    AppendMenuW(bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildDownloadMenu()), L"Téléchargement");
    AppendMenuW(bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildViewMenu()), L"Affichage");
    AppendMenuW(bar_, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildHelpMenu()), L"Aide");
    SetMenu(window, bar_);
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
