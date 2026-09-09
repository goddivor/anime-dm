#include "ui/Sidebar.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>

#include "core/Image.h"
#include "core/Text.h"
#include "ui/Commands.h"
#include "ui/IconFactory.h"
#include "ui/Strings.h"
#include "ui/Theme.h"

namespace {
constexpr wchar_t kHeaderClass[] = L"AnimeDmCategoriesHeader";
constexpr int kHeaderHeight = 24;
constexpr int kCloseBoxSize = 20;
constexpr int kRowHeight = 20;
constexpr int kAnimeIntegral = 3;  // an anime row is three plain rows tall
constexpr int kPosterWidth = 34;
constexpr int kPosterHeight = 48;
constexpr int kPosterRadius = 3;
constexpr int kGlyph = 16;

// Returns the square that holds the header close button.
RECT CloseBoxRect(HWND header) {
    RECT client = {};
    GetClientRect(header, &client);
    int top = (client.bottom - kCloseBoxSize) / 2;
    RECT box = {client.right - kCloseBoxSize - 4, top, client.right - 4, top + kCloseBoxSize};
    return box;
}

// Paints the caption bar: label on the left, close cross on the right.
void PaintHeader(HWND header) {
    auto* state = reinterpret_cast<SidebarHeaderState*>(GetWindowLongPtrW(header, GWLP_USERDATA));
    if (state == nullptr) {
        return;
    }

    PAINTSTRUCT paint = {};
    HDC dc = BeginPaint(header, &paint);

    RECT client = {};
    GetClientRect(header, &client);
    HBRUSH background = CreateSolidBrush(state->surface);
    FillRect(dc, &client, background);
    DeleteObject(background);

    // The frame of the panel starts here: its upper edge and both sides, the
    // tree below carrying the rest.
    HPEN pen = CreatePen(PS_SOLID, 1, state->frame);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, client.left, client.bottom, nullptr);
    LineTo(dc, client.left, client.top);
    LineTo(dc, client.right - 1, client.top);
    LineTo(dc, client.right - 1, client.bottom);

