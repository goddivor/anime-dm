#pragma once

#include <windows.h>

#include <functional>
#include <vector>

#include "core/Download.h"
#include "core/Schedule.h"

// What the scheduler window works on: the schedules to edit, the items
// whose queues it lists, and the way to start or stop a queue at once.
struct SchedulerScreen {
    Scheduler* scheduler = nullptr;
    const std::vector<DownloadItem>* items = nullptr;
    std::function<void(QueueKind, bool)> run;
    QueueKind initial = QueueKind::Scheduler;
};

// Shows the scheduler window; true when OK changed the schedules.
bool ShowSchedulerDialog(HWND owner, HINSTANCE instance, SchedulerScreen* screen);
