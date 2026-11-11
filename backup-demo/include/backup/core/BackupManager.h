#pragma once

#include "backup/core/BackupTypes.h"

namespace backup {

// Implementations must invoke progressCallback from the calling thread unless
// their concrete class documents otherwise.
class BackupManager {
public:
    virtual ~BackupManager() = default;

    BackupManager(const BackupManager&) = delete;
    BackupManager& operator=(const BackupManager&) = delete;
    BackupManager(BackupManager&&) = delete;
    BackupManager& operator=(BackupManager&&) = delete;

    [[nodiscard]] virtual OperationResult backup(
        const BackupRequest& request,
        ProgressCallback progressCallback = {},
        CancellationCheck cancellationCheck = {}) = 0;

protected:
    BackupManager() = default;
};

} // namespace backup
