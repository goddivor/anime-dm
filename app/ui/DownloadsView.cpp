#include "ui/DownloadsView.h"

#include <commctrl.h>

#include "core/Text.h"
#include "ui/Format.h"
#include "ui/Strings.h"

namespace {
struct Column {
    StringId title;
    int width;
};

constexpr Column kColumns[] = {
    {STR_COL_FILENAME, 320},
    {STR_COL_SIZE, 90},
    {STR_COL_STATUS, 140},
    {STR_COL_TIME_LEFT, 110},
    {STR_COL_SPEED, 110},
    {STR_COL_LAST_TRY, 130},
    {STR_COL_ADDED, 130},
};

enum ColumnIndex {
    COL_FILENAME,
    COL_SIZE,
    COL_STATUS,
    COL_TIME_LEFT,
    COL_SPEED,
    COL_LAST_TRY,
    COL_ADDED,
};

// What the interface says about a failure.
StringId ErrorText(DownloadError error) {
    switch (error) {
    case DownloadError::Source:
        return STR_ERR_SOURCE;
    case DownloadError::NoPlayer:
        return STR_ERR_NO_PLAYER;
    case DownloadError::NoVideo:
        return STR_ERR_NO_VIDEO;
    case DownloadError::Playlist:
        return STR_ERR_PLAYLIST;
    case DownloadError::Key:
        return STR_ERR_KEY;
    case DownloadError::Disk:
        return STR_ERR_DISK;
    default:
        return STR_ERR_NETWORK;
    }
}

// Sets one cell of a row.
void SetCell(HWND list, int row, int column, const std::wstring& text) {
    ListView_SetItemText(list, row, column, const_cast<wchar_t*>(text.c_str()));
}

// Keeps the vertical scroll bar on screen even while the rows fit, the way
// IDM frames its list. The control drops the bar whenever it recomputes its
// range; putting it back disabled, right after, restores the frame.
LRESULT CALLBACK KeepScrollBar(HWND list, UINT msg, WPARAM wParam, LPARAM lParam,
                               UINT_PTR, DWORD_PTR) {
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(list, KeepScrollBar, 1);
        return DefSubclassProc(list, msg, wParam, lParam);
    }
    LRESULT result = DefSubclassProc(list, msg, wParam, lParam);

    static thread_local bool restoring = false;
    if (!restoring && (GetWindowLongPtrW(list, GWL_STYLE) & WS_VSCROLL) == 0) {
        restoring = true;
        // SetScrollInfo only disables a bar that is still there: a bar the
        // control removed has to be shown again before its empty range makes
        // Windows disable it rather than remove it.
        ShowScrollBar(list, SB_VERT, TRUE);
        SCROLLINFO info = {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
        info.nMin = 0;
        info.nMax = 0;
        info.nPage = 1;
        SetScrollInfo(list, SB_VERT, &info, TRUE);
        restoring = false;
    }
    return result;
}

}  // namespace

// The text of the status cell.
std::wstring StatusText(const DownloadItem& item) {
    switch (item.status) {
    case DownloadStatus::Queued:
        return Str(STR_STATUS_PENDING);
    case DownloadStatus::Resolving:
        return Str(STR_STATUS_RESOLVING);
    case DownloadStatus::Downloading:
        if (item.fraction < 0.0) {
            return Str(STR_STATUS_STARTING);
        }
        return std::to_wstring(static_cast<int>(item.fraction * 100.0)) + L" %";
    case DownloadStatus::Assembling:
        return Str(STR_STATUS_ASSEMBLING);
    case DownloadStatus::Completed:
        return Str(STR_STATUS_COMPLETED);
    case DownloadStatus::Failed: {
        std::wstring text = Str(STR_STATUS_FAILED);
        text += L" : ";
        text += Str(ErrorText(item.error));
        if (!item.detail.empty()) {
            text += L" (" + Widen(item.detail) + L")";
        }
        return text;
    }
    case DownloadStatus::Stopped:
        return Str(STR_STATUS_STOPPED);
    }
    return std::wstring();
}

// Creates the ListView child and configures its columns and extended styles.
bool DownloadsView::Create(HWND parent, HINSTANCE instance) {
    hwnd_ = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0,
        parent, nullptr, instance, nullptr);
    if (hwnd_ == nullptr) {
        return false;
    }

    ListView_SetExtendedListViewStyle(
        hwnd_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    AddColumns();
    SetWindowSubclass(hwnd_, KeepScrollBar, 1, 0);
    return true;
}

