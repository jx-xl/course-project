#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace backup {

namespace fs = std::filesystem;

enum class BackupMode {
    Full,
    Incremental,
    Differential
};

enum class ConflictPolicy {
    Overwrite,
    Skip,
    Rename
};

enum class OperationStatus {
    Success,
    Cancelled,
    InvalidRequest,
    IoError,
    VerificationFailed
};

enum class OperationPhase {
    Preparing,
    Scanning,
    Copying,
    Verifying,
    Finalizing,
    Packing,
    Unpacking
};

struct FilterRules {
    std::vector<std::string> includedExtensions;
    std::vector<std::string> excludedExtensions;
    std::vector<fs::path> excludedPaths;
    std::uintmax_t minimumFileSize{0};
    std::uintmax_t maximumFileSize{0}; // 0 means unlimited.
    bool includeHiddenFiles{false};
    std::vector<std::string> includedNamePatterns; // UTF-8 basename globs; * and ?.
    std::vector<std::string> excludedNamePatterns;
    std::optional<std::int64_t> modifiedAfterMs; // Inclusive Unix milliseconds.
    std::optional<std::int64_t> modifiedBeforeMs;
};

struct BackupRequest {
    fs::path sourceDirectory;
    fs::path destinationDirectory;
    std::string backupName;
    BackupMode mode{BackupMode::Full};
    FilterRules filters;
    bool preserveTimestamps{true};
    bool verifyAfterCopy{false};
    std::uint32_t maximumSnapshots{0}; // 0: unlimited; dependencies are always retained.
};

struct RestoreRequest {
    fs::path backupDirectory;
    fs::path destinationDirectory;
    ConflictPolicy conflictPolicy{ConflictPolicy::Overwrite};
    bool preserveTimestamps{true};
    bool verifyAfterRestore{false};
};

struct ProgressInfo {
    OperationPhase phase{OperationPhase::Preparing};
    fs::path currentPath;
    std::uint64_t processedFiles{0};
    std::uint64_t totalFiles{0};
    std::uintmax_t processedBytes{0};
    std::uintmax_t totalBytes{0};
};

struct OperationResult {
    OperationStatus status{OperationStatus::Success};
    std::string message;
    fs::path outputPath;
    std::uint64_t processedFiles{0};
    std::uint64_t skippedFiles{0};
    std::uint64_t filteredFiles{0};
    std::uint64_t unchangedFiles{0};
    std::uint64_t uncountedExcludedPaths{0};
    std::uintmax_t processedBytes{0};

    [[nodiscard]] bool succeeded() const noexcept
    {
        return status == OperationStatus::Success;
    }
};

using ProgressCallback = std::function<void(const ProgressInfo&)>;
using CancellationCheck = std::function<bool()>;

} // namespace backup
