#include "backup/core/FilesystemBackupManager.h"
#include "backup/filter/DefaultFilterManager.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
namespace fs = std::filesystem;
void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
void write(const fs::path& path, const char* text) {
    fs::create_directories(path.parent_path()); std::ofstream(path) << text;
}
int main() {
    const auto root = fs::temp_directory_path() / ("backup-statistics-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto source = root / "source";
        write(source / "keep.txt", "keep"); write(source / "notes.md", "exclude by type");
        write(source / "ignored.log", "exclude by type");
        write(source / "cache/a.txt", "exclude entire subtree");
        write(source / "cache/nested/b.bin", "exclude entire subtree");
        fs::create_directories(source / "empty");
        auto filter = std::make_shared<backup::DefaultFilterManager>();
        backup::FilesystemBackupManager manager(filter);
        backup::BackupRequest request;
        request.sourceDirectory = source; request.destinationDirectory = root / "repository";
        request.filters.includedExtensions = {".txt"}; request.filters.excludedPaths = {"cache"};
        auto full = manager.backup(request);
        require(full.succeeded() && full.processedFiles == 1 && full.filteredFiles == 4
            && full.unchangedFiles == 0 && full.skippedFiles == 4 && full.uncountedExcludedPaths == 0,
            "Full statistics must include files under excluded nested directories, not directories themselves");
        request.mode = backup::BackupMode::Incremental;
        auto unchanged = manager.backup(request);
        require(unchanged.succeeded() && unchanged.processedFiles == 0 && unchanged.filteredFiles == 4
            && unchanged.unchangedFiles == 1 && unchanged.skippedFiles == 5,
            "No-change increment must count four filtered and one unchanged file");
        write(source / "keep.txt", "keep, modified"); write(source / "new.txt", "new");
        auto changed = manager.backup(request);
        require(changed.succeeded() && changed.processedFiles == 2 && changed.filteredFiles == 4
            && changed.unchangedFiles == 0 && changed.skippedFiles == 4,
            "Changed increment statistics are incorrect");
        request.mode = backup::BackupMode::Full;
        request.filters.includedExtensions = {".no-match"};
        auto none = manager.backup(request);
        require(none.succeeded() && none.processedFiles == 0 && none.filteredFiles == 6
            && none.skippedFiles == 6, "All-filtered snapshot statistics are incorrect");
        fs::remove_all(root);
        std::cout << "Filtered subtree counts, unchanged increments, changed increments and all-filtered counts passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
