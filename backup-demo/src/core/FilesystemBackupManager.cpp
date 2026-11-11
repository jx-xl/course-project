#include "backup/core/FilesystemBackupManager.h"

#include "BackupStorage.h"
#include "backup/filter/FilterManager.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace backup {
namespace {

bool cancellationRequested(const CancellationCheck& check)
{
    return check && check();
}

void report(const ProgressCallback& callback, const ProgressInfo& progress)
{
    if (callback) {
        callback(progress);
    }
}

void countExcludedFiles(const fs::path& directory, std::uint64_t& filteredFiles,
    std::uint64_t& uncountedPaths, const CancellationCheck& cancellationCheck)
{
    // Count excluded descendants without opening their contents, following links,
    // or letting inaccessible excluded paths make the backup fail.
    std::vector<fs::path> pending{directory};
    while (!pending.empty()) {
        if (cancellationRequested(cancellationCheck)) throw OperationStatus::Cancelled;
        const auto current = pending.back(); pending.pop_back();
        std::error_code error;
        fs::directory_iterator item(current, fs::directory_options::none, error), end;
        if (error) { ++uncountedPaths; continue; }
        for (; item != end; item.increment(error)) {
            if (cancellationRequested(cancellationCheck)) throw OperationStatus::Cancelled;
            if (error) break;
            std::error_code statusError;
            const auto status = item->symlink_status(statusError);
            if (statusError) { ++uncountedPaths; continue; }
            if (fs::is_symlink(status)) continue;
            if (fs::is_regular_file(status)) ++filteredFiles;
            else if (fs::is_directory(status)) pending.push_back(item->path());
        }
        if (error) ++uncountedPaths;
    }
}

detail::Manifest scanSource(
    const fs::path& source,
    const FilterRules& rules,
    const FilterManager& filter,
    const ProgressCallback& progressCallback,
    const CancellationCheck& cancellationCheck,
    std::uint64_t& filteredFiles,
    std::uint64_t& uncountedExcludedPaths)
{
    detail::Manifest manifest;
    ProgressInfo progress;
    progress.phase = OperationPhase::Scanning;

    std::error_code iteratorError;
    fs::recursive_directory_iterator iterator(
        source,
        fs::directory_options::none,
        iteratorError);
    const fs::recursive_directory_iterator end;
    if (iteratorError) {
        throw fs::filesystem_error("Cannot scan source directory", source, iteratorError);
    }

    for (; iterator != end; iterator.increment(iteratorError)) {
        if (cancellationRequested(cancellationCheck)) {
            throw OperationStatus::Cancelled;
        }
        if (iteratorError) {
            throw fs::filesystem_error("Directory traversal failed", source, iteratorError);
        }

        std::error_code entryError;
        const auto status = iterator->symlink_status(entryError);
        if (entryError) {
            throw fs::filesystem_error("Cannot inspect source entry", iterator->path(), entryError);
        }
        if (fs::is_symlink(status)) {
            if (fs::is_directory(status)) {
                iterator.disable_recursion_pending();
            }
            continue;
        }

        const bool isDirectory = fs::is_directory(status);
        const bool isRegularFile = fs::is_regular_file(status);
        if (!isDirectory && !isRegularFile) {
            continue;
        }

        const auto relativePath = fs::relative(iterator->path(), source, entryError);
        if (entryError || !detail::isSafeRelativePath(relativePath)) {
            throw std::runtime_error("Cannot create a safe relative source path");
        }

        FileCandidate candidate;
        candidate.relativePath = relativePath;
        candidate.isDirectory = isDirectory;
        candidate.isHidden = detail::isHiddenPath(iterator->path());
        if (isRegularFile) {
            if (rules.modifiedAfterMs || rules.modifiedBeforeMs) {
                candidate.modifiedTimeMs = detail::modifiedTimeMilliseconds(iterator->path());
            }
            candidate.size = iterator->file_size(entryError);
            if (entryError) {
                throw fs::filesystem_error("Cannot read source file size", iterator->path(), entryError);
            }
        }

        if (!filter.shouldInclude(candidate, rules)) {
            if (isDirectory) {
                countExcludedFiles(iterator->path(), filteredFiles, uncountedExcludedPaths, cancellationCheck);
                iterator.disable_recursion_pending();
            } else ++filteredFiles;
            continue;
        }

        const auto writeTime = iterator->last_write_time(entryError);
        if (entryError) {
            throw fs::filesystem_error("Cannot read source timestamp", iterator->path(), entryError);
        }

        detail::ManifestEntry manifestEntry;
        manifestEntry.relativePath = relativePath;
        manifestEntry.isDirectory = isDirectory;
        manifestEntry.size = candidate.size;
        manifestEntry.writeTime = detail::serializeWriteTime(writeTime);
        manifest[relativePath] = manifestEntry;

        ++progress.processedFiles;
        progress.currentPath = relativePath;
        report(progressCallback, progress);
    }
    if (iteratorError) {
        throw fs::filesystem_error("Directory traversal failed", source, iteratorError);
    }
    return manifest;
}

bool entryChanged(
    const detail::ManifestEntry& current,
    const detail::Manifest& reference)
{
    const auto found = reference.find(current.relativePath);
    return found == reference.end()
        || found->second.isDirectory != current.isDirectory
        || found->second.size != current.size
        || found->second.writeTime != current.writeTime;
}

void copyFileWithProgress(
    const fs::path& source,
    const fs::path& destination,
    const detail::ManifestEntry& entry,
    bool preserveTimestamp,
    ProgressInfo& progress,
    const ProgressCallback& progressCallback,
    const CancellationCheck& cancellationCheck)
{
    fs::create_directories(destination.parent_path());
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!input || !output) {
        throw std::runtime_error("Cannot open a file for backup copy: " + source.u8string());
    }

