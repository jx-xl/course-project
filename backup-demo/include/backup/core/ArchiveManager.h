#pragma once
#include "backup/core/BackupTypes.h"

namespace backup {

struct PackRequest {
    fs::path backupDirectory;
    fs::path archiveFile;
};

struct UnpackRequest {
    fs::path archiveFile;
    fs::path destinationDirectory; // Parent for a newly created, independent snapshot.
};

// Self-contained DBPACK v1, implemented using the C++ standard library.
class ArchiveManager final {
public:
    OperationResult pack(const PackRequest& request, ProgressCallback progress = {},
        CancellationCheck cancelled = {}) const;
    OperationResult unpack(const UnpackRequest& request, ProgressCallback progress = {},
        CancellationCheck cancelled = {}) const;
};
}
