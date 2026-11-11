#include "backup/core/BackupManager.h"
#include "backup/core/RestoreManager.h"
#include "backup/filter/FilterManager.h"
#include "backup/scheduler/Scheduler.h"

#include <type_traits>

int main()
{
    static_assert(std::is_abstract_v<backup::BackupManager>);
    static_assert(std::is_abstract_v<backup::RestoreManager>);
    static_assert(std::is_abstract_v<backup::FilterManager>);
    static_assert(std::is_abstract_v<backup::Scheduler>);

    backup::BackupRequest backupRequest;
    backup::RestoreRequest restoreRequest;
    backup::FileCandidate candidate;
    backup::ScheduledBackup schedule;

    (void)backupRequest;
    (void)restoreRequest;
    (void)candidate;
    (void)schedule;
    return 0;
}
