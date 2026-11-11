#include "backup/core/FilesystemBackupManager.h"
#include "backup/core/FilesystemRestoreManager.h"
#include "backup/filter/DefaultFilterManager.h"
#include "../../src/core/BackupStorage.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace fs = std::filesystem;
void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
void write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path()); std::ofstream(path, std::ios::binary) << value;
}
std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
int main() {
    const auto root = fs::temp_directory_path() / ("backup-retention-test-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        auto filter = std::make_shared<backup::DefaultFilterManager>();
        backup::FilesystemBackupManager manager(filter);
        backup::FilesystemRestoreManager restore;
        backup::BackupRequest request;
        request.sourceDirectory = root / fs::u8path(u8"中文源目录");
        request.destinationDirectory = root / fs::u8path(u8"中文仓库");
        request.backupName = "retention";
        request.maximumSnapshots = 2;
        request.verifyAfterCopy = true;
        write(request.sourceDirectory / fs::u8path(u8"子目录/示例.txt"), "original");
        write(request.destinationDirectory / "unrelated/keep.txt", "untouched");
        auto first = manager.backup(request); require(first.succeeded(), first.message.c_str());
        auto second = manager.backup(request); require(second.succeeded(), second.message.c_str());
        auto third = manager.backup(request); require(third.succeeded(), third.message.c_str());
        require(!fs::exists(first.outputPath) && fs::exists(second.outputPath)
            && fs::exists(third.outputPath), "Full retention should keep exactly the newest two");
        require(read(request.destinationDirectory / "unrelated/keep.txt") == "untouched", "Unrelated data changed");

        // A one-snapshot target must never remove the baseline required by an increment.
        request.maximumSnapshots = 1;
        request.mode = backup::BackupMode::Incremental;
        write(request.sourceDirectory / "new.txt", "new data");
        auto incremental = manager.backup(request); require(incremental.succeeded(), incremental.message.c_str());
        require(fs::exists(third.outputPath) && !fs::exists(second.outputPath), "Dependency retention failed");
        backup::RestoreRequest recovery;
        recovery.backupDirectory = incremental.outputPath;
        recovery.destinationDirectory = root / fs::u8path(u8"中文还原");
        recovery.verifyAfterRestore = true;
        auto restored = restore.restore(recovery); require(restored.succeeded(), restored.message.c_str());
        require(read(recovery.destinationDirectory / fs::u8path(u8"子目录/示例.txt")) == "original"
            && read(recovery.destinationDirectory / "new.txt") == "new data", "Incremental chain content mismatch");

        // A new independent full snapshot makes the complete older chain expendable.
        request.mode = backup::BackupMode::Full;
        auto final = manager.backup(request); require(final.succeeded(), final.message.c_str());
        require(!fs::exists(third.outputPath) && !fs::exists(incremental.outputPath)
            && fs::exists(final.outputPath), "Old chain was not retired after a new full backup");

        // Backups with another name are preserved, and broken metadata disables cleanup.
        request.backupName = "other";
        auto other = manager.backup(request); require(other.succeeded(), other.message.c_str());
        require(fs::exists(final.outputPath), "A different backup name was deleted");
        write(request.destinationDirectory / "broken/backup.meta", "invalid");
        auto warning = manager.backup(request); require(warning.succeeded(), "Cleanup warning lost successful backup");
        require(warning.message.find("Retention warning") != std::string::npos
            && fs::exists(other.outputPath), "Broken metadata should stop cleanup");
        fs::remove_all(root);
        std::cout << "Retention, dependency chains, Chinese paths, unrelated data and fail-safe cleanup passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\nTest evidence: " << root.u8string() << '\n';
        return 1;
    }
}
