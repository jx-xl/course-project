#include "backup/scheduler/QtScheduler.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    QSettings().clear();
    return 1;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("DataBackupSoftwareTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SchedulerPersistence"));
    QTemporaryDir settingsDirectory;
    if (!settingsDirectory.isValid()) {
        std::cerr << "Cannot create a temporary settings directory\n";
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(
        QSettings::IniFormat,
        QSettings::UserScope,
        settingsDirectory.path());
    QSettings().clear();

    backup::ScheduledBackup schedule;
    schedule.id = "task-1";
    schedule.name = "Daily documents";
    schedule.request.sourceDirectory = "C:/source";
    schedule.request.destinationDirectory = "D:/backups";
    schedule.request.backupName = "documents";
    schedule.request.maximumSnapshots = 3;
    schedule.request.filters.excludedExtensions = {".tmp"};
    schedule.request.filters.includedNamePatterns = {u8"报告*.txt", "*.md"};
    schedule.request.filters.excludedNamePatterns = {u8"*草稿*"};
    schedule.request.filters.modifiedAfterMs = 1700000000000LL;
    schedule.request.filters.modifiedBeforeMs = 1900000000000LL;
    schedule.frequency = backup::ScheduleFrequency::Daily;
    schedule.nextRun = std::chrono::system_clock::now() + std::chrono::hours(1);

    {
        backup::QtScheduler scheduler;
        if (!scheduler.add(schedule) || scheduler.add(schedule)) {
            return fail("Scheduler should add a unique task exactly once");
        }
    }

    {
        backup::QtScheduler scheduler;
        auto items = scheduler.schedules();
        if (items.size() != 1 || items.front().name != schedule.name
            || items.front().request.backupName != schedule.request.backupName
            || items.front().request.maximumSnapshots != 3
            || items.front().request.filters.includedNamePatterns != schedule.request.filters.includedNamePatterns
            || items.front().request.filters.excludedNamePatterns != schedule.request.filters.excludedNamePatterns
            || items.front().request.filters.modifiedAfterMs != schedule.request.filters.modifiedAfterMs
            || items.front().request.filters.modifiedBeforeMs != schedule.request.filters.modifiedBeforeMs
            || items.front().request.filters.excludedExtensions != std::vector<std::string>{".tmp"}) {
            return fail("Scheduler did not persist and reload the complete task");
        }

        items.front().enabled = false;
        items.front().request.filters.includedNamePatterns.clear();
        items.front().request.filters.excludedNamePatterns.clear();
        items.front().request.filters.modifiedAfterMs.reset();
        items.front().request.filters.modifiedBeforeMs.reset();
        if (!scheduler.update(items.front()) || scheduler.schedules().front().enabled) {
            return fail("Scheduler did not update a task");
        }
        {
            backup::QtScheduler reloaded;
            const auto rules = reloaded.schedules().front().request.filters;
            if (!rules.includedNamePatterns.empty() || !rules.excludedNamePatterns.empty()
                || rules.modifiedAfterMs || rules.modifiedBeforeMs) {
                return fail("Cleared name/time filters remained in persistent settings");
            }
        }
        if (!scheduler.remove(schedule.id) || !scheduler.schedules().empty()) {
            return fail("Scheduler did not remove a task");
        }
    }

    backup::ScheduledBackup invalid = schedule;
    invalid.id = "invalid";
    invalid.frequency = backup::ScheduleFrequency::CustomInterval;
    invalid.customInterval = std::chrono::minutes(0);
    backup::QtScheduler scheduler;
    auto badRange = schedule;
    badRange.id = "invalid-range";
    badRange.request.filters.modifiedAfterMs = 2000;
    badRange.request.filters.modifiedBeforeMs = 1000;
    if (scheduler.add(badRange)) return fail("Scheduler accepted a reversed time range");
    if (scheduler.add(invalid)) {
        return fail("Scheduler accepted a non-positive custom interval");
    }

    auto due = schedule;
    due.id = "due-once";
    due.frequency = backup::ScheduleFrequency::Once;
    due.nextRun = std::chrono::system_clock::now() - std::chrono::seconds(1);
    int triggerCount = 0;
    scheduler.setTriggerCallback([&](const backup::ScheduledBackup& triggered) {
        if (triggered.id == due.id) {
            ++triggerCount;
            application.quit();
        }
    });
    if (!scheduler.add(due)) {
        return fail("Scheduler rejected a valid due task");
    }
    QTimer::singleShot(3000, &application, &QCoreApplication::quit);
    scheduler.start();
    application.exec();
    scheduler.stop();
    const auto afterTrigger = scheduler.schedules();
    if (triggerCount != 1 || afterTrigger.size() != 1 || afterTrigger.front().enabled) {
        return fail("A due one-time task was not triggered and disabled exactly once");
    }

    QSettings().clear();
    return 0;
}
