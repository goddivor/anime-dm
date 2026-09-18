#pragma once

#include <memory>
#include <vector>

#include <windows.h>

#include "core/AddonStore.h"
#include "core/Download.h"
#include "core/Downloader.h"
#include "core/Follow.h"
#include "core/Http.h"
#include "core/Schedule.h"
#include "core/Settings.h"
#include "ui/AddDialog.h"
#include "ui/DownloadsView.h"
#include "ui/FollowDialog.h"
#include "ui/MenuBar.h"
#include "ui/Sidebar.h"
#include "ui/Theme.h"
#include "ui/Toolbar.h"

struct PosterPayload;
struct IconPayload;
struct FollowPayload;
struct ImportPayload;

// Which items the list shows, as chosen in the categories panel.
struct ListFilter {
    enum class Kind { All, Anime, QueueMain, QueueScheduler };
    Kind kind = Kind::All;
    std::string animeUrl;
};

// Top-level application window backed by a registered Win32 window class.
class MainWindow {
public:
    bool Create(HINSTANCE instance, const wchar_t* title);
    // Opens the add window on an address handed in from outside (the browser
    // extension, or a second instance started with --add).
    void AddFromOutside(const std::string& url, const std::string& episode = std::string());
    void Show(int cmdShow);
    HWND Handle() const { return hwnd_; }
    HACCEL Accelerator() const { return accel_; }

private:
    static LRESULT CALLBACK WndProcTrampoline(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void OnCreate();
    void OnDestroy();
    void OnCommand(int commandId);
    void ShowSoon(int commandId);
    void OnAddDownload(const std::string& initialUrl = std::string(),
                       const std::string& initialEpisode = std::string());
    void PublishSources();
    void ApplySettings();
    void OnDownloadEvent(std::unique_ptr<DownloadEvent> event);
    void OnPosterEvent(std::unique_ptr<PosterPayload> payload);
    void OnIconEvent(std::unique_ptr<IconPayload> payload);
    void OnFollowEvent(std::unique_ptr<FollowPayload> payload);
    void OnImportEvent(std::unique_ptr<ImportPayload> payload);
    LRESULT OnSidebarNotify(NMHDR* notify);
    void OnSidebarSelect(const SidebarNode* node);
    void OnSidebarContext();
    void ApplyTheme();
    void Retranslate();
    LRESULT OnToolbarCustomDraw(NMTBCUSTOMDRAW* draw);
    LRESULT OnListCustomDraw(NMLVCUSTOMDRAW* draw);
    void OnColumnClick(int column);
    void ApplySort();
    void DrawRow(NMLVCUSTOMDRAW* draw);
    bool DrawProgressCell(HDC dc, const RECT& cell, uint64_t id, bool selected);
    void OnContextMenu(HWND target, int x, int y);
    void Relayout();
    RECT SplitterRect() const;
    bool OnSetCursor();
    void OnLeftButtonDown(int x);
    void OnMouseMove(int x);
    void OnLeftButtonUp();
    void CancelSplitterDrag();
    void DrawTracker(int x);
    void ApplyUiFont();

    // --- the queue ---
    DownloadItem* Find(uint64_t id);
    DownloadTask TaskOf(const DownloadItem& item) const;
    void Refresh(const DownloadItem& item);
    void Persist();
    void UpdateActions();
    void StartItem(DownloadItem& item, bool fresh);
    void ResumeSelected();
    // Shows the downloads menu for the selection and carries out its choice.
    void RunDownloadsMenu(int x, int y);
    // Restarts the stopped and failed items of the selection through a player.
    void ResumeSelectedWith(const std::string& player);
    void StopSelected();
    void RedownloadSelected();
    void RemoveSelected();
    void StopAll();
    void DeleteAll();
    // --- the queues and their clock ---
    void StartQueue(QueueKind queue);
    void StopQueue(QueueKind queue);
    void MoveSelectedTo(QueueKind queue);
    void MoveAnimeTo(const std::string& url, QueueKind queue);
    void OpenScheduler();
    void OnScheduleTick();
    void FinishScheduledRun(QueueKind queue);
    // --- export and import ---
    void ExportList(int format);
    void ImportList(int format);
    // --- the followed animes ---
    void AddEpisodes(const AddRequest& request, QueueKind queue, bool start);
    void FollowAnime(const std::string& url);
    std::vector<FollowChoice> FollowChoices() const;
    void CheckFollows();
    void CheckFollow(const FollowedAnime& follow);
    void RemoveCompleted();
    void OpenSelected(bool folder);
    // Selects an item in the list, lifting the filter when it hides it.
    void RevealItem(uint64_t id);
    void ShowNotice(const wchar_t* message);
    int ChosenSkin() const;
    void ChooseSkin(int index);

    // --- the anime groups and the categories panel ---
    AnimeGroup* FindGroup(const std::string& url);
    bool Visible(const DownloadItem& item) const;
    void FillList();
    void RebuildSidebar();
    void PruneGroups();
    void LoadPosters();
    void FetchPoster(const AnimeGroup& group);
    void OpenAnime(const std::string& url);
    void OpenAnimeFolder(const std::string& url);
    void DeleteAnime(const std::string& url);
    std::wstring FolderOfAnime(const std::string& url) const;
    void DecorateFolder(const std::string& url, const std::string& chosenTemplate);
    void ApplyIcon(const std::string& url, const std::string& templateId, bool aniyomi,
                   bool announce);

    HWND hwnd_ = nullptr;
    HFONT uiFont_ = nullptr;
    HACCEL accel_ = nullptr;
    Http http_;
    AddonStore store_{http_};
    Downloader downloader_{http_, store_};
    std::vector<DownloadItem> items_;
    std::vector<AnimeGroup> groups_;
    std::vector<ToolbarSkin> skins_;
    ListFilter filter_;
    Settings settings_;
    Scheduler scheduler_;
    std::vector<FollowedAnime> follows_;
    std::vector<std::string> checking_;
    bool scheduledRun_[2] = {false, false};
    int sortColumn_ = -1;  // the column the rows follow, or none
    bool sortAscending_ = true;
    uint64_t nextId_ = 1;
    MenuBar menuBar_;
    Toolbar toolbar_;
    Sidebar sidebar_;
    DownloadsView downloads_;
    int themeCommand_ = 0;
    int languageCommand_ = 0;
    int sidebarWidth_ = 230;
    bool sidebarVisible_ = true;
    bool draggingSplitter_ = false;
    int trackX_ = 0;  // where the tracker bar stands during a drag
};
