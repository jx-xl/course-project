#pragma once

#include "backup/core/BackupTypes.h"

namespace backup {

// Contract for rebuilding the original directory structure from a backup.
class RestoreManager {
public:
    virtual ~RestoreManager() = default;

    RestoreManager(const RestoreManager&) = delete;
    RestoreManager& operator=(const RestoreManager&) = delete;
    RestoreManager(RestoreManager&&) = delete;
    RestoreManager& operator=(RestoreManager&&) = delete;

    [[nodiscard]] virtual OperationResult restore(
        const RestoreRequest& request,
        ProgressCallback progressCallback = {},
        CancellationCheck cancellationCheck = {}) = 0;

protected:
    RestoreManager() = default;
};

} // namespace backup
