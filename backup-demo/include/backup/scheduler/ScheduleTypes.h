#pragma once

#include "backup/core/BackupTypes.h"

#include <chrono>
#include <string>

namespace backup {

enum class ScheduleFrequency {
    Once,
    Daily,
    Weekly,
    CustomInterval
};

struct ScheduledBackup {
    std::string id;
    std::string name;
    BackupRequest request;
    ScheduleFrequency frequency{ScheduleFrequency::Once};
    std::chrono::system_clock::time_point nextRun{};
    std::chrono::minutes customInterval{0};
    bool enabled{true};
};

} // namespace backup
