#pragma once

#include "backup/scheduler/Scheduler.h"

#include <QObject>

#include <memory>

namespace backup {

class QtScheduler final : public QObject, public Scheduler {
public:
    explicit QtScheduler(QObject* parent = nullptr);
    ~QtScheduler() override;

    [[nodiscard]] bool add(const ScheduledBackup& schedule) override;
    [[nodiscard]] bool update(const ScheduledBackup& schedule) override;
    [[nodiscard]] bool remove(const std::string& id) override;
    [[nodiscard]] std::vector<ScheduledBackup> schedules() const override;

    void setTriggerCallback(ScheduledBackupCallback callback) override;
    void start() override;
    void stop() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace backup
