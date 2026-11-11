#pragma once

#include "backup/scheduler/ScheduleTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace backup {

using ScheduledBackupCallback = std::function<void(const ScheduledBackup&)>;

// Scheduling contract implemented by C. Trigger callbacks should be dispatched
// without performing file I/O on the GUI thread.
class Scheduler {
public:
    virtual ~Scheduler() = default;

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    [[nodiscard]] virtual bool add(const ScheduledBackup& schedule) = 0;
    [[nodiscard]] virtual bool update(const ScheduledBackup& schedule) = 0;
    [[nodiscard]] virtual bool remove(const std::string& id) = 0;
    [[nodiscard]] virtual std::vector<ScheduledBackup> schedules() const = 0;

    virtual void setTriggerCallback(ScheduledBackupCallback callback) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

protected:
    Scheduler() = default;
};

} // namespace backup
