#include "ui/SettingsDialog.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/Bridge.h"
#include "core/FolderIcon.h"
#include "core/Text.h"
#include "ui/FolderPicker.h"
#include "ui/Paint.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/TabStrip.h"
#include "ui/TemplateNames.h"
#include "ui/Theme.h"

namespace {

constexpr int kPageCount = 3;
constexpr int kPageIds[kPageCount] = {IDD_SET_GENERAL, IDD_SET_SAVE, IDD_SET_DOWNLOADS};
constexpr StringId kPageTitles[kPageCount] = {STR_SET_TAB_GENERAL, STR_SET_TAB_SAVE,
                                              STR_SET_TAB_DOWNLOADS};

// The strip of tabs and the body under it, in dialog units.
constexpr RECT kStripUnits = {8, 6, 332, 20};
constexpr RECT kBodyUnits = {8, 20, 332, 228};

// What the dialog works on: the settings, the pages, and where the tabs lie.
struct Screen {
    Settings* settings = nullptr;
    Settings original;
    Settings draft;
    std::vector<std::string> templates;
    HINSTANCE instance = nullptr;
    HWND pages[kPageCount] = {};
    int creating = 0;
    TabStrip tabs;
};

HFONT FontOf(HWND window) {
    return reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
}

// --- the pages ----------------------------------------------------------------

// Enables the template combo only while folder icons are on.
void SyncTemplateState(HWND page) {
    bool enabled = IsDlgButtonChecked(page, IDC_SET_FOLDER_ICONS) == BST_CHECKED;
    EnableWindow(GetDlgItem(page, IDC_SET_TEMPLATE), enabled);
    EnableWindow(GetDlgItem(page, IDC_SET_LBL_TEMPLATE), enabled);
}

// Fills the general page: the two switches and the browsers the host is
// declared to, one checked row each.
void InitGeneral(HWND page, Screen& screen) {
    const Settings& settings = *screen.settings;
    SetDialogText(page, IDC_SET_HEADING, STR_SET_HEADING);
    SetDialogText(page, IDC_SET_AUTOSTART, STR_SET_AUTOSTART);
    SetDialogText(page, IDC_SET_CLIPBOARD, STR_SET_CLIPBOARD);
    SetDialogText(page, IDC_SET_CLOSE_TO_TRAY, STR_SET_CLOSE_TO_TRAY);
    SetDialogText(page, IDC_SET_LBL_BROWSERS, STR_SET_BROWSERS);
    SetDialogText(page, IDC_SET_BROWSERS_HINT, STR_SET_BROWSERS_HINT);
    SetDialogText(page, IDC_SET_LBL_PANEL, STR_SET_PANEL);
    SetDialogText(page, IDC_SET_PANEL_EDIT, STR_SET_PANEL_EDIT);
    SetDialogText(page, IDC_SET_INSTALL, STR_SET_INSTALL);

    HICON icon = static_cast<HICON>(LoadImageW(screen.instance, MAKEINTRESOURCEW(IDI_APP),
                                               IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    SendDlgItemMessageW(page, IDC_SET_ICON, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);

    CheckDlgButton(page, IDC_SET_AUTOSTART,
                   settings.startWithWindows ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(page, IDC_SET_CLIPBOARD, settings.clipboardUrl ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(page, IDC_SET_CLOSE_TO_TRAY, settings.closeToTray ? BST_CHECKED : BST_UNCHECKED);

    HWND list = GetDlgItem(page, IDC_SET_BROWSERS);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    LVCOLUMNW column = {};
    column.mask = LVCF_WIDTH;
    ListView_InsertColumn(list, 0, &column);

    const std::vector<bridge::Browser>& browsers = bridge::Browsers();
    for (size_t i = 0; i < browsers.size(); ++i) {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(browsers[i].name);
        ListView_InsertItem(list, &item);
        bool enabled = std::find(settings.browsers.begin(), settings.browsers.end(),
                                 browsers[i].id) != settings.browsers.end();
        ListView_SetCheckState(list, static_cast<int>(i), enabled ? TRUE : FALSE);
    }
    ListView_SetColumnWidth(list, 0, LVSCW_AUTOSIZE_USEHEADER);
}

// The rectangle of a control, in the coordinates of its page.
RECT ControlRect(HWND page, int id) {
    RECT rect = {};
    GetWindowRect(GetDlgItem(page, id), &rect);
    MapWindowPoints(nullptr, page, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}

// Underlines the heading of the general page, from the icon to the margin.
void PaintHeadingRule(HWND page) {
    PAINTSTRUCT ps = {};
    HDC dc = BeginPaint(page, &ps);
    RECT icon = ControlRect(page, IDC_SET_ICON);
    RECT heading = ControlRect(page, IDC_SET_HEADING);
    int y = heading.bottom + (heading.bottom - heading.top) / 2;
    HPEN pen = CreatePen(PS_SOLID, 1, ActiveTheme().Colors().line);
    HPEN old = SelectPen(dc, pen);
    MoveToEx(dc, icon.right + (icon.right - icon.left) / 2, y, nullptr);
    LineTo(dc, heading.right, y);
    SelectPen(dc, old);
    DeleteObject(pen);
    EndPaint(page, &ps);
}

// Fills the save page: the folder, its recall, the folder icons and Aniyomi.
void InitSave(HWND page, Screen& screen) {
    const Settings& settings = *screen.settings;
    SetDialogText(page, IDC_SET_LBL_FOLDER, STR_SET_FOLDER);
    SetDialogText(page, IDC_SET_REMEMBER, STR_SET_REMEMBER);
    SetDialogText(page, IDC_SET_LBL_ICONS, STR_DLG_SET_ICONS);
    SetDialogText(page, IDC_SET_FOLDER_ICONS, STR_DLG_SET_ICONS_DESC);
    SetDialogText(page, IDC_SET_LBL_TEMPLATE, STR_DLG_SET_TEMPLATE);
    SetDialogText(page, IDC_SET_LBL_ANIYOMI, STR_DLG_SET_ANIYOMI);
    SetDialogText(page, IDC_SET_ANIYOMI, STR_DLG_SET_ANIYOMI_DESC);

    SetDlgItemTextW(page, IDC_SET_FOLDER, Widen(settings.savePath).c_str());
    CheckDlgButton(page, IDC_SET_REMEMBER, settings.rememberPath ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(page, IDC_SET_FOLDER_ICONS,
                   settings.folderIcons ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(page, IDC_SET_ANIYOMI, settings.aniyomi ? BST_CHECKED : BST_UNCHECKED);

    screen.templates = foldericon::TemplateIds();
    int chosen = 0;
    for (size_t i = 0; i < screen.templates.size(); ++i) {
        SendDlgItemMessageW(page, IDC_SET_TEMPLATE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(Str(TemplateName(screen.templates[i]))));
        if (screen.templates[i] == settings.folderTemplate) {
            chosen = static_cast<int>(i);
        }
    }
    SendDlgItemMessageW(page, IDC_SET_TEMPLATE, CB_SETCURSEL, chosen, 0);
    SyncTemplateState(page);
}

// Fills the downloads page: the two limits of the engine, with their spinners.
void InitDownloads(HWND page, Screen& screen) {
    const Settings& settings = *screen.settings;
    SetDialogText(page, IDC_SET_LBL_RUNNING, STR_SET_RUNNING);
    SetDialogText(page, IDC_SET_LBL_CONNECTIONS, STR_SET_CONNECTIONS);
    SetDialogText(page, IDC_SET_LIMITS_HINT, STR_SET_LIMITS_HINT);

    HWND running = GetDlgItem(page, IDC_SET_RUNNING_SPIN);
    SendMessageW(running, UDM_SETRANGE32, settings::kMinRunning, settings::kMaxRunning);
    SendMessageW(running, UDM_SETPOS32, 0, settings.maxRunning);
    HWND connections = GetDlgItem(page, IDC_SET_CONNECTIONS_SPIN);
    SendMessageW(connections, UDM_SETRANGE32, settings::kMinConnections,
                 settings::kMaxConnections);
    SendMessageW(connections, UDM_SETPOS32, 0, settings.connections);
}

// Reads a spinner back, within its bounds.
int SpinValue(HWND page, int spinId) {
    return static_cast<int>(SendDlgItemMessageW(page, spinId, UDM_GETPOS32, 0, 0));
}

// Reads every page back into the settings.
void ReadPages(Screen& screen) {
    Settings& settings = *screen.settings;
    HWND general = screen.pages[0];
    settings.startWithWindows = IsDlgButtonChecked(general, IDC_SET_AUTOSTART) == BST_CHECKED;
    settings.clipboardUrl = IsDlgButtonChecked(general, IDC_SET_CLIPBOARD) == BST_CHECKED;
    settings.closeToTray = IsDlgButtonChecked(general, IDC_SET_CLOSE_TO_TRAY) == BST_CHECKED;
    settings.browsers.clear();
    HWND list = GetDlgItem(general, IDC_SET_BROWSERS);
    const std::vector<bridge::Browser>& browsers = bridge::Browsers();
    for (size_t i = 0; i < browsers.size(); ++i) {
        if (ListView_GetCheckState(list, static_cast<int>(i))) {
            settings.browsers.push_back(browsers[i].id);
        }
    }

    HWND save = screen.pages[1];
    wchar_t folder[MAX_PATH] = {};
    GetDlgItemTextW(save, IDC_SET_FOLDER, folder, MAX_PATH);
    settings.savePath = Narrow(folder);
    settings.rememberPath = IsDlgButtonChecked(save, IDC_SET_REMEMBER) == BST_CHECKED;
    settings.folderIcons = IsDlgButtonChecked(save, IDC_SET_FOLDER_ICONS) == BST_CHECKED;
    settings.aniyomi = IsDlgButtonChecked(save, IDC_SET_ANIYOMI) == BST_CHECKED;
    int chosen = static_cast<int>(SendDlgItemMessageW(save, IDC_SET_TEMPLATE, CB_GETCURSEL, 0, 0));
    if (chosen >= 0 && static_cast<size_t>(chosen) < screen.templates.size()) {
        settings.folderTemplate = screen.templates[static_cast<size_t>(chosen)];
    }

    HWND downloads = screen.pages[2];
    settings.maxRunning = SpinValue(downloads, IDC_SET_RUNNING_SPIN);
    settings.connections = SpinValue(downloads, IDC_SET_CONNECTIONS_SPIN);

    settings.panelMode = screen.draft.panelMode;
    settings.panelOnPage = screen.draft.panelOnPage;
    settings.panelOnLinks = screen.draft.panelOnLinks;
}

// Paints a page in the colour of a window, so that it reads as the sheet
// under the chosen tab.
INT_PTR PageColor(HDC dc) {
    const Theme& theme = ActiveTheme();
    SetTextColor(dc, theme.Colors().text);
    SetBkColor(dc, theme.Colors().window);
    return reinterpret_cast<INT_PTR>(theme.WindowBrush());
}

INT_PTR CALLBACK PanelProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam);

// Procedure shared by the three pages: colours, the initial fill, and the
// few controls that act at once.
INT_PTR CALLBACK PageProc(HWND page, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return PageColor(reinterpret_cast<HDC>(wParam));
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        INT_PTR colour = 0;
        return ThemeDialogMessage(msg, wParam, &colour) ? colour : 0;
    }
    case WM_INITDIALOG: {
        SetWindowLongPtrW(page, GWLP_USERDATA, lParam);
        auto* screen = reinterpret_cast<Screen*>(lParam);
        ActiveTheme().ApplyToDialog(page);
        if (screen->creating == 0) {
            InitGeneral(page, *screen);
        } else if (screen->creating == 1) {
            InitSave(page, *screen);
        } else {
            InitDownloads(page, *screen);
        }
        return FALSE;
    }
    case WM_PAINT: {
        auto* screen = reinterpret_cast<Screen*>(GetWindowLongPtrW(page, GWLP_USERDATA));
        if (screen != nullptr && page == screen->pages[0]) {
            PaintHeadingRule(page);
            return TRUE;
        }
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_SET_FOLDER_ICONS:
            SyncTemplateState(page);
            return TRUE;
        case IDC_SET_INSTALL:
            bridge::OpenStorePage();
            return TRUE;
        case IDC_SET_PANEL_EDIT: {
            auto* screen = reinterpret_cast<Screen*>(GetWindowLongPtrW(page, GWLP_USERDATA));
            DialogBoxParamW(screen->instance, MAKEINTRESOURCEW(IDD_PANEL), GetParent(page),
                            PanelProc, reinterpret_cast<LPARAM>(screen));
            return TRUE;
        }
        case IDC_SET_BROWSE: {
            std::wstring folder = PickFolder(page);
            if (!folder.empty()) {
                SetDlgItemTextW(page, IDC_SET_FOLDER, folder.c_str());
            }
            return TRUE;
        }
        default:
            return FALSE;
        }
    default:
        return FALSE;
    }
}

// --- the panel dialog ---------------------------------------------------------

// Paints a sample of the panel as the extension draws it: the pill in the
// accent colour, the icon, and the caption unless the sample is the mini one.
void DrawPanelSample(const DRAWITEMSTRUCT& item, HINSTANCE instance, bool mini) {
    const ThemeColors& colors = ActiveTheme().Colors();
    RECT bounds = item.rcItem;
    HBRUSH back = CreateSolidBrush(colors.surface);
    FillRect(item.hDC, &bounds, back);
    DeleteObject(back);

    int height = bounds.bottom - bounds.top;
    int icon = 16;
    int pad = (height - icon) / 2;
    std::wstring caption = mini ? std::wstring() : Str(STR_BUTTON_SAMPLE);
    SIZE size = {};
    HFONT font = FontOf(item.hwndItem);
    HFONT old = SelectFont(item.hDC, font);
    if (!caption.empty()) {
        GetTextExtentPoint32W(item.hDC, caption.c_str(), static_cast<int>(caption.size()), &size);
    }
    RECT pill = bounds;
    pill.right = pill.left + pad + icon + (caption.empty() ? pad : pad + size.cx + pad + 2);
    paint::RoundedRect(item.hDC, pill, static_cast<float>(height) / 2.0f, colors.accent,
                       colors.accent);

    HICON glyph = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                icon, icon, LR_DEFAULTCOLOR));
    DrawIconEx(item.hDC, pill.left + pad, pill.top + pad, glyph, icon, icon, 0, nullptr,
               DI_NORMAL);
    DestroyIcon(glyph);
    if (!caption.empty()) {
        RECT text = {pill.left + pad + icon + pad, pill.top, pill.right - pad, pill.bottom};
        paint::Label(item.hDC, text, caption, colors.accentText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectFont(item.hDC, old);
}

// Procedure of the panel dialog: two looks to pick from, two places to show.
INT_PTR CALLBACK PanelProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }
    auto* screen = reinterpret_cast<Screen*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        screen = reinterpret_cast<Screen*>(lParam);
        ActiveTheme().ApplyToDialog(dialog);
        SetDialogTitle(dialog, STR_DLG_PANEL_TITLE);
        SetDialogText(dialog, IDC_PANEL_LBL_MODE, STR_PANEL_MODE);
        SetDialogText(dialog, IDC_PANEL_FULL, STR_PANEL_FULL);
        SetDialogText(dialog, IDC_PANEL_MINI, STR_PANEL_MINI);
        SetDialogText(dialog, IDC_PANEL_LBL_WHERE, STR_PANEL_WHERE);
        SetDialogText(dialog, IDC_PANEL_ON_PAGE, STR_PANEL_ON_PAGE);
        SetDialogText(dialog, IDC_PANEL_ON_LINKS, STR_PANEL_ON_LINKS);
        SetDialogText(dialog, IDOK, STR_DLG_OK);
        SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);
        const Settings& settings = screen->draft;
        CheckRadioButton(dialog, IDC_PANEL_FULL, IDC_PANEL_MINI,
                         settings.panelMode == "mini" ? IDC_PANEL_MINI : IDC_PANEL_FULL);
        CheckDlgButton(dialog, IDC_PANEL_ON_PAGE, settings.panelOnPage ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_PANEL_ON_LINKS,
                       settings.panelOnLinks ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item->CtlID == IDC_PANEL_PREVIEW_FULL || item->CtlID == IDC_PANEL_PREVIEW_MINI) {
            DrawPanelSample(*item, screen->instance, item->CtlID == IDC_PANEL_PREVIEW_MINI);
            return TRUE;
        }
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            Settings& settings = screen->draft;
            settings.panelMode =
                IsDlgButtonChecked(dialog, IDC_PANEL_MINI) == BST_CHECKED ? "mini" : "full";
            settings.panelOnPage = IsDlgButtonChecked(dialog, IDC_PANEL_ON_PAGE) == BST_CHECKED;
            settings.panelOnLinks = IsDlgButtonChecked(dialog, IDC_PANEL_ON_LINKS) == BST_CHECKED;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    default:
        return FALSE;
    }
}

// --- the tabs -----------------------------------------------------------------

// Shows one page and hides the others.
void ShowPage(HWND dialog, Screen& screen, int index) {
    screen.tabs.SetPage(dialog, index);
    for (int i = 0; i < kPageCount; ++i) {
        ShowWindow(screen.pages[i], i == index ? SW_SHOW : SW_HIDE);
    }
}

// Creates the pages inside the body and shows the first.
void CreatePages(HWND dialog, Screen& screen) {
    screen.tabs.Init(dialog, kStripUnits, kBodyUnits,
                     std::vector<StringId>(kPageTitles, kPageTitles + kPageCount));
    const RECT& body = screen.tabs.Body();
    for (int i = 0; i < kPageCount; ++i) {
        screen.creating = i;
        screen.pages[i] = CreateDialogParamW(screen.instance, MAKEINTRESOURCEW(kPageIds[i]), dialog,
                                             PageProc, reinterpret_cast<LPARAM>(&screen));
        SetWindowPos(screen.pages[i], HWND_TOP, body.left + 1, body.top + 1,
                     body.right - body.left - 2, body.bottom - body.top - 2, SWP_NOACTIVATE);
    }
    ShowPage(dialog, screen, 0);
}

// Dialog procedure of the frame: the strip of tabs, OK and Cancel.
INT_PTR CALLBACK SettingsDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    auto* screen = reinterpret_cast<Screen*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        screen = reinterpret_cast<Screen*>(lParam);
        ActiveTheme().ApplyToDialog(dialog);
        SetDialogTitle(dialog, STR_DLG_SETTINGS_TITLE);
        SetDialogText(dialog, IDOK, STR_DLG_OK);
        SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);
        CreatePages(dialog, *screen);
        return TRUE;
    case WM_PAINT:
        screen->tabs.Paint(dialog);
        return TRUE;
    case WM_LBUTTONDOWN: {
        int tab = screen->tabs.HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if (tab >= 0 && tab != screen->tabs.Page()) {
            ShowPage(dialog, *screen, tab);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            ReadPages(*screen);
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    default:
        return FALSE;
    }
}

// Whether two settings differ in what the dialog edits.
bool Differs(const Settings& a, const Settings& b) {
    return a.folderIcons != b.folderIcons || a.folderTemplate != b.folderTemplate ||
           a.aniyomi != b.aniyomi || a.clipboardUrl != b.clipboardUrl ||
           a.rememberPath != b.rememberPath || a.savePath != b.savePath ||
           a.startWithWindows != b.startWithWindows || a.browsers != b.browsers ||
           a.maxRunning != b.maxRunning || a.connections != b.connections ||
           a.panelMode != b.panelMode || a.panelOnPage != b.panelOnPage ||
           a.panelOnLinks != b.panelOnLinks;
}

}  // namespace

// Shows the options dialog over the settings; Cancel leaves them untouched.
bool ShowSettingsDialog(HWND owner, HINSTANCE instance, Settings* settings) {
    Screen screen;
    screen.settings = settings;
    screen.original = *settings;
    screen.draft = *settings;
    screen.instance = instance;
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsDialogProc,
                    reinterpret_cast<LPARAM>(&screen));
    return Differs(screen.original, *settings);
}
