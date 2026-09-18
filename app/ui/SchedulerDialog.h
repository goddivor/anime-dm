#pragma once

#include <windows.h>

#include <functional>
#include <vector>

#include "core/Download.h"
#include "core/Follow.h"
#include "core/Schedule.h"
#include "ui/FollowDialog.h"

// What the scheduler window works on: the schedules and the follows to
// edit, the items whose queues it lists, the animes a follow may name, and
// the way to start or stop a queue at once.
struct SchedulerScreen {
    Scheduler* scheduler = nullptr;
    std::vector<FollowedAnime>* follows = nullptr;
    const std::vector<DownloadItem>* items = nullptr;
    std::vector<FollowChoice> choices;
    std::function<void(QueueKind, bool)> run;
    QueueKind initial = QueueKind::Scheduler;
    int initialPage = 0;  // 0 for the queues, 1 for the follows
};

// Shows the scheduler window; true when OK changed the schedules or the
// follows.
bool ShowSchedulerDialog(HWND owner, HINSTANCE instance, SchedulerScreen* screen);
