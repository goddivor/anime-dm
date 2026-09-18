#include "core/Schedule.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <vector>

#include "core/Paths.h"
#include "third_party/json.hpp"

namespace {

constexpr const char* kQueueNames[2] = {"main", "scheduler"};

// The minute a time falls in, as a whole number of minutes since the epoch.
std::time_t MinuteOf(std::time_t time) {
    return time / 60;
}

// Whether the schedule names this local time as a moment to act, for the
// given hour and minute of the day.
bool Due(const QueueSchedule& schedule, const std::tm& local, int hour, int minute) {
    if (local.tm_hour != hour || local.tm_min != minute) {
        return false;
    }
    if (schedule.daily) {
        int monday = (local.tm_wday + 6) % 7;
        return schedule.days[monday];
    }
    return local.tm_year + 1900 == schedule.year && local.tm_mon + 1 == schedule.month &&
           local.tm_mday == schedule.day;
}

nlohmann::json ToJson(const QueueSchedule& schedule) {
    return {
        {"enabled", schedule.enabled},
        {"daily", schedule.daily},
        {"year", schedule.year},
        {"month", schedule.month},
        {"day", schedule.day},
        {"days", std::vector<bool>(schedule.days, schedule.days + 7)},
        {"startHour", schedule.startHour},
        {"startMinute", schedule.startMinute},
        {"stopEnabled", schedule.stopEnabled},
        {"stopHour", schedule.stopHour},
        {"stopMinute", schedule.stopMinute},
        {"whenDone", static_cast<int>(schedule.whenDone)},
    };
}

void FromJson(const nlohmann::json& json, QueueSchedule* schedule) {
    if (!json.is_object()) {
        return;
    }
    schedule->enabled = json.value("enabled", false);
    schedule->daily = json.value("daily", false);
    schedule->year = json.value("year", 0);
    schedule->month = json.value("month", 0);
    schedule->day = json.value("day", 0);
    auto days = json.find("days");
    if (days != json.end() && days->is_array() && days->size() == 7) {
        for (size_t i = 0; i < 7; ++i) {
            schedule->days[i] = (*days)[i].is_boolean() ? (*days)[i].get<bool>() : true;
        }
    }
    schedule->startHour = json.value("startHour", 0);
    schedule->startMinute = json.value("startMinute", 0);
    schedule->stopEnabled = json.value("stopEnabled", false);
    schedule->stopHour = json.value("stopHour", 0);
    schedule->stopMinute = json.value("stopMinute", 0);
    int whenDone = json.value("whenDone", 0);
    schedule->whenDone = whenDone == 1   ? QueueSchedule::WhenDone::Quit
                         : whenDone == 2 ? QueueSchedule::WhenDone::Shutdown
                                         : QueueSchedule::WhenDone::Nothing;
}

}  // namespace

// Compares the local time with both schedules and names what is due.
std::vector<ScheduleAction> Scheduler::Tick(std::time_t now) {
    std::vector<ScheduleAction> actions;
    std::tm local = {};
    localtime_s(&local, &now);
    std::time_t minute = MinuteOf(now);
    for (int i = 0; i < 2; ++i) {
        QueueSchedule& schedule = schedules_[i];
        QueueKind queue = i == 1 ? QueueKind::Scheduler : QueueKind::Main;
        if (!schedule.enabled) {
            continue;
        }
        if (lastStart_[i] != minute && Due(schedule, local, schedule.startHour, schedule.startMinute)) {
            lastStart_[i] = minute;
            actions.push_back({queue, true});
            if (!schedule.daily) {
                schedule.enabled = false;
            }
        }
        if (schedule.stopEnabled && lastStop_[i] != minute &&
            Due(schedule, local, schedule.stopHour, schedule.stopMinute)) {
            lastStop_[i] = minute;
            actions.push_back({queue, false});
        }
    }
    return actions;
}

namespace schedule {

// Reads the file back; a missing or broken file leaves the defaults.
void Load(Scheduler* scheduler) {
    std::wstring path = paths::ScheduleFile();
    if (path.empty()) {
        return;
    }
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return;
    }
    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (!root.is_object()) {
        return;
    }
    FromJson(root.value("main", nlohmann::json()), &scheduler->Of(QueueKind::Main));
    FromJson(root.value("scheduler", nlohmann::json()), &scheduler->Of(QueueKind::Scheduler));
}

// Writes the file, whole.
void Save(const Scheduler& scheduler) {
    std::wstring path = paths::ScheduleFile();
    if (path.empty()) {
        return;
    }
    nlohmann::json root = {
        {kQueueNames[0], ToJson(scheduler.Of(QueueKind::Main))},
        {kQueueNames[1], ToJson(scheduler.Of(QueueKind::Scheduler))},
    };
    std::wstring temp = path + L".tmp";
    {
        std::ofstream file(std::filesystem::path(temp), std::ios::binary | std::ios::trunc);
        if (!file) {
            return;
        }
        file << root.dump(2);
    }
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

}  // namespace schedule
