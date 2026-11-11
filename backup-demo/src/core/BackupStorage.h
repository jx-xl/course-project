#pragma once

#include "backup/core/BackupTypes.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace backup::detail {

inline constexpr const char* metadataFileName = "backup.meta";
inline constexpr const char* manifestFileName = "manifest.tsv";
inline constexpr const char* dataDirectoryName = "data";

struct ManifestEntry {
    std::filesystem::path relativePath;
    bool isDirectory{false};
    std::uintmax_t size{0};
    std::int64_t writeTime{0};
};

using Manifest = std::map<std::filesystem::path, ManifestEntry>;

struct BackupMetadata {
    int version{1};
    BackupMode mode{BackupMode::Full};
    std::int64_t createdAt{0};
    std::filesystem::path sourceDirectory;
    std::filesystem::path parentBackup;
    std::string backupName;
};

[[nodiscard]] bool isSafeRelativePath(const std::filesystem::path& path);
[[nodiscard]] bool isHiddenPath(const std::filesystem::path& path);
[[nodiscard]] std::int64_t modifiedTimeMilliseconds(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path weakCanonical(const std::filesystem::path& path);
[[nodiscard]] bool isPathInside(
    const std::filesystem::path& child,
    const std::filesystem::path& parent);

[[nodiscard]] std::int64_t serializeWriteTime(std::filesystem::file_time_type value);
[[nodiscard]] std::filesystem::file_time_type deserializeWriteTime(std::int64_t value);

void writeManifest(const std::filesystem::path& backupDirectory, const Manifest& manifest);
[[nodiscard]] Manifest readManifest(const std::filesystem::path& backupDirectory);

void writeMetadata(const std::filesystem::path& backupDirectory, const BackupMetadata& metadata);
[[nodiscard]] BackupMetadata readMetadata(const std::filesystem::path& backupDirectory);

[[nodiscard]] std::vector<std::filesystem::path> findBackups(
    const std::filesystem::path& repositoryDirectory,
    const std::filesystem::path& sourceDirectory);

[[nodiscard]] std::optional<std::filesystem::path> latestBackup(
    const std::filesystem::path& repositoryDirectory,
    const std::filesystem::path& sourceDirectory,
    std::optional<BackupMode> requiredMode = std::nullopt);

[[nodiscard]] std::filesystem::path makeBackupDirectory(
    const std::filesystem::path& repositoryDirectory,
    const std::string& requestedName);

[[nodiscard]] bool filesEqual(
    const std::filesystem::path& first,
    const std::filesystem::path& second);

// Only remove this source/name's old snapshots; retain every live chain dependency.
std::size_t pruneBackups(const BackupRequest& request, const std::filesystem::path& newest);

} // namespace backup::detail
