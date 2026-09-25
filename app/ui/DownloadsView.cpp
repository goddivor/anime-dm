#include "ui/DownloadsView.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <iterator>

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
    {STR_COL_LOCATION, 220},
    {STR_COL_ADDRESS, 280},
    {STR_COL_PARENT_PAGE, 260},
};

// Posted to the list itself once a column has been resized by hand.
constexpr UINT kColumnsSettled = WM_APP + 1;

enum ColumnIndex {
    COL_FILENAME,
    COL_SIZE,
    COL_STATUS,
    COL_TIME_LEFT,
    COL_SPEED,
    COL_LAST_TRY,
    COL_ADDED,
    COL_LOCATION,
    COL_ADDRESS,
    COL_PARENT_PAGE,
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

constexpr int kWheelStep = 40;  // pixels of sideways travel per wheel notch
constexpr int kMinColumnWidth = 40;  // a column can shrink, not vanish

// Whether the pointer of a wheel message rests on this window.
bool WheelIsOver(HWND window, LPARAM lParam) {
    POINT at = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    RECT bounds = {};
    GetWindowRect(window, &bounds);
    return PtInRect(&bounds, at) != FALSE;
}


// Keeps the vertical scroll bar on screen even while the rows fit, the way
// IDM frames its list, and repaints the whole list after any scroll. The
// control scrolls by shifting the pixels it already has and painting only the
// strip that appears, which leaves the rows painted by the window out of step
// with the header for a moment and smears them; a full repaint through the
// double buffer costs nothing visible and settles it. Ctrl or Shift with the
// wheel travels sideways, which the control does not do by itself.
constexpr wchar_t kHotProperty[] = L"AnimeDm.HotRow";

// The row under the pointer, kept as a property of the list; -1 for none.
int HotRowOf(HWND list) {
    return static_cast<int>(reinterpret_cast<intptr_t>(GetPropW(list, kHotProperty))) - 1;
}

// Remembers the row under the pointer and repaints the rows that change.
void SetHotRow(HWND list, int row) {
    int previous = HotRowOf(list);
    if (previous == row) {
        return;
    }
    SetPropW(list, kHotProperty, reinterpret_cast<HANDLE>(static_cast<intptr_t>(row + 1)));
    for (int changed : {previous, row}) {
        if (changed >= 0) {
            RECT bounds = {};
            if (ListView_GetItemRect(list, changed, &bounds, LVIR_BOUNDS)) {
                InvalidateRect(list, &bounds, FALSE);
            }
        }
    }
}

LRESULT CALLBACK KeepScrollBar(HWND list, UINT msg, WPARAM wParam, LPARAM lParam,
                               UINT_PTR, DWORD_PTR data) {
    if (msg == WM_NCDESTROY) {
        RemovePropW(list, kHotProperty);
        RemoveWindowSubclass(list, KeepScrollBar, 1);
        return DefSubclassProc(list, msg, wParam, lParam);
    }
    // The row under the pointer lights up, as in Explorer; the pointer
    // leaving the list puts it out.
    if (msg == WM_MOUSEMOVE) {
        LVHITTESTINFO hit = {};
        hit.pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ListView_SubItemHitTest(list, &hit);
        SetHotRow(list, hit.iItem);
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, list, 0};
        TrackMouseEvent(&track);
    } else if (msg == WM_MOUSELEAVE) {
        SetHotRow(list, -1);
    }
    // The dotted outline of the keyboard row shows only while the list has
    // the focus: the rows repaint when it comes and goes.
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        InvalidateRect(list, nullptr, FALSE);
    }
    // Ctrl+A takes every row, as in Explorer.
    if (msg == WM_KEYDOWN && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
        return 0;
    }
    if ((msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) && !WheelIsOver(list, lParam)) {
        return SendMessageW(GetParent(list), msg, wParam, lParam);
    }
    if (msg == kColumnsSettled) {
        auto* view = reinterpret_cast<DownloadsView*>(data);
        if (view != nullptr) {
            view->ColumnsResized();
        }
        return 0;
    }
    if (msg == WM_NOTIFY) {
        auto* header = reinterpret_cast<NMHEADERW*>(lParam);
        // The header applies the new width after these: the list reads it
        // once the message has gone through.
        UINT code = header->hdr.code;
        if (code == HDN_ENDTRACKW || code == HDN_ENDTRACKA || code == HDN_DIVIDERDBLCLICKW ||
            code == HDN_DIVIDERDBLCLICKA) {
            PostMessageW(list, kColumnsSettled, 0, 0);
        }
        if ((header->hdr.code == HDN_ITEMCHANGINGW || header->hdr.code == HDN_ITEMCHANGINGA) &&
            header->pitem != nullptr && (header->pitem->mask & HDI_WIDTH) != 0) {
            // A column being dragged narrower stops at the minimum instead of
            // closing on itself.
            HDC dc = GetDC(list);
            int least = MulDiv(kMinColumnWidth, GetDeviceCaps(dc, LOGPIXELSX), 96);
            ReleaseDC(list, dc);
            if (header->pitem->cxy < least) {
                header->pitem->cxy = least;
            }
        }
    }
    if (msg == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wParam) & (MK_CONTROL | MK_SHIFT)) != 0) {
        int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        ListView_Scroll(list, -notches * kWheelStep, 0);
        InvalidateRect(list, nullptr, FALSE);
        return 0;
    }

    int horizontal = GetScrollPos(list, SB_HORZ);
    int vertical = GetScrollPos(list, SB_VERT);
    LRESULT result = DefSubclassProc(list, msg, wParam, lParam);
    if (GetScrollPos(list, SB_HORZ) != horizontal || GetScrollPos(list, SB_VERT) != vertical) {
        InvalidateRect(list, nullptr, FALSE);
    }

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
// The row under the pointer, or -1.
int DownloadsView::HotRow() const {
    return HotRowOf(hwnd_);
}

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
    for (int column = 0; column < ColumnCount(); ++column) {
        shown_.push_back(column);
        widths_.push_back(kColumns[column].width);
    }
    AddColumns();
    SetWindowSubclass(hwnd_, KeepScrollBar, 1, reinterpret_cast<DWORD_PTR>(this));
    return true;
}

