#include "ui/MenuBar.h"

#include "ui/Commands.h"

namespace {

// Builds the "Fichier" drop-down.
HMENU BuildFileMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TASK_ADD, L"Ajouter un téléchargement\tCtrl+N");
    AppendMenuW(menu, MF_STRING, ID_FILE_REMOVE, L"Supprimer\tSuppr");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_FILE_EXIT, L"Quitter");
    return menu;
}

// Builds the "Téléchargement" drop-down.
HMENU BuildDownloadMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_DOWNLOAD_RESUME, L"Reprendre");
    AppendMenuW(menu, MF_STRING, ID_DOWNLOAD_STOP, L"Arrêter");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_DOWNLOAD_RESUME_ALL, L"Tout reprendre");
    AppendMenuW(menu, MF_STRING, ID_DOWNLOAD_STOP_ALL, L"Tout arrêter");
    return menu;
}

// Builds the "Affichage" drop-down.
HMENU BuildViewMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_VIEW_DOWNLOADS, L"Téléchargements");
    AppendMenuW(menu, MF_STRING, ID_VIEW_ADDONS, L"Extensions");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_VIEW_SETTINGS, L"Paramètres");
    return menu;
}

// Builds the "Aide" drop-down.
HMENU BuildHelpMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_HELP_SHORTCUTS, L"Raccourcis clavier");
    AppendMenuW(menu, MF_STRING, ID_HELP_ABOUT, L"À propos");
    return menu;
}

}  // namespace

// Assembles the top-level menu bar and installs it on the window.
void MenuBar::AttachTo(HWND window) {
    HMENU bar = CreateMenu();
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildFileMenu()), L"Fichier");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildDownloadMenu()), L"Téléchargement");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildViewMenu()), L"Affichage");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildHelpMenu()), L"Aide");
    SetMenu(window, bar);
}