// Inserts the report columns in declaration order.
void DownloadsView::AddColumns() {
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    int index = 0;
    for (const Column& column : kColumns) {
        col.iSubItem = index;
        col.cx = column.width;
        col.pszText = const_cast<wchar_t*>(Str(column.title));
        ListView_InsertColumn(hwnd_, index, &col);
        ++index;
    }
}

// Inserts the row of an item, or refreshes it when it is already there.
void DownloadsView::Upsert(const DownloadItem& item) {
    int row = RowOf(item.id);
    if (row < 0) {
        LVITEMW entry = {};
        entry.mask = LVIF_TEXT | LVIF_PARAM;
        entry.iItem = ListView_GetItemCount(hwnd_);
        entry.pszText = const_cast<wchar_t*>(L"");
        entry.lParam = static_cast<LPARAM>(item.id);
        row = ListView_InsertItem(hwnd_, &entry);
    }
    Fill(row, item);
}

// Writes every cell of a row from its item.
void DownloadsView::Fill(int row, const DownloadItem& item) {
    SetCell(hwnd_, row, COL_FILENAME, FileNameOf(item.outPath));
    SetCell(hwnd_, row, COL_SIZE, format::Size(item.total > 0 ? item.total : item.done));
    SetCell(hwnd_, row, COL_STATUS, StatusText(item));

    bool downloading = item.status == DownloadStatus::Downloading;
    double remaining = -1.0;
    if (downloading && item.speed > 0.0) {
        if (item.total > item.done) {
            remaining = static_cast<double>(item.total - item.done) / item.speed;
        } else if (item.fraction > 0.0 && item.done > 0) {
            double estimated = static_cast<double>(item.done) / item.fraction;
            remaining = (estimated - static_cast<double>(item.done)) / item.speed;
        }
    }
    // Nothing is left to say about the time and the rate of a transfer that
    // is not running: the cells stay empty rather than holding a dash.
    SetCell(hwnd_, row, COL_TIME_LEFT, remaining < 0.0 ? L"" : format::Duration(remaining));
    SetCell(hwnd_, row, COL_SPEED,
            downloading && item.speed > 0.0 ? format::Speed(item.speed) : L"");
    SetCell(hwnd_, row, COL_LAST_TRY, format::Date(item.lastTry));
    SetCell(hwnd_, row, COL_ADDED, format::Date(item.addedAt));
}

// Drops the row of an item.
void DownloadsView::Remove(uint64_t id) {
    int row = RowOf(id);
    if (row >= 0) {
        ListView_DeleteItem(hwnd_, row);
    }
}

// Drops every row.
void DownloadsView::Clear() {
    ListView_DeleteAllItems(hwnd_);
}

// The ids of the selected rows, top to bottom.
std::vector<uint64_t> DownloadsView::Selected() const {
    std::vector<uint64_t> ids;
    int row = -1;
    while ((row = ListView_GetNextItem(hwnd_, row, LVNI_SELECTED)) >= 0) {
        ids.push_back(IdAt(row));
    }
    return ids;
}

// The row of an item, or -1.
int DownloadsView::RowOf(uint64_t id) const {
    LVFINDINFOW find = {};
    find.flags = LVFI_PARAM;
    find.lParam = static_cast<LPARAM>(id);
    return ListView_FindItem(hwnd_, -1, &find);
}

// The id of a row, or zero.
uint64_t DownloadsView::IdAt(int row) const {
    LVITEMW entry = {};
    entry.mask = LVIF_PARAM;
    entry.iItem = row;
    if (!ListView_GetItem(hwnd_, &entry)) {
        return 0;
    }
    return static_cast<uint64_t>(entry.lParam);
}

int DownloadsView::Count() const {
    return ListView_GetItemCount(hwnd_);
}

// Refreshes the column captions after a language change.
void DownloadsView::Retranslate() {
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT;
    int index = 0;
    for (const Column& column : kColumns) {
        col.pszText = const_cast<wchar_t*>(Str(column.title));
        ListView_SetColumn(hwnd_, index, &col);
        ++index;
    }
}

// Queues the move of the list into a deferred batch, without copying its
// old pixels, so the frame and the rules are painted afresh where it lands.
HDWP DownloadsView::Place(HDWP batch, int x, int y, int width, int height) {
    return DeferWindowPos(batch, hwnd_, nullptr, x, y, width, height,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}
