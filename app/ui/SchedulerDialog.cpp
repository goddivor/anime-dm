#include "ui/SchedulerDialog.h"

#include <commctrl.h>
#include <windowsx.h>

#include <ctime>
#include <string>

#include "core/Text.h"
#include "ui/DownloadsView.h"
#include "ui/Resource.h"
#include "ui/Strings.h"
#include "ui/TabStrip.h"
#include "ui/Theme.h"

namespace {

constexpr int kPageCount = 2;
constexpr int kPageIds[kPageCount] = {IDD_SCH_QUEUES, IDD_SCH_FOLLOWS};
constexpr StringId kPageTitles[kPageCount] = {STR_SCH_TAB_QUEUES, STR_SCH_TAB_FOLLOWS};
constexpr RECT kStripUnits = {8, 6, 332, 20};
constexpr RECT kBodyUnits = {8, 20, 332, 260};

// The dialog and the copies of the schedules and follows it edits until OK.
struct Editing {
    SchedulerScreen* screen = nullptr;
    HINSTANCE instance = nullptr;
    Scheduler draft;
    std::vector<FollowedAnime> follows;
    QueueKind shown = QueueKind::Main;
    HWND pages[kPageCount] = {};
    int creating = 0;
    TabStrip tabs;
};

QueueKind QueueAt(int index) {
    return index == 1 ? QueueKind::Scheduler : QueueKind::Main;
}

int IndexOf(QueueKind queue) {
    return queue == QueueKind::Scheduler ? 1 : 0;
}

// Sets a date picker to a local date, or to today when the schedule has none.
void SetDate(HWND picker, int year, int month, int day) {
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    if (year > 0) {
        time.wYear = static_cast<WORD>(year);
        time.wMonth = static_cast<WORD>(month);
        time.wDay = static_cast<WORD>(day);
    }
    DateTime_SetSystemtime(picker, GDT_VALID, &time);
}

// Sets a time picker to an hour of the day.
void SetClock(HWND picker, int hour, int minute) {
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    time.wHour = static_cast<WORD>(hour);
    time.wMinute = static_cast<WORD>(minute);
    time.wSecond = 0;
    DateTime_SetSystemtime(picker, GDT_VALID, &time);
}

// Greys the schedule controls that the switches above them turn off.
void SyncEnabled(HWND dialog) {
    bool enabled = IsDlgButtonChecked(dialog, IDC_SCH_ENABLED) == BST_CHECKED;
    bool daily = IsDlgButtonChecked(dialog, IDC_SCH_DAILY) == BST_CHECKED;
    bool stop = IsDlgButtonChecked(dialog, IDC_SCH_STOP) == BST_CHECKED;
    for (int id : {IDC_SCH_ONCE, IDC_SCH_DAILY, IDC_SCH_LBL_START, IDC_SCH_START, IDC_SCH_STOP,
                   IDC_SCH_LBL_DONE, IDC_SCH_DONE}) {
        EnableWindow(GetDlgItem(dialog, id), enabled);
    }
    EnableWindow(GetDlgItem(dialog, IDC_SCH_DATE), enabled && !daily);
    for (int id = IDC_SCH_DAY0; id <= IDC_SCH_DAY6; ++id) {
        EnableWindow(GetDlgItem(dialog, id), enabled && daily);
    }
    EnableWindow(GetDlgItem(dialog, IDC_SCH_STOP_TIME), enabled && stop);
}

// Shows one schedule in the controls.
void ShowSchedule(HWND dialog, const QueueSchedule& schedule) {
    CheckDlgButton(dialog, IDC_SCH_ENABLED, schedule.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckRadioButton(dialog, IDC_SCH_ONCE, IDC_SCH_DAILY,
                     schedule.daily ? IDC_SCH_DAILY : IDC_SCH_ONCE);
    SetDate(GetDlgItem(dialog, IDC_SCH_DATE), schedule.year, schedule.month, schedule.day);
    for (int i = 0; i < 7; ++i) {
        CheckDlgButton(dialog, IDC_SCH_DAY0 + i, schedule.days[i] ? BST_CHECKED : BST_UNCHECKED);
    }
    SetClock(GetDlgItem(dialog, IDC_SCH_START), schedule.startHour, schedule.startMinute);
    CheckDlgButton(dialog, IDC_SCH_STOP, schedule.stopEnabled ? BST_CHECKED : BST_UNCHECKED);
    SetClock(GetDlgItem(dialog, IDC_SCH_STOP_TIME), schedule.stopHour, schedule.stopMinute);
    SendDlgItemMessageW(dialog, IDC_SCH_DONE, CB_SETCURSEL, static_cast<int>(schedule.whenDone), 0);
    SyncEnabled(dialog);
}

// Reads the controls back into one schedule.
void ReadSchedule(HWND dialog, QueueSchedule* schedule) {
    schedule->enabled = IsDlgButtonChecked(dialog, IDC_SCH_ENABLED) == BST_CHECKED;
    schedule->daily = IsDlgButtonChecked(dialog, IDC_SCH_DAILY) == BST_CHECKED;
    SYSTEMTIME time = {};
    if (DateTime_GetSystemtime(GetDlgItem(dialog, IDC_SCH_DATE), &time) == GDT_VALID) {
        schedule->year = time.wYear;
        schedule->month = time.wMonth;
        schedule->day = time.wDay;
    }
    for (int i = 0; i < 7; ++i) {
        schedule->days[i] = IsDlgButtonChecked(dialog, IDC_SCH_DAY0 + i) == BST_CHECKED;
    }
    if (DateTime_GetSystemtime(GetDlgItem(dialog, IDC_SCH_START), &time) == GDT_VALID) {
        schedule->startHour = time.wHour;
        schedule->startMinute = time.wMinute;
    }
    schedule->stopEnabled = IsDlgButtonChecked(dialog, IDC_SCH_STOP) == BST_CHECKED;
    if (DateTime_GetSystemtime(GetDlgItem(dialog, IDC_SCH_STOP_TIME), &time) == GDT_VALID) {
        schedule->stopHour = time.wHour;
        schedule->stopMinute = time.wMinute;
    }
    int done = static_cast<int>(SendDlgItemMessageW(dialog, IDC_SCH_DONE, CB_GETCURSEL, 0, 0));
    schedule->whenDone = done == 1   ? QueueSchedule::WhenDone::Quit
                         : done == 2 ? QueueSchedule::WhenDone::Shutdown
                                     : QueueSchedule::WhenDone::Nothing;
}

// Whether an item of the queue is running, which turns Start now into Stop.
bool QueueRunning(const Editing& editing, QueueKind queue) {
    for (const DownloadItem& item : *editing.screen->items) {
        if (item.queue == queue && IsActive(item.status)) {
            return true;
        }
    }
    return false;
}

// Lists the files of the shown queue with their status.
void FillFiles(HWND dialog, const Editing& editing) {
    HWND list = GetDlgItem(dialog, IDC_SCH_FILES);
    ListView_DeleteAllItems(list);
    int row = 0;
    for (const DownloadItem& item : *editing.screen->items) {
        if (item.queue != editing.shown) {
            continue;
        }
        size_t cut = item.outPath.find_last_of(L"\\/");
        std::wstring name = cut == std::wstring::npos ? item.outPath : item.outPath.substr(cut + 1);
        LVITEMW entry = {};
        entry.mask = LVIF_TEXT;
        entry.iItem = row;
        entry.pszText = name.data();
        ListView_InsertItem(list, &entry);
        std::wstring status = StatusText(item);
        ListView_SetItemText(list, row, 1, status.data());
        ++row;
    }
    if (row == 0) {
        LVITEMW entry = {};
        entry.mask = LVIF_TEXT;
        entry.pszText = const_cast<wchar_t*>(Str(STR_SCH_EMPTY));
        ListView_InsertItem(list, &entry);
    }
    SetDlgItemTextW(dialog, IDC_SCH_RUN,
                    Str(QueueRunning(editing, editing.shown) ? STR_SCH_HALT : STR_SCH_RUN));
}

// Switches the window to another queue, keeping what was typed for the one
// it leaves.
void ShowQueue(HWND dialog, Editing& editing, QueueKind queue) {
    ReadSchedule(dialog, &editing.draft.Of(editing.shown));
    editing.shown = queue;
    ShowSchedule(dialog, editing.draft.Of(queue));
    FillFiles(dialog, editing);
}

// Applies the captions, fills the lists and shows the first queue.
void InitQueuesPage(HWND page, Editing& editing) {
    SetDialogText(page, IDC_SCH_LBL_QUEUE, STR_SCH_QUEUE);
    SetDialogText(page, IDC_SCH_ENABLED, STR_SCH_ENABLED);
    SetDialogText(page, IDC_SCH_ONCE, STR_SCH_ONCE);
    SetDialogText(page, IDC_SCH_DAILY, STR_SCH_DAILY);
    SetDialogText(page, IDC_SCH_LBL_START, STR_SCH_START);
    SetDialogText(page, IDC_SCH_STOP, STR_SCH_STOP);
    SetDialogText(page, IDC_SCH_LBL_DONE, STR_SCH_DONE);
    SetDialogText(page, IDC_SCH_LBL_FILES, STR_SCH_FILES);

    std::wstring days = Str(STR_SCH_DAYS);
    size_t from = 0;
    for (int i = 0; i < 7; ++i) {
        size_t bar = days.find(L'|', from);
        std::wstring day = days.substr(from, bar == std::wstring::npos ? bar : bar - from);
        SetDlgItemTextW(page, IDC_SCH_DAY0 + i, day.c_str());
        from = bar == std::wstring::npos ? days.size() : bar + 1;
    }

    for (StringId id : {STR_QUEUE_MAIN, STR_QUEUE_SCHEDULER}) {
        SendDlgItemMessageW(page, IDC_SCH_QUEUE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(Str(id)));
    }
    for (StringId id : {STR_SCH_DONE_NOTHING, STR_SCH_DONE_QUIT, STR_SCH_DONE_SHUTDOWN}) {
        SendDlgItemMessageW(page, IDC_SCH_DONE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(Str(id)));
    }
    for (int id : {IDC_SCH_START, IDC_SCH_STOP_TIME}) {
        DateTime_SetFormat(GetDlgItem(page, id), L"HH:mm");
    }

    HWND list = GetDlgItem(page, IDC_SCH_FILES);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
    RECT area = {};
    GetClientRect(list, &area);
    LVCOLUMNW column = {};
    column.mask = LVCF_WIDTH;
    column.cx = area.right * 2 / 3;
    ListView_InsertColumn(list, 0, &column);
    ListView_InsertColumn(list, 1, &column);
    ListView_SetColumnWidth(list, 1, LVSCW_AUTOSIZE_USEHEADER);

    editing.shown = editing.screen->initial;
    SendDlgItemMessageW(page, IDC_SCH_QUEUE, CB_SETCURSEL, IndexOf(editing.shown), 0);
    ShowSchedule(page, editing.draft.Of(editing.shown));
    FillFiles(page, editing);
}

// --- the follows page ----------------------------------------------------------

// A local moment as "day hh:mm", or a word when there is none.
std::wstring MomentText(std::time_t moment) {
    if (moment <= 0) {
        return Str(STR_FOL_SOON);
    }
    std::tm local = {};
    localtime_s(&local, &moment);
    wchar_t text[64] = {};
    wcsftime(text, 64, L"%d/%m %H:%M", &local);
    return text;
}

// The highest episode the source has shown for a follow, or a word.
std::wstring LastEpisodeText(const FollowedAnime& follow) {
    double last = follow.lastNumber;
    if (last < 0) {
        return Str(STR_FOL_NONE_YET);
    }
    wchar_t text[32] = {};
    swprintf(text, 32, last == static_cast<int>(last) ? L"%.0f" : L"%.1f", last);
    return text;
}

// "Lun 20:00" for the release moment of a follow.
std::wstring ReleaseText(const FollowedAnime& follow) {
    std::wstring days = Str(STR_SCH_DAYS);
    size_t from = 0;
    for (int i = 0; i < follow.releaseDay; ++i) {
        from = days.find(L'|', from) + 1;
    }
    size_t bar = days.find(L'|', from);
    wchar_t clock[16] = {};
    swprintf(clock, 16, L" %02d:%02d", follow.releaseHour, follow.releaseMinute);
    return days.substr(from, bar == std::wstring::npos ? bar : bar - from) + clock;
}

// Lists the follows with their state.
void FillFollows(HWND page, const Editing& editing) {
    HWND list = GetDlgItem(page, IDC_FOL_LIST);
    ListView_DeleteAllItems(list);
    int row = 0;
    for (const FollowedAnime& follow : editing.follows) {
        std::wstring title = Widen(follow.title);
        LVITEMW entry = {};
        entry.mask = LVIF_TEXT;
        entry.iItem = row;
        entry.pszText = title.data();
        ListView_InsertItem(list, &entry);
        std::wstring last = LastEpisodeText(follow);
        std::wstring release = ReleaseText(follow);
        std::wstring next = MomentText(follow.nextCheck);
        ListView_SetItemText(list, row, 1, last.data());
        ListView_SetItemText(list, row, 2, release.data());
        ListView_SetItemText(list, row, 3, next.data());
        ++row;
    }
    bool any = row > 0;
    for (int id : {IDC_FOL_EDIT, IDC_FOL_CHECK, IDC_FOL_REMOVE}) {
        EnableWindow(GetDlgItem(page, id), any);
    }
}

// The follow chosen in the list, or -1.
int ChosenFollow(HWND page) {
    return ListView_GetNextItem(GetDlgItem(page, IDC_FOL_LIST), -1, LVNI_SELECTED);
}

// Fills the captions and the columns of the follows page.
void InitFollowsPage(HWND page, Editing& editing) {
    SetDialogText(page, IDC_FOL_HINT, STR_FOL_HINT);
    SetDialogText(page, IDC_FOL_ADD, STR_FOL_ADD);
    SetDialogText(page, IDC_FOL_EDIT, STR_FOL_EDIT);
    SetDialogText(page, IDC_FOL_CHECK, STR_FOL_CHECK);
    SetDialogText(page, IDC_FOL_REMOVE, STR_FOL_REMOVE);

    HWND list = GetDlgItem(page, IDC_FOL_LIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
    RECT area = {};
    GetClientRect(list, &area);
    int total = area.right - GetSystemMetrics(SM_CXVSCROLL);
    int widths[4] = {total * 38 / 100, total * 17 / 100, total * 17 / 100, 0};
    widths[3] = total - widths[0] - widths[1] - widths[2];
    StringId titles[4] = {STR_FOL_COL_ANIME, STR_FOL_COL_LAST, STR_FOL_COL_RELEASE,
                          STR_FOL_COL_NEXT};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW column = {};
        column.mask = LVCF_WIDTH | LVCF_TEXT;
        column.cx = widths[i];
        column.pszText = const_cast<wchar_t*>(Str(titles[i]));
        ListView_InsertColumn(list, i, &column);
    }
    FillFollows(page, editing);
}

// Opens the follow dialog on a new follow, or on the chosen one.
void EditFollow(HWND page, Editing& editing, bool fresh) {
    FollowScreen screen;
    screen.choices = editing.screen->choices;
    FollowedAnime draft;
    int chosen = fresh ? -1 : ChosenFollow(page);
    if (!fresh && chosen < 0) {
        return;
    }
    if (!fresh) {
        draft = editing.follows[static_cast<size_t>(chosen)];
        screen.editing = true;
    } else {
        std::time_t now = std::time(nullptr);
        std::tm local = {};
        localtime_s(&local, &now);
        draft.releaseDay = (local.tm_wday + 6) % 7;
        draft.releaseHour = local.tm_hour;
        draft.releaseMinute = 0;
    }
    screen.follow = &draft;
    if (!ShowFollowDialog(GetParent(page), editing.instance, &screen)) {
        return;
    }
    if (fresh) {
        for (const FollowedAnime& other : editing.follows) {
            if (other.animeUrl == draft.animeUrl) {
                return;
            }
        }
        draft.nextCheck = 0;
        draft.primed = false;
        editing.follows.push_back(draft);
    } else {
        FollowedAnime& kept = editing.follows[static_cast<size_t>(chosen)];
        bool moved = kept.releaseDay != draft.releaseDay || kept.releaseHour != draft.releaseHour ||
                     kept.releaseMinute != draft.releaseMinute;
        kept = draft;
        if (moved && kept.primed) {
            kept.nextCheck = follow::NextRelease(kept, std::time(nullptr));
            kept.misses = 0;
        }
    }
    FillFollows(page, editing);
}

INT_PTR CALLBACK PageProc(HWND page, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        const Theme& theme = ActiveTheme();
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme.Colors().text);
        SetBkColor(dc, theme.Colors().window);
        return reinterpret_cast<INT_PTR>(theme.WindowBrush());
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        INT_PTR colour = 0;
        return ThemeDialogMessage(msg, wParam, &colour) ? colour : 0;
    }
    case WM_INITDIALOG: {
        SetWindowLongPtrW(page, GWLP_USERDATA, lParam);
        auto* editing = reinterpret_cast<Editing*>(lParam);
        ActiveTheme().ApplyToDialog(page);
        if (editing->creating == 0) {
            InitQueuesPage(page, *editing);
        } else {
            InitFollowsPage(page, *editing);
        }
        return FALSE;
    }
    case WM_COMMAND: {
        auto* editing = reinterpret_cast<Editing*>(GetWindowLongPtrW(page, GWLP_USERDATA));
        switch (LOWORD(wParam)) {
        case IDC_SCH_QUEUE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int index = static_cast<int>(
                    SendDlgItemMessageW(page, IDC_SCH_QUEUE, CB_GETCURSEL, 0, 0));
                ShowQueue(page, *editing, QueueAt(index));
            }
            return TRUE;
        case IDC_SCH_ENABLED:
        case IDC_SCH_ONCE:
        case IDC_SCH_DAILY:
        case IDC_SCH_STOP:
            SyncEnabled(page);
            return TRUE;
        case IDC_SCH_RUN:
            editing->screen->run(editing->shown, !QueueRunning(*editing, editing->shown));
            FillFiles(page, *editing);
            return TRUE;
        case IDC_FOL_ADD:
            EditFollow(page, *editing, true);
            return TRUE;
        case IDC_FOL_EDIT:
            EditFollow(page, *editing, false);
            return TRUE;
        case IDC_FOL_CHECK: {
            int chosen = ChosenFollow(page);
            if (chosen >= 0) {
                editing->follows[static_cast<size_t>(chosen)].nextCheck = 0;
                FillFollows(page, *editing);
            }
            return TRUE;
        }
        case IDC_FOL_REMOVE: {
            int chosen = ChosenFollow(page);
            if (chosen >= 0) {
                editing->follows.erase(editing->follows.begin() + chosen);
                FillFollows(page, *editing);
            }
            return TRUE;
        }
        default:
            return FALSE;
        }
    }
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->idFrom == IDC_FOL_LIST && header->code == NM_DBLCLK) {
            auto* editing = reinterpret_cast<Editing*>(GetWindowLongPtrW(page, GWLP_USERDATA));
            EditFollow(page, *editing, false);
            return TRUE;
        }
        return FALSE;
    }
    default:
        return FALSE;
    }
}

