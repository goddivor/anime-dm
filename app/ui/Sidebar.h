#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>
#include <commctrl.h>

#include "core/Download.h"

class Theme;

// Colours the caption bar paints itself with.
struct SidebarHeaderState {
    HFONT font;
    COLORREF surface;
    COLORREF text;
    COLORREF line;
    COLORREF frame;  // the outline of the panel
    COLORREF hover;
    bool hovered;   // the pointer rests on the close box
    bool tracking;  // a leave notification is pending
};

// What a row of the tree stands for.
enum class SidebarNodeKind {
    All,
    Anime,
    Episode,
    Separator,  // a rule between the animes and the queues
    Queues,
    QueueMain,
    QueueScheduler,
};

struct SidebarNode {
    SidebarNodeKind kind = SidebarNodeKind::All;
    std::string animeUrl;  // for an anime and its episodes
    uint64_t itemId = 0;   // for an episode
};

// Owns the left-hand categories panel: a caption bar plus the tree that lists
// every anime of the queue with its poster, its episodes and their state.
//
// The tree is rebuilt from the model on demand; the window that owns the
// model receives the clicks through the usual tree notifications and reads
// the row behind them with NodeOf.
class Sidebar {
public:
    ~Sidebar();

    bool Create(HWND parent, HINSTANCE instance);
    // Queues the moves of the caption bar and the tree into a deferred batch.
    HDWP Place(HDWP batch, int x, int y, int width, int height);
    void SetVisible(bool visible);
    void Retranslate();
    void ApplyTheme(const Theme& theme);

    // Rebuilds the rows from the model, keeping the selection when its row
    // survives.
    void Rebuild(const std::vector<AnimeGroup>& groups, const std::vector<DownloadItem>& items);

    // Hands the poster of an anime to the panel, which decodes and keeps it.
    void SetPoster(const std::string& animeUrl, const std::vector<uint8_t>& bytes);
    void DropPoster(const std::string& animeUrl);

    const SidebarNode* NodeOf(HTREEITEM item) const;
    const SidebarNode* Selected() const;
    void Select(const SidebarNode& node);

    // Paints the anime rows, which the tree cannot draw itself.
    LRESULT OnCustomDraw(NMTVCUSTOMDRAW* draw);

    // Folds a row when the press lands on its box; true when it did.
    bool OnTreePress(POINT point);

    // Whether the rows are being rebuilt, in which case the selection
    // notifications the tree sends mean nothing.
    bool Busy() const { return busy_; }

    HWND Handle() const { return tree_; }
    HWND HeaderHandle() const { return header_; }

private:
    struct Node : SidebarNode {
        HTREEITEM handle = nullptr;
        std::wstring title;
        int icon = 0;
        int count = 0;
    };

    HTREEITEM Insert(HTREEITEM parent, const wchar_t* text, int icon, Node* node, int integral);
    Node* Add(SidebarNodeKind kind, const std::string& animeUrl, uint64_t itemId);
    void DrawAnimeRow(NMTVCUSTOMDRAW* draw, const Node& node);
    void DrawSimpleRow(NMTVCUSTOMDRAW* draw, const Node& node);
    void DrawSeparator(NMTVCUSTOMDRAW* draw);
    void DrawTies(HDC dc, HTREEITEM item, const RECT& row, int level, bool expander);
    POINT ExpanderCentre(const RECT& row, int level) const;
    void RebuildIcons(const Theme& theme);

    HWND header_ = nullptr;
    HWND tree_ = nullptr;
    HIMAGELIST icons_ = nullptr;
    SidebarHeaderState headerState_ = {};
    std::vector<std::unique_ptr<Node>> nodes_;
    std::map<std::string, HBITMAP> posters_;
    std::vector<AnimeGroup> groups_;
    std::vector<DownloadItem> items_;
    bool busy_ = false;
};