    HFONT oldFont = nullptr;
    if (state->font != nullptr) {
        oldFont = static_cast<HFONT>(SelectObject(dc, state->font));
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, state->text);
    RECT label = client;
    label.left += 8;
    DrawTextW(dc, Str(STR_SIDEBAR_TITLE), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (oldFont != nullptr) {
        SelectObject(dc, oldFont);
    }

    RECT box = CloseBoxRect(header);
    if (state->hovered) {
        HBRUSH lit = CreateSolidBrush(state->hover);
        FillRect(dc, &box, lit);
        DeleteObject(lit);
        HBRUSH edge = CreateSolidBrush(state->frame);
        FrameRect(dc, &box, edge);
        DeleteObject(edge);
    }

    HPEN cross = CreatePen(PS_SOLID, 1, state->text);
    SelectObject(dc, cross);
    MoveToEx(dc, box.left + 6, box.top + 6, nullptr);
    LineTo(dc, box.right - 6, box.bottom - 6);
    MoveToEx(dc, box.right - 7, box.top + 6, nullptr);
    LineTo(dc, box.left + 5, box.bottom - 6);

    SelectObject(dc, oldPen);
    DeleteObject(cross);
    DeleteObject(pen);
    EndPaint(header, &paint);
}

// Window procedure for the caption bar; the close cross hides the panel.
LRESULT CALLBACK HeaderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SidebarHeaderState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT:
        PaintHeader(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFONT:
        if (state != nullptr) {
            state->font = reinterpret_cast<HFONT>(wParam);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETFONT:
        return state != nullptr ? reinterpret_cast<LRESULT>(state->font) : 0;
    case WM_LBUTTONDOWN: {
        POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT box = CloseBoxRect(hwnd);
        if (PtInRect(&box, point)) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, ID_VIEW_CATEGORIES, 0);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (state == nullptr) {
            return 0;
        }
        POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT box = CloseBoxRect(hwnd);
        bool over = PtInRect(&box, point) != FALSE;
        if (over != state->hovered) {
            state->hovered = over;
            InvalidateRect(hwnd, &box, FALSE);
        }
        if (!state->tracking) {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
            state->tracking = TrackMouseEvent(&track) != FALSE;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (state != nullptr) {
            state->tracking = false;
            if (state->hovered) {
                state->hovered = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// Registers the caption bar class once per process.
void EnsureHeaderClass(HINSTANCE instance) {
    WNDCLASSEXW existing = {};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(instance, kHeaderClass, &existing)) {
        return;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    // The close cross is laid out from the right edge, so the whole bar has to
    // repaint when the splitter changes its width.
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = HeaderProc;
    wc.hInstance = instance;
    wc.lpszClassName = kHeaderClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
}

// The glyph that tells the state of an episode.
int StatusGlyph(DownloadStatus status) {
    switch (status) {
    case DownloadStatus::Downloading:
    case DownloadStatus::Assembling:
        return CAT_DOWNLOADING;
    case DownloadStatus::Completed:
        return CAT_DONE;
    case DownloadStatus::Failed:
        return CAT_FAILED;
    case DownloadStatus::Stopped:
        return CAT_STOPPED;
    default:
        return CAT_WAITING;
    }
}

// The caption of an episode row.
std::wstring EpisodeCaption(const DownloadItem& item) {
    if (item.movie) {
        return Str(STR_CAT_MOVIE);
    }
    return L"Ep " + EpisodeLabel(item.episodeNumber);
}

// A caption followed by a count in brackets.
std::wstring Counted(const wchar_t* caption, size_t count) {
    return std::wstring(caption) + L" (" + std::to_wstring(count) + L")";
}

// Whether two rows stand for the same thing.
bool Same(const SidebarNode& a, const SidebarNode& b) {
    return a.kind == b.kind && a.animeUrl == b.animeUrl && a.itemId == b.itemId;
}

// Paints a bitmap with its alpha channel onto a device context.
void BlendBitmap(HDC dc, HBITMAP bitmap, int x, int y, int width, int height) {
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP previous = static_cast<HBITMAP>(SelectObject(memory, bitmap));
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, width, height, memory, 0, 0, width, height, blend);
    SelectObject(memory, previous);
    DeleteDC(memory);
}
}  // namespace

// Releases the image list and the posters.
Sidebar::~Sidebar() {
    if (icons_ != nullptr) {
        ImageList_Destroy(icons_);
        icons_ = nullptr;
    }
    for (auto& [url, bitmap] : posters_) {
        DeleteObject(bitmap);
    }
    posters_.clear();
}

// Creates the caption bar and the tree, then shows the fixed categories.
bool Sidebar::Create(HWND parent, HINSTANCE instance) {
    EnsureHeaderClass(instance);

    headerState_.surface = GetSysColor(COLOR_BTNFACE);
    headerState_.text = GetSysColor(COLOR_BTNTEXT);
    headerState_.line = GetSysColor(COLOR_BTNSHADOW);
    headerState_.frame = GetSysColor(COLOR_BTNTEXT);
    headerState_.hover = GetSysColor(COLOR_BTNHIGHLIGHT);

    header_ = CreateWindowExW(
        0, kHeaderClass, nullptr, WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, parent, nullptr, instance, nullptr);
    SetWindowLongPtrW(header_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&headerState_));

    tree_ = CreateWindowExW(
        0, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASBUTTONS | TVS_LINESATROOT |
            TVS_FULLROWSELECT | TVS_NONEVENHEIGHT,
        0, 0, 0, 0, parent, nullptr, instance, nullptr);
    if (tree_ == nullptr) {
        return false;
    }
    TreeView_SetItemHeight(tree_, kRowHeight);

    RebuildIcons(ActiveTheme());
    Rebuild({}, {});
    return true;
}

// Draws the glyphs in the colours of a palette.
void Sidebar::RebuildIcons(const Theme& theme) {
    const ThemeColors& colors = theme.Colors();
    CategoryPalette palette{colors.text, colors.muted, colors.accent, colors.ok, colors.bad};
    HIMAGELIST previous = icons_;
    icons_ = CreateCategoryImageList(palette);
    SendMessageW(tree_, TVM_SETIMAGELIST, TVSIL_NORMAL, reinterpret_cast<LPARAM>(icons_));
    if (previous != nullptr) {
        ImageList_Destroy(previous);
    }
}

// Records a row and hands back its description, which the tree item points to.
Sidebar::Node* Sidebar::Add(SidebarNodeKind kind, const std::string& animeUrl, uint64_t itemId) {
    auto node = std::make_unique<Node>();
    node->kind = kind;
    node->animeUrl = animeUrl;
    node->itemId = itemId;
    nodes_.push_back(std::move(node));
    return nodes_.back().get();
}

// Appends a row under a parent and returns its handle.
HTREEITEM Sidebar::Insert(HTREEITEM parent, const wchar_t* text, int icon, Node* node,
                          int integral) {
    TVINSERTSTRUCTW insert = {};
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.itemex.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_INTEGRAL;
    insert.itemex.pszText = const_cast<wchar_t*>(text);
    insert.itemex.iImage = icon;
    insert.itemex.iSelectedImage = icon;
    insert.itemex.lParam = reinterpret_cast<LPARAM>(node);
    insert.itemex.iIntegral = integral;
    auto handle = reinterpret_cast<HTREEITEM>(
        SendMessageW(tree_, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert)));
    node->handle = handle;
    return handle;
}

// Rebuilds the rows from the model.
void Sidebar::Rebuild(const std::vector<AnimeGroup>& groups,
                      const std::vector<DownloadItem>& items) {
    groups_ = groups;
    items_ = items;

    SidebarNode remembered;
    if (const SidebarNode* selected = Selected()) {
        remembered = *selected;
    }

    busy_ = true;
    SendMessageW(tree_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(tree_, TVM_DELETEITEM, 0, reinterpret_cast<LPARAM>(TVI_ROOT));
    nodes_.clear();

    Node* all = Add(SidebarNodeKind::All, std::string(), 0);
    HTREEITEM allRoot =
        Insert(TVI_ROOT, Counted(Str(STR_CAT_ALL), items.size()).c_str(), CAT_FOLDER, all, 1);

    for (const AnimeGroup& group : groups) {
        std::vector<const DownloadItem*> episodes;
        for (const DownloadItem& item : items) {
            if (item.animeUrl == group.url) {
                episodes.push_back(&item);
            }
        }
        if (episodes.empty()) {
            continue;
        }
        std::stable_sort(episodes.begin(), episodes.end(),
                         [](const DownloadItem* a, const DownloadItem* b) {
                             return a->episodeNumber < b->episodeNumber;
                         });

        Node* anime = Add(SidebarNodeKind::Anime, group.url, 0);
        anime->title = Widen(group.title);
        anime->count = static_cast<int>(episodes.size());
        HTREEITEM parent = Insert(allRoot, anime->title.c_str(), CAT_ANIME, anime, kAnimeIntegral);

        for (const DownloadItem* item : episodes) {
            Node* episode = Add(SidebarNodeKind::Episode, group.url, item->id);
            episode->title = EpisodeCaption(*item);
            Insert(parent, episode->title.c_str(), StatusGlyph(item->status), episode, 1);
        }
        if (group.expanded) {
            SendMessageW(tree_, TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(parent));
        }
    }

    SendMessageW(tree_, TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(allRoot));

    Node* rule = Add(SidebarNodeKind::Separator, std::string(), 0);
    Insert(TVI_ROOT, L"", CAT_FOLDER, rule, 1);

    Node* queues = Add(SidebarNodeKind::Queues, std::string(), 0);
    HTREEITEM queueRoot = Insert(TVI_ROOT, Str(STR_CAT_QUEUE), CAT_QUEUE, queues, 1);
    Node* main = Add(SidebarNodeKind::QueueMain, std::string(), 0);
    Insert(queueRoot, Counted(Str(STR_QUEUE_MAIN), items.size()).c_str(), CAT_QUEUE, main, 1);
    Node* scheduler = Add(SidebarNodeKind::QueueScheduler, std::string(), 0);
    Insert(queueRoot, Counted(Str(STR_QUEUE_SCHEDULER), 0).c_str(), CAT_TIMER, scheduler, 1);
    SendMessageW(tree_, TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(queueRoot));

    Select(remembered);
    SendMessageW(tree_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(tree_, nullptr, TRUE);
    busy_ = false;
}

// Hands the poster of an anime to the panel.
void Sidebar::SetPoster(const std::string& animeUrl, const std::vector<uint8_t>& bytes) {
    HBITMAP bitmap = image::Cover(bytes, kPosterWidth, kPosterHeight, kPosterRadius);
    if (bitmap == nullptr) {
        return;
    }
    DropPoster(animeUrl);
    posters_[animeUrl] = bitmap;
    InvalidateRect(tree_, nullptr, FALSE);
}

// Forgets the poster of an anime.
void Sidebar::DropPoster(const std::string& animeUrl) {
    auto found = posters_.find(animeUrl);
    if (found != posters_.end()) {
        DeleteObject(found->second);
        posters_.erase(found);
    }
}

// The description behind a tree item, or nothing.
const SidebarNode* Sidebar::NodeOf(HTREEITEM item) const {
    if (item == nullptr) {
        return nullptr;
    }
    TVITEMW query = {};
    query.mask = TVIF_PARAM;
    query.hItem = item;
    if (!TreeView_GetItem(tree_, &query)) {
        return nullptr;
    }
    return reinterpret_cast<const Node*>(query.lParam);
}

// The description of the selected row, or nothing.
const SidebarNode* Sidebar::Selected() const {
    return NodeOf(TreeView_GetSelection(tree_));
}

// Selects the row that stands for a description, or the first row.
void Sidebar::Select(const SidebarNode& node) {
    for (const std::unique_ptr<Node>& candidate : nodes_) {
        if (Same(*candidate, node)) {
            TreeView_SelectItem(tree_, candidate->handle);
            return;
        }
    }
    if (!nodes_.empty()) {
        TreeView_SelectItem(tree_, nodes_.front()->handle);
    }
}

// Paints the anime rows; every other row is left to the tree.
LRESULT Sidebar::OnCustomDraw(NMTVCUSTOMDRAW* draw) {
    switch (draw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        const SidebarNode* node = NodeOf(reinterpret_cast<HTREEITEM>(draw->nmcd.dwItemSpec));
        if (node != nullptr && node->kind == SidebarNodeKind::Anime) {
            DrawAnimeRow(draw, *static_cast<const Node*>(node));
            return CDRF_SKIPDEFAULT;
        }
        if (node != nullptr && node->kind == SidebarNodeKind::Separator) {
            DrawSeparator(draw);
            return CDRF_SKIPDEFAULT;
        }
        return CDRF_DODEFAULT;
    }
    default:
        return CDRF_DODEFAULT;
    }
}

// Paints one anime row: chevron, poster, title and episode count.
void Sidebar::DrawAnimeRow(NMTVCUSTOMDRAW* draw, const Node& node) {
    HDC dc = draw->nmcd.hdc;
    auto item = reinterpret_cast<HTREEITEM>(draw->nmcd.dwItemSpec);
    RECT row = {};
    TreeView_GetItemRect(tree_, item, &row, FALSE);

    const ThemeColors& colors = ActiveTheme().Colors();
    bool selected = (draw->nmcd.uItemState & CDIS_SELECTED) != 0;
    bool focused = GetFocus() == tree_;
    COLORREF background = selected ? (focused ? colors.accent : colors.hover) : colors.window;
    COLORREF text = selected && focused ? colors.accentText : colors.text;
    COLORREF faint = selected && focused ? colors.accentText : colors.muted;

    HBRUSH brush = CreateSolidBrush(background);
    FillRect(dc, &row, brush);
    DeleteObject(brush);

    int indent = static_cast<int>(TreeView_GetIndent(tree_));
    int level = 0;
    for (HTREEITEM up = TreeView_GetParent(tree_, item); up != nullptr;
         up = TreeView_GetParent(tree_, up)) {
        ++level;
    }
    int left = row.left + indent * level;
    int middle = (row.top + row.bottom) / 2;
    bool expanded = (TreeView_GetItemState(tree_, item, TVIS_EXPANDED) & TVIS_EXPANDED) != 0;
    ImageList_Draw(icons_, expanded ? CAT_CHEVRON_DOWN : CAT_CHEVRON_RIGHT, dc,
                   left + (indent - kGlyph) / 2, middle - kGlyph / 2, ILD_TRANSPARENT);

    RECT box = {left + indent + 2, middle - kPosterHeight / 2, 0, 0};
    box.right = box.left + kPosterWidth;
    box.bottom = box.top + kPosterHeight;
    auto poster = posters_.find(node.animeUrl);
    if (poster != posters_.end()) {
        BlendBitmap(dc, poster->second, box.left, box.top, kPosterWidth, kPosterHeight);
    } else {
        HPEN pen = CreatePen(PS_SOLID, 1, colors.line);
        HPEN previousPen = static_cast<HPEN>(SelectObject(dc, pen));
        HBRUSH previousBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        RoundRect(dc, box.left, box.top, box.right, box.bottom, kPosterRadius * 2,
                  kPosterRadius * 2);
        SelectObject(dc, previousBrush);
        SelectObject(dc, previousPen);
        DeleteObject(pen);
        ImageList_Draw(icons_, CAT_ANIME, dc, box.left + (kPosterWidth - kGlyph) / 2,
                       middle - kGlyph / 2, ILD_TRANSPARENT);
    }

    HFONT font = reinterpret_cast<HFONT>(SendMessageW(tree_, WM_GETFONT, 0, 0));
    HFONT previousFont = font != nullptr ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    SetBkMode(dc, TRANSPARENT);

    std::wstring badge = std::to_wstring(node.count);
    SIZE badgeSize = {};
    GetTextExtentPoint32W(dc, badge.c_str(), static_cast<int>(badge.size()), &badgeSize);
    RECT badgeRect = {row.right - 8 - badgeSize.cx, row.top, row.right - 8, row.bottom};
    SetTextColor(dc, faint);
    DrawTextW(dc, badge.c_str(), -1, &badgeRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    RECT title = {box.right + 8, row.top, badgeRect.left - 6, row.bottom};
    SetTextColor(dc, text);
    DrawTextW(dc, node.title.c_str(), -1, &title,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    if (previousFont != nullptr) {
        SelectObject(dc, previousFont);
    }
}

// Paints the rule that parts the animes from the queues.
void Sidebar::DrawSeparator(NMTVCUSTOMDRAW* draw) {
    HDC dc = draw->nmcd.hdc;
    RECT row = {};
    TreeView_GetItemRect(tree_, reinterpret_cast<HTREEITEM>(draw->nmcd.dwItemSpec), &row, FALSE);

    const ThemeColors& colors = ActiveTheme().Colors();
    HBRUSH brush = CreateSolidBrush(colors.window);
    FillRect(dc, &row, brush);
    DeleteObject(brush);

    HPEN pen = CreatePen(PS_SOLID, 1, colors.line);
    HPEN previous = static_cast<HPEN>(SelectObject(dc, pen));
    int middle = (row.top + row.bottom) / 2;
    MoveToEx(dc, row.left + 6, middle, nullptr);
    LineTo(dc, row.right - 6, middle);
    SelectObject(dc, previous);
    DeleteObject(pen);
}

// Rebuilds the rows and repaints the caption in the active language.
void Sidebar::Retranslate() {
    Rebuild(groups_, items_);
    InvalidateRect(header_, nullptr, TRUE);
}

// Pushes the palette onto the caption bar, the tree and its glyphs.
void Sidebar::ApplyTheme(const Theme& theme) {
    const ThemeColors& colors = theme.Colors();
    headerState_.surface = colors.surface;
    headerState_.text = colors.text;
    headerState_.line = colors.line;
    headerState_.frame = colors.text;
    headerState_.hover = colors.hover;
    InvalidateRect(header_, nullptr, TRUE);

    RebuildIcons(theme);
    theme.ApplyToTree(tree_);
}

// Repositions the caption bar and the tree inside the panel bounds.
void Sidebar::SetBounds(int x, int y, int width, int height) {
    int headerHeight = height < kHeaderHeight ? height : kHeaderHeight;
    MoveWindow(header_, x, y, width, headerHeight, TRUE);
    MoveWindow(tree_, x, y + headerHeight, width, height - headerHeight, TRUE);
}

// Shows or hides the whole panel, caption bar included.
void Sidebar::SetVisible(bool visible) {
    ShowWindow(header_, visible ? SW_SHOW : SW_HIDE);
    ShowWindow(tree_, visible ? SW_SHOW : SW_HIDE);
}