// Shows one page and hides the others.
void ShowPage(HWND dialog, Editing& editing, int index) {
    editing.tabs.SetPage(dialog, index);
    for (int i = 0; i < kPageCount; ++i) {
        ShowWindow(editing.pages[i], i == index ? SW_SHOW : SW_HIDE);
    }
}

// Creates both pages inside the body and shows the one asked for.
void CreatePages(HWND dialog, Editing& editing) {
    editing.tabs.Init(dialog, kStripUnits, kBodyUnits,
                      std::vector<StringId>(kPageTitles, kPageTitles + kPageCount));
    const RECT& body = editing.tabs.Body();
    for (int i = 0; i < kPageCount; ++i) {
        editing.creating = i;
        editing.pages[i] = CreateDialogParamW(editing.instance, MAKEINTRESOURCEW(kPageIds[i]),
                                              dialog, PageProc, reinterpret_cast<LPARAM>(&editing));
        SetWindowPos(editing.pages[i], HWND_TOP, body.left + 1, body.top + 1,
                     body.right - body.left - 2, body.bottom - body.top - 2, SWP_NOACTIVATE);
    }
    ShowPage(dialog, editing, editing.screen->initialPage);
}

// Dialog procedure of the frame: the tabs, OK and Cancel.
INT_PTR CALLBACK SchedulerProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    INT_PTR colour = 0;
    if (ThemeDialogMessage(msg, wParam, &colour)) {
        return colour;
    }
    auto* editing = reinterpret_cast<Editing*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        editing = reinterpret_cast<Editing*>(lParam);
        ActiveTheme().ApplyToDialog(dialog);
        SetDialogTitle(dialog, STR_SCH_TITLE);
        SetDialogText(dialog, IDOK, STR_DLG_OK);
        SetDialogText(dialog, IDCANCEL, STR_DLG_CANCEL);
        CreatePages(dialog, *editing);
        return TRUE;
    case WM_PAINT:
        editing->tabs.Paint(dialog);
        return TRUE;
    case WM_LBUTTONDOWN: {
        int tab = editing->tabs.HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if (tab >= 0 && tab != editing->tabs.Page()) {
            ShowPage(dialog, *editing, tab);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            ReadSchedule(editing->pages[0], &editing->draft.Of(editing->shown));
            *editing->screen->scheduler = editing->draft;
            *editing->screen->follows = editing->follows;
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

}  // namespace

// Shows the scheduler window over the schedules and follows; Cancel leaves
// them alone.
bool ShowSchedulerDialog(HWND owner, HINSTANCE instance, SchedulerScreen* screen) {
    Editing editing;
    editing.screen = screen;
    editing.instance = instance;
    editing.draft = *screen->scheduler;
    editing.follows = *screen->follows;
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SCHEDULER), owner, SchedulerProc,
                           reinterpret_cast<LPARAM>(&editing)) == IDOK;
}
