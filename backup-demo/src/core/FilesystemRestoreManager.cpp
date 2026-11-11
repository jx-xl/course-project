#include "backup/core/FilesystemRestoreManager.h"

#include "BackupStorage.h"

#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
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

std::optional<fs::path> findStoredFile(
    fs::path backupDirectory,
    const fs::path& relativePath)
{
    std::set<fs::path> visited;
    while (!backupDirectory.empty()) {
        backupDirectory = detail::weakCanonical(backupDirectory);
        if (!visited.insert(backupDirectory).second) {
            throw std::runtime_error("A cycle was found in the backup chain");
        }

        const auto candidate = backupDirectory / detail::dataDirectoryName / relativePath;
        std::error_code error;
        if (fs::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
        const auto metadata = detail::readMetadata(backupDirectory);
        backupDirectory = metadata.parentBackup;
    }
    return std::nullopt;
}

fs::path renamedDestination(const fs::path& requestedPath)
{
    const auto parent = requestedPath.parent_path();
    const auto stem = requestedPath.stem().u8string();
    const auto extension = requestedPath.extension().u8string();
    for (unsigned int number = 1;; ++number) {
        const auto name = stem + " (restored " + std::to_string(number) + ")" + extension;
        const auto candidate = parent / fs::u8path(name);
        if (!fs::exists(candidate)) {
            return candidate;
        }
    }
}

void copyFileWithProgress(
    const fs::path& source,
    const fs::path& destination,
    ProgressInfo& progress,
    const ProgressCallback& progressCallback,
    const CancellationCheck& cancellationCheck)
{
    fs::create_directories(destination.parent_path());
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!input || !output) {
        throw std::runtime_error("Cannot open a file for restore: " + destination.u8string());
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
                throw std::runtime_error("Failed while writing restored file: " + destination.u8string());
            }
            progress.processedBytes += static_cast<std::uintmax_t>(count);
            report(progressCallback, progress);
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("Failed while reading backup file: " + source.u8string());
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

OperationResult FilesystemRestoreManager::restore(
    const RestoreRequest& request,
    ProgressCallback progressCallback,
    CancellationCheck cancellationCheck)
{
    std::error_code error;
    if (request.backupDirectory.empty() || request.destinationDirectory.empty()) {
        return invalidResult("Backup and restore destination directories are required.");
    }
    if (!fs::is_directory(request.backupDirectory, error) || error) {
        return invalidResult("The selected backup directory does not exist.");
    }

    const auto backupDirectory = detail::weakCanonical(request.backupDirectory);
    const auto destinationDirectory = detail::weakCanonical(request.destinationDirectory);
    if (detail::isPathInside(destinationDirectory, backupDirectory)) {
        return invalidResult("The restore destination cannot be inside the backup directory.");
    }

    try {
        ProgressInfo progress;
        progress.phase = OperationPhase::Preparing;
        report(progressCallback, progress);

        (void)detail::readMetadata(backupDirectory);
        const auto manifest = detail::readManifest(backupDirectory);
        fs::create_directories(destinationDirectory);

        std::vector<detail::ManifestEntry> directories;
        std::vector<detail::ManifestEntry> files;
        for (const auto& [path, entry] : manifest) {
            (void)path;
            if (entry.isDirectory) {
                directories.push_back(entry);
            } else {
                files.push_back(entry);
                progress.totalBytes += entry.size;
            }
        }
        progress.totalFiles = files.size();

        for (const auto& directory : directories) {
            if (cancellationRequested(cancellationCheck)) {
                throw OperationStatus::Cancelled;
            }
            fs::create_directories(destinationDirectory / directory.relativePath);
        }

        progress.phase = OperationPhase::Copying;
        report(progressCallback, progress);
        OperationResult result;
        for (const auto& entry : files) {
            if (cancellationRequested(cancellationCheck)) {
                throw OperationStatus::Cancelled;
            }
            progress.currentPath = entry.relativePath;
            const auto storedFile = findStoredFile(backupDirectory, entry.relativePath);
            if (!storedFile) {
                throw std::runtime_error(
                    "The backup chain is missing: " + entry.relativePath.u8string());
            }

            auto destinationPath = destinationDirectory / entry.relativePath;
            if (fs::exists(destinationPath)) {
                if (request.conflictPolicy == ConflictPolicy::Skip) {
                    ++result.skippedFiles;
                    ++progress.processedFiles;
                    report(progressCallback, progress);
                    continue;
                }
                if (request.conflictPolicy == ConflictPolicy::Rename) {
                    destinationPath = renamedDestination(destinationPath);
                }
            }

            copyFileWithProgress(
                *storedFile,
                destinationPath,
                progress,
                progressCallback,
                cancellationCheck);
            if (request.preserveTimestamps) {
                fs::last_write_time(destinationPath, detail::deserializeWriteTime(entry.writeTime));
            }
            if (request.verifyAfterRestore && !detail::filesEqual(*storedFile, destinationPath)) {
                result.status = OperationStatus::VerificationFailed;
                result.message = "Restore verification failed for: " + entry.relativePath.u8string();
                return result;
            }
            ++progress.processedFiles;
            report(progressCallback, progress);
        }

        if (request.preserveTimestamps) {
            for (auto iterator = directories.rbegin(); iterator != directories.rend(); ++iterator) {
                const auto path = destinationDirectory / iterator->relativePath;
                std::error_code timestampError;
                fs::last_write_time(
                    path,
                    detail::deserializeWriteTime(iterator->writeTime),
                    timestampError);
            }
        }

        progress.phase = OperationPhase::Finalizing;
        report(progressCallback, progress);
        result.status = OperationStatus::Success;
        result.outputPath = destinationDirectory;
        result.processedFiles = progress.processedFiles;
        result.processedBytes = progress.processedBytes;
        result.message = "Restore completed successfully.";
        return result;
    } catch (OperationStatus status) {
        OperationResult result;
        result.status = status;
        result.message = "Restore was cancelled.";
        return result;
    } catch (const std::exception& exception) {
        OperationResult result;
        result.status = OperationStatus::IoError;
        result.message = exception.what();
        return result;
    }
}

} // namespace backup
