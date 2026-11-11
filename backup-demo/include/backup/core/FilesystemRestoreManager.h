#pragma once

#include "backup/core/RestoreManager.h"

namespace backup {

class FilesystemRestoreManager final : public RestoreManager {
public:
    FilesystemRestoreManager() = default;

    [[nodiscard]] OperationResult restore(
        const RestoreRequest& request,
        ProgressCallback progressCallback = {},
        CancellationCheck cancellationCheck = {}) override;
};

} // namespace backup
