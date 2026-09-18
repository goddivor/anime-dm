#pragma once

#include <ctime>
#include <string>

#include "core/Download.h"

// When a queue starts and stops on its own, and what follows once it is
// done, as the scheduler window sets it.
struct QueueSchedule {
    enum class WhenDone { Nothing, Quit, Shutdown };

    bool enabled = false;
    bool daily = false;          // otherwise once, on the date below
    int year = 0;                // the date of a single run
    int month = 0;
    int day = 0;
    bool days[7] = {true, true, true, true, true, true, true};  // Monday first
    int startHour = 0;
    int startMinute = 0;
    bool stopEnabled = false;
    int stopHour = 0;
    int stopMinute = 0;
    WhenDone whenDone = WhenDone::Nothing;
};

// What the clock asks the window to do with a queue.
struct ScheduleAction {
    QueueKind queue = QueueKind::Main;
    bool start = true;  // otherwise stop
};

// Keeps the schedule of both queues and tells, once a minute, which one is
// due to start or stop.
class Scheduler {
public:
    QueueSchedule& Of(QueueKind queue) { return schedules_[Index(queue)]; }
    const QueueSchedule& Of(QueueKind queue) const { return schedules_[Index(queue)]; }

    // The actions due at `now`; each minute fires at most once per queue, and
    // a single run switches itself off after its start.
    std::vector<ScheduleAction> Tick(std::time_t now);

private:
    static int Index(QueueKind queue) { return queue == QueueKind::Scheduler ? 1 : 0; }

    QueueSchedule schedules_[2];
    std::time_t lastStart_[2] = {0, 0};
    std::time_t lastStop_[2] = {0, 0};
};

namespace schedule {

// Reads and writes `schedule.json` beside the settings.
void Load(Scheduler* scheduler);
void Save(const Scheduler& scheduler);

}  // namespace schedule
