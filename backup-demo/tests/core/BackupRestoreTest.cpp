#include "backup/core/FilesystemBackupManager.h"
#include "backup/core/FilesystemRestoreManager.h"
#include "backup/filter/DefaultFilterManager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace {

void writeText(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    if (!output) {
        throw std::runtime_error("Cannot write test file");
    }
}

std::string readText(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int fail(const fs::path& root, const std::string& message)
{
    std::cerr << message << '\n';
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    return 1;
}

} // namespace

int main()
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path()
        / ("data-backup-software-test-" + std::to_string(suffix));
    const fs::path source = root / "source";
    const fs::path repository = root / "repository";

    try {
        writeText(source / "docs" / "note.txt", "version one");
        writeText(source / "ignored.log", "do not back up");
        fs::create_directories(source / "empty-directory");

        auto filter = std::make_shared<backup::DefaultFilterManager>();
        backup::FilesystemBackupManager backupManager(filter);
        backup::FilesystemRestoreManager restoreManager;

        backup::BackupRequest request;
        request.sourceDirectory = source;
        request.destinationDirectory = repository;
        request.backupName = "integration";
        request.mode = backup::BackupMode::Full;
        request.filters.excludedExtensions = {".log"};
        request.verifyAfterCopy = true;

        bool sawCopyProgress = false;
        auto full = backupManager.backup(request, [&](const backup::ProgressInfo& progress) {
            sawCopyProgress = sawCopyProgress || progress.phase == backup::OperationPhase::Copying;
        });
        if (!full.succeeded() || !fs::is_directory(full.outputPath)) {
            return fail(root, "Full backup did not complete");
        }
        if (!sawCopyProgress) {
            return fail(root, "Backup did not report copy progress");
        }

        auto invalidRequest = request;
        invalidRequest.destinationDirectory = source / "nested-backup";
        if (backupManager.backup(invalidRequest).status != backup::OperationStatus::InvalidRequest) {
            return fail(root, "Backup accepted a destination inside the source directory");
        }

        auto cancelledRequest = request;
        cancelledRequest.backupName = "cancelled";
        if (backupManager.backup(cancelledRequest, {}, [] { return true; }).status
            != backup::OperationStatus::Cancelled) {
            return fail(root, "Backup did not honor cancellation");
        }

        writeText(source / "docs" / "note.txt", "version two, longer");
        writeText(source / "new.txt", "new file");
        request.mode = backup::BackupMode::Incremental;
        auto incremental = backupManager.backup(request);
        if (!incremental.succeeded() || incremental.outputPath == full.outputPath) {
            return fail(root, "Incremental backup did not complete");
        }

        backup::RestoreRequest restore;
        restore.backupDirectory = incremental.outputPath;
        restore.destinationDirectory = root / "restored-incremental";
        restore.verifyAfterRestore = true;
        auto restored = restoreManager.restore(restore);
        if (!restored.succeeded()
            || readText(restore.destinationDirectory / "docs" / "note.txt")
                != "version two, longer"
            || readText(restore.destinationDirectory / "new.txt") != "new file"
            || fs::exists(restore.destinationDirectory / "ignored.log")
            || !fs::is_directory(restore.destinationDirectory / "empty-directory")) {
            return fail(root, "Incremental restore did not reproduce the filtered source tree");
        }

        writeText(source / "docs" / "note.txt", "version three");
        request.mode = backup::BackupMode::Differential;
        auto differential = backupManager.backup(request);
        if (!differential.succeeded()) {
            return fail(root, "Differential backup did not complete");
        }

        restore.backupDirectory = differential.outputPath;
        restore.destinationDirectory = root / "restored-differential";
        restored = restoreManager.restore(restore);
        if (!restored.succeeded()
            || readText(restore.destinationDirectory / "docs" / "note.txt")
                != "version three"
            || readText(restore.destinationDirectory / "new.txt") != "new file") {
            return fail(root, "Differential restore did not resolve the backup chain");
        }

        writeText(restore.destinationDirectory / "new.txt", "keep me");
        restore.conflictPolicy = backup::ConflictPolicy::Skip;
        restored = restoreManager.restore(restore);
        if (!restored.succeeded()
            || restored.skippedFiles == 0
            || readText(restore.destinationDirectory / "new.txt") != "keep me") {
            return fail(root, "Skip conflict policy did not preserve an existing file");
        }

        restore.conflictPolicy = backup::ConflictPolicy::Rename;
        restored = restoreManager.restore(restore);
        if (!restored.succeeded()
            || !fs::exists(restore.destinationDirectory / "new (restored 1).txt")) {
            return fail(root, "Rename conflict policy did not create a separate file");
        }

        std::error_code cleanupError;
        fs::remove_all(root, cleanupError);
        return 0;
    } catch (const std::exception& exception) {
        return fail(root, std::string("Unexpected integration test error: ") + exception.what());
    }
}
