#pragma once

#include <windows.h>
#include <commctrl.h>

// Toolbar glyph indices; the order matches the image list built below.
enum ToolbarIcon {
    ICON_ADD,
    ICON_RESUME,
    ICON_STOP,
    ICON_RESUME_ALL,
    ICON_STOP_ALL,
    ICON_REMOVE,
    ICON_SETTINGS,
    ICON_SCHEDULE,
    ICON_DOWNLOADS,
    ICON_ADDONS,
    ICON_COUNT,
};

// Category tree glyph indices; the order matches the image list built below.
enum CategoryIcon {
    CAT_FOLDER,
    CAT_FOLDER_OPEN,
    CAT_VIDEO,
    CAT_PENDING,
    CAT_DONE,
    CAT_QUEUE,
    CAT_COUNT,
};

// Starts the GDI+ runtime used to render anti-aliased icons, for the process lifetime.
class GdiPlusRuntime {
public:
    GdiPlusRuntime();
    ~GdiPlusRuntime();

    GdiPlusRuntime(const GdiPlusRuntime&) = delete;
    GdiPlusRuntime& operator=(const GdiPlusRuntime&) = delete;

private:
    ULONG_PTR token_ = 0;
};

// Builds a 32x32 alpha-blended image list with the toolbar glyphs (no assets).
// The caller owns the returned list and must ImageList_Destroy it.
HIMAGELIST CreateToolbarImageList();

// Builds a 16x16 alpha-blended image list with the category tree glyphs.
// The caller owns the returned list and must ImageList_Destroy it.
HIMAGELIST CreateCategoryImageList();
