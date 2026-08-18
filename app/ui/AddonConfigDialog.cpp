#include "ui/AddonConfigDialog.h"

#include <commctrl.h>

#include <map>
#include <memory>
#include <vector>

#include "core/Addon.h"
#include "core/AddonStore.h"
#include "core/Http.h"
#include "core/Text.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {

constexpr int kRowHeight = 30;
constexpr int kLabelWidth = 150;
constexpr int kGap = 10;

// What the dialog needs to build its form and save it back.
struct Session {
    const AddonStore* store = nullptr;
    std::string addonId;
    std::vector<AddonPreference> preferences;
    std::map<std::string, std::string> values;
    std::vector<HWND> fields;
};

// Creates the control a preference calls for, and fills it with the stored value.
HWND CreateField(HWND dialog, const AddonPreference& preference, const std::string& value,
                 int id, int x, int y, int width, int height) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
    auto menu = reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id));

    if (preference.kind == "bool") {
        HWND box = CreateWindowExW(0, WC_BUTTONW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                           BS_AUTOCHECKBOX,
                                   x, y, width, height, dialog, menu, instance, nullptr);
        SendMessageW(box, BM_SETCHECK, value == "true" ? BST_CHECKED : BST_UNCHECKED, 0);
        return box;
    }

    if (preference.kind == "select") {
        HWND combo = CreateWindowExW(0, WC_COMBOBOXW, L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                         CBS_DROPDOWNLIST,
                                     x, y, width, height * 8, dialog, menu, instance, nullptr);
        int selected = 0;
        int index = 0;
        for (const std::string& option : preference.options) {
            SendMessageW(combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(Widen(option).c_str()));
            if (option == value) {
                selected = index;
            }
            ++index;
        }
        SendMessageW(combo, CB_SETCURSEL, selected, 0);
        return combo;
    }

    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, Widen(value).c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x, y, width,
                                height, dialog, menu, instance, nullptr);
    return edit;
}

// Lays the form out inside the reserved area, one row per preference.
void BuildForm(HWND dialog, Session& session) {
    HWND area = GetDlgItem(dialog, IDC_CONFIG_AREA);
    RECT bounds = {};
    GetWindowRect(area, &bounds);
    MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&bounds), 2);
    ShowWindow(area, SW_HIDE);

    HFONT font = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));

    if (session.preferences.empty()) {
        HWND empty = CreateWindowExW(0, WC_STATICW, Str(STR_CONFIG_EMPTY), WS_CHILD | WS_VISIBLE,
                                     bounds.left, bounds.top, bounds.right - bounds.left, 20,
                                     dialog, nullptr, instance, nullptr);
        SendMessageW(empty, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return;
    }

    int fieldWidth = bounds.right - bounds.left - kLabelWidth - kGap;
    int y = bounds.top;
    int id = IDC_CONFIG_FIRST;

    for (const AddonPreference& preference : session.preferences) {
        HWND label = CreateWindowExW(0, WC_STATICW, Widen(preference.title).c_str(),
                                     WS_CHILD | WS_VISIBLE, bounds.left, y + 4, kLabelWidth, 18,
                                     dialog, nullptr, instance, nullptr);
        SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

        auto stored = session.values.find(preference.key);
        std::string value =
            stored != session.values.end() ? stored->second : preference.defaultValue;

        HWND field = CreateField(dialog, preference, value, id, bounds.left + kLabelWidth + kGap,
                                 y, fieldWidth, 22);
        SendMessageW(field, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        session.fields.push_back(field);

        y += kRowHeight;
        ++id;
    }
}

// Reads the form back and writes it next to the addon.
void Save(HWND dialog, Session& session) {
    std::map<std::string, std::string> config;
    for (size_t index = 0; index < session.preferences.size(); ++index) {
        const AddonPreference& preference = session.preferences[index];
        HWND field = session.fields[index];

        if (preference.kind == "bool") {
            config[preference.key] =
                SendMessageW(field, BM_GETCHECK, 0, 0) == BST_CHECKED ? "true" : "false";
            continue;
        }

        int length = GetWindowTextLengthW(field);
        std::wstring text(static_cast<size_t>(length), L'\0');
        if (length > 0) {
            GetWindowTextW(field, text.data(), length + 1);
        }
        config[preference.key] = Narrow(text);
    }
    session.store->WriteConfig(session.addonId, config);
    EndDialog(dialog, IDOK);
}

INT_PTR CALLBACK ConfigDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }

    auto* session = reinterpret_cast<Session*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        session = reinterpret_cast<Session*>(lParam);
        SetDialogTitle(dialog, STR_CONFIG_TITLE);
        SetDialogText(dialog, IDOK, STR_DLG_OK);
        SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);
        BuildForm(dialog, *session);
        ActiveTheme().ApplyToDialog(dialog);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            if (session != nullptr) {
                Save(dialog, *session);
            }
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

}  // namespace

// Loads the source, reads the settings it declares, and shows them as a form.
INT_PTR ShowAddonConfigDialog(HWND owner, HINSTANCE instance, const AddonStore& store, Http& http,
                              const std::string& addonId) {
    Session session;
    session.store = &store;
    session.addonId = addonId;
    session.values = store.ReadConfig(addonId);

    std::unique_ptr<Addon> addon = Addon::Load(store.LibraryPath(addonId), http, session.values);
    if (addon) {
        session.preferences = addon->Preferences();
    }

    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ADDON_CONFIG), owner, ConfigDialogProc,
                           reinterpret_cast<LPARAM>(&session));
}