    std::vector<char> buffer(256 * 1024);
    while (input) {
        if (cancellationRequested(cancellationCheck)) {
            throw OperationStatus::Cancelled;
        }
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            output.write(buffer.data(), count);
            if (!output) {
                throw std::runtime_error("Failed while writing backup file: " + destination.u8string());
            }
            progress.processedBytes += static_cast<std::uintmax_t>(count);
            report(progressCallback, progress);
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("Failed while reading source file: " + source.u8string());
    }
    output.close();
    if (preserveTimestamp) {
        fs::last_write_time(destination, detail::deserializeWriteTime(entry.writeTime));
    }
}

OperationResult invalidResult(std::string message)
{
    OperationResult result;
    result.status = OperationStatus::InvalidRequest;
    result.message = std::move(message);
    return result;
}

} // namespace

FilesystemBackupManager::FilesystemBackupManager(
    std::shared_ptr<const FilterManager> filterManager)
    : filterManager_(std::move(filterManager))
{
    if (!filterManager_) {
        throw std::invalid_argument("A FilterManager is required");
    }
}

OperationResult FilesystemBackupManager::backup(
    const BackupRequest& request,
    ProgressCallback progressCallback,
    CancellationCheck cancellationCheck)
{
    std::error_code error;
    if (request.sourceDirectory.empty() || request.destinationDirectory.empty()) {
        return invalidResult("Source and destination directories are required.");
    }
    if (!fs::is_directory(request.sourceDirectory, error) || error) {
        return invalidResult("The source directory does not exist or cannot be read.");
    }
    if (request.filters.maximumFileSize != 0
        && request.filters.minimumFileSize > request.filters.maximumFileSize) {
        return invalidResult("The minimum file size is greater than the maximum file size.");
    }
    if (request.filters.modifiedAfterMs && request.filters.modifiedBeforeMs
        && *request.filters.modifiedAfterMs > *request.filters.modifiedBeforeMs) {
        return invalidResult("The modification-time start is after the end.");
    }

    const auto sourceDirectory = detail::weakCanonical(request.sourceDirectory);
    const auto repositoryDirectory = detail::weakCanonical(request.destinationDirectory);
    if (detail::isPathInside(repositoryDirectory, sourceDirectory)) {
        return invalidResult("The backup destination cannot be inside the source directory.");
    }

    fs::path temporaryDirectory;
    try {
        fs::create_directories(repositoryDirectory);

        ProgressInfo progress;
        progress.phase = OperationPhase::Preparing;
        report(progressCallback, progress);

        BackupMode effectiveMode = request.mode;
        std::optional<fs::path> referenceBackup;
        if (request.mode == BackupMode::Incremental) {
            referenceBackup = detail::latestBackup(repositoryDirectory, sourceDirectory);
        } else if (request.mode == BackupMode::Differential) {
            referenceBackup = detail::latestBackup(
                repositoryDirectory,
                sourceDirectory,
                BackupMode::Full);
        }
        if (request.mode != BackupMode::Full && !referenceBackup) {
            effectiveMode = BackupMode::Full;
        }

        detail::Manifest referenceManifest;
        if (referenceBackup) {
            referenceManifest = detail::readManifest(*referenceBackup);
        }

        std::uint64_t filteredFiles = 0;
        std::uint64_t uncountedExcludedPaths = 0;
        const auto currentManifest = scanSource(
            sourceDirectory,
            request.filters,
            *filterManager_,
            progressCallback,
            cancellationCheck,
            filteredFiles,
            uncountedExcludedPaths);

        std::vector<detail::ManifestEntry> filesToCopy;
        for (const auto& [path, entry] : currentManifest) {
            (void)path;
            if (!entry.isDirectory
                && (effectiveMode == BackupMode::Full || entryChanged(entry, referenceManifest))) {
                filesToCopy.push_back(entry);
            }
        }
        const auto sourceFileCount = static_cast<std::uint64_t>(std::count_if(
            currentManifest.begin(),
            currentManifest.end(),
            [](const auto& item) { return !item.second.isDirectory; }));

        const std::string backupName = request.backupName.empty()
            ? sourceDirectory.filename().u8string()
            : request.backupName;
        const auto finalDirectory = detail::makeBackupDirectory(repositoryDirectory, backupName);
        temporaryDirectory = finalDirectory;
        temporaryDirectory += ".partial";
        fs::create_directories(temporaryDirectory / detail::dataDirectoryName);

        progress = {};
        progress.phase = OperationPhase::Copying;
        progress.totalFiles = filesToCopy.size();
        for (const auto& entry : filesToCopy) {
            progress.totalBytes += entry.size;
        }
        report(progressCallback, progress);

        OperationResult result;
        for (const auto& entry : filesToCopy) {
            if (cancellationRequested(cancellationCheck)) {
                throw OperationStatus::Cancelled;
            }
            progress.currentPath = entry.relativePath;
            const auto sourcePath = sourceDirectory / entry.relativePath;
            const auto destinationPath = temporaryDirectory / detail::dataDirectoryName
                / entry.relativePath;
            copyFileWithProgress(
                sourcePath,
                destinationPath,
                entry,
                request.preserveTimestamps,
                progress,
                progressCallback,
                cancellationCheck);
            if (request.verifyAfterCopy && !detail::filesEqual(sourcePath, destinationPath)) {
                result.status = OperationStatus::VerificationFailed;
                result.message = "Backup verification failed for: " + entry.relativePath.u8string();
                std::error_code cleanupError;
                fs::remove_all(temporaryDirectory, cleanupError);
                return result;
            }
            ++progress.processedFiles;
            report(progressCallback, progress);
        }

        progress.phase = OperationPhase::Finalizing;
        report(progressCallback, progress);
        detail::writeManifest(temporaryDirectory, currentManifest);

        detail::BackupMetadata metadata;
        metadata.mode = effectiveMode;
        metadata.createdAt = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (const auto previous = detail::latestBackup(repositoryDirectory, sourceDirectory)) {
            metadata.createdAt = std::max(metadata.createdAt,
                detail::readMetadata(*previous).createdAt + 1);
        }
        metadata.sourceDirectory = sourceDirectory;
        metadata.parentBackup = referenceBackup.value_or(fs::path{});
        metadata.backupName = backupName;
        detail::writeMetadata(temporaryDirectory, metadata);
        fs::rename(temporaryDirectory, finalDirectory);
        temporaryDirectory.clear();

        result.status = OperationStatus::Success;
        result.outputPath = finalDirectory;
        result.processedFiles = progress.processedFiles;
        result.filteredFiles = filteredFiles;
        result.unchangedFiles = sourceFileCount - result.processedFiles;
        result.skippedFiles = result.filteredFiles + result.unchangedFiles;
        result.uncountedExcludedPaths = uncountedExcludedPaths;
        result.processedBytes = progress.processedBytes;
        result.message = effectiveMode == request.mode
            ? "Backup completed successfully."
            : "No compatible base backup was found; a full backup was created successfully.";
        try {
            const auto removed = detail::pruneBackups(request, finalDirectory);
            if (request.maximumSnapshots != 0) {
                result.message += " Retention: removed " + std::to_string(removed)
                    + " old snapshot(s); required chain dependencies retained.";
            }
        } catch (const std::exception& exception) {
            result.message += " Retention warning: " + std::string(exception.what());
        }
        return result;
    } catch (OperationStatus status) {
        std::error_code cleanupError;
        if (!temporaryDirectory.empty()) {
            fs::remove_all(temporaryDirectory, cleanupError);
        }
        OperationResult result;
        result.status = status;
        result.message = "Backup was cancelled.";
        return result;
    } catch (const std::exception& exception) {
        std::error_code cleanupError;
        if (!temporaryDirectory.empty()) {
            fs::remove_all(temporaryDirectory, cleanupError);
        }
        OperationResult result;
        result.status = OperationStatus::IoError;
        result.message = exception.what();
        return result;
    }
}

} // namespace backup