// Inserts the columns on screen, left to right, each at its kept width.
void DownloadsView::AddColumns() {
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    for (size_t position = 0; position < shown_.size(); ++position) {
        int id = shown_[position];
        col.iSubItem = static_cast<int>(position);
        col.cx = widths_[static_cast<size_t>(id)];
        col.pszText = const_cast<wchar_t*>(Str(kColumns[id].title));
        ListView_InsertColumn(hwnd_, static_cast<int>(position), &col);
    }
}

// How many columns the list knows, shown or not.
int DownloadsView::ColumnCount() {
    return static_cast<int>(std::size(kColumns));
}

// The caption of a column.
StringId DownloadsView::ColumnTitle(int column) {
    return kColumns[column].title;
}

// The column at a position on screen, or -1 past the last.
int DownloadsView::ColumnAt(int position) const {
    if (position < 0 || position >= static_cast<int>(shown_.size())) {
        return -1;
    }
    return shown_[static_cast<size_t>(position)];
}

// The position of a column on screen, or -1 while it is hidden.
int DownloadsView::PositionOf(int column) const {
    auto found = std::find(shown_.begin(), shown_.end(), column);
    return found == shown_.end() ? -1 : static_cast<int>(found - shown_.begin());
}

// Shows these columns, in this order. The file name always stays, first; a
// list that names nothing else, or names a column twice, is read as far as it
// makes sense. The rows go with the old columns: the caller fills them again.
void DownloadsView::SetShown(const std::vector<int>& columns) {
    std::vector<int> shown = {COL_FILENAME};
    for (int column : columns) {
        if (column > COL_FILENAME && column < ColumnCount() &&
            std::find(shown.begin(), shown.end(), column) == shown.end()) {
            shown.push_back(column);
        }
    }
    if (columns.empty()) {
        shown.clear();
        for (int column = 0; column < ColumnCount(); ++column) {
            shown.push_back(column);
        }
    }

    SyncWidths();
    ListView_DeleteAllItems(hwnd_);
    while (Header_GetItemCount(ListView_GetHeader(hwnd_)) > 0) {
        ListView_DeleteColumn(hwnd_, 0);
    }
    shown_ = std::move(shown);
    AddColumns();
}

// Gives the columns the widths kept from an earlier session; a missing or
// absurd width leaves the column as declared.
void DownloadsView::SetWidths(const std::vector<int>& widths) {
    for (int column = 0; column < ColumnCount() && column < static_cast<int>(widths.size());
         ++column) {
        int width = widths[static_cast<size_t>(column)];
        if (width >= kMinColumnWidth && width <= 4000) {
            widths_[static_cast<size_t>(column)] = width;
            int position = PositionOf(column);
            if (position >= 0) {
                ListView_SetColumnWidth(hwnd_, position, width);
            }
        }
    }
}

// The width of every column, shown or not, in declaration order.
std::vector<int> DownloadsView::Widths() const {
    return widths_;
}

