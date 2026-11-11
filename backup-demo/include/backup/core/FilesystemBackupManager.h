#pragma once

#include "backup/core/BackupManager.h"

#include <memory>

namespace backup {

class FilterManager;

class FilesystemBackupManager final : public BackupManager {
public:
    explicit FilesystemBackupManager(std::shared_ptr<const FilterManager> filterManager);

    [[nodiscard]] OperationResult backup(
        const BackupRequest& request,
        ProgressCallback progressCallback = {},
        CancellationCheck cancellationCheck = {}) override;

private:
    std::shared_ptr<const FilterManager> filterManager_;
};

} // namespace backup