// Reads the width of every column on screen back into the kept widths; a
// hidden column keeps the width it had when it went.
void DownloadsView::SyncWidths() {
    for (size_t position = 0; position < shown_.size(); ++position) {
        int width = ListView_GetColumnWidth(hwnd_, static_cast<int>(position));
        if (width > 0) {
            widths_[static_cast<size_t>(shown_[position])] = width;
        }
    }
}

// Tells the owner that the user has settled a column on a new width.
void DownloadsView::ColumnsResized() {
    SyncWidths();
    if (onResized_) {
        onResized_();
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

namespace {
// The text of one cell of an item.
std::wstring CellText(int column, const DownloadItem& item) {
    bool downloading = item.status == DownloadStatus::Downloading;
    switch (column) {
    case COL_FILENAME:
        return FileNameOf(item.outPath);
    case COL_SIZE:
        return format::Size(item.total > 0 ? item.total : item.done);
    case COL_STATUS:
        return StatusText(item);
    case COL_TIME_LEFT: {
        // Nothing is left to say about the time and the rate of a transfer
        // that is not running: the cells stay empty rather than holding a dash.
        double remaining = -1.0;
        if (downloading && item.speed > 0.0) {
            if (item.total > item.done) {
                remaining = static_cast<double>(item.total - item.done) / item.speed;
            } else if (item.fraction > 0.0 && item.done > 0) {
                double estimated = static_cast<double>(item.done) / item.fraction;
                remaining = (estimated - static_cast<double>(item.done)) / item.speed;
            }
        }
        return remaining < 0.0 ? std::wstring() : format::Duration(remaining);
    }
    case COL_SPEED:
        return downloading && item.speed > 0.0 ? format::Speed(item.speed) : std::wstring();
    case COL_LAST_TRY:
        return format::Date(item.lastTry);
    case COL_ADDED:
        return format::Date(item.addedAt);
    case COL_LOCATION: {
        size_t cut = item.outPath.find_last_of(L"\\/");
        return cut == std::wstring::npos ? std::wstring() : item.outPath.substr(0, cut);
    }
    case COL_ADDRESS:
        return Widen(item.pageUrl);
    case COL_PARENT_PAGE:
        return Widen(item.animeUrl);
    default:
        return std::wstring();
    }
}
}  // namespace

// Writes every cell of a row that is on screen from its item.
void DownloadsView::Fill(int row, const DownloadItem& item) {
    for (size_t position = 0; position < shown_.size(); ++position) {
        SetCell(hwnd_, row, static_cast<int>(position), CellText(shown_[position], item));
    }
}

// Marks the column the rows are sorted by, or none, in the header.
void DownloadsView::SetSortMark(int column, bool ascending) {
    int marked = PositionOf(column);
    HWND header = ListView_GetHeader(hwnd_);
    int count = Header_GetItemCount(header);
    for (int index = 0; index < count; ++index) {
        HDITEMW item = {};
        item.mask = HDI_FORMAT;
        Header_GetItem(header, index, &item);
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (index == marked) {
            item.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
        }
        Header_SetItem(header, index, &item);
    }
    InvalidateRect(header, nullptr, FALSE);
}

namespace {
// What the sort callback of the list is handed: the caller's ordering.
struct SortContext {
    const std::function<bool(uint64_t, uint64_t)>* before;
};

int CALLBACK CompareRows(LPARAM first, LPARAM second, LPARAM data) {
    const auto* context = reinterpret_cast<const SortContext*>(data);
    uint64_t a = static_cast<uint64_t>(first);
    uint64_t b = static_cast<uint64_t>(second);
    if ((*context->before)(a, b)) {
        return -1;
    }
    return (*context->before)(b, a) ? 1 : 0;
}
}  // namespace

// Reorders the rows; the items of the rows are handed to `before` by id.
void DownloadsView::Sort(const std::function<bool(uint64_t, uint64_t)>& before) {
    SortContext context = {&before};
    ListView_SortItems(hwnd_, CompareRows, reinterpret_cast<LPARAM>(&context));
    InvalidateRect(hwnd_, nullptr, FALSE);
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
    for (size_t position = 0; position < shown_.size(); ++position) {
        col.pszText = const_cast<wchar_t*>(Str(kColumns[shown_[position]].title));
        ListView_SetColumn(hwnd_, static_cast<int>(position), &col);
    }
}

// Queues the move of the list into a deferred batch, without copying its
// old pixels, so the frame and the rules are painted afresh where it lands.
HDWP DownloadsView::Place(HDWP batch, int x, int y, int width, int height) {
    return DeferWindowPos(batch, hwnd_, nullptr, x, y, width, height,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}
