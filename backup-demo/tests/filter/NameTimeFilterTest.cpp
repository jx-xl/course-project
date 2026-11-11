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
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
int main() {
    const auto root = fs::temp_directory_path() / ("name-time-filter-test-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        auto filter = std::make_shared<backup::DefaultFilterManager>();
        backup::FileCandidate file;
        backup::FilterRules rules;
        rules.includedNamePatterns = {"report*.txt", "*.md"};
        file.relativePath = "folder/Report-Final.TXT";
        require(filter->shouldInclude(file, rules), "ASCII case-insensitive basename wildcard failed");
        file.relativePath = "report.txt.bak";
        require(!filter->shouldInclude(file, rules), "Glob was not anchored to the complete name");
        file.relativePath = "folder/readme.md";
        require(filter->shouldInclude(file, rules), "Alternative include patterns should use OR");
        rules.includedNamePatterns = {u8"报告?.txt"};
        file.relativePath = fs::u8path(u8"资料/报告新.txt");
        require(filter->shouldInclude(file, rules), "Question mark did not match one Chinese character");
        file.relativePath = fs::u8path(u8"报告😀.txt");
        require(filter->shouldInclude(file, rules), "Question mark did not match one Unicode code point");
        file.relativePath = fs::u8path(u8"报告新旧.txt");
        require(!filter->shouldInclude(file, rules), "Question mark accepted two characters");
        rules.includedNamePatterns = {u8"报告**.txt"};
        rules.excludedNamePatterns = {u8"*草稿*"};
        file.relativePath = fs::u8path(u8"报告-草稿.txt");
        require(!filter->shouldInclude(file, rules), "Name exclusion did not override inclusion");
        rules.includedNamePatterns.clear(); rules.excludedNamePatterns.clear();
        rules.modifiedAfterMs = 1000; rules.modifiedBeforeMs = 2000;
        file.modifiedTimeMs = 1000;
        require(filter->shouldInclude(file, rules), "Time lower boundary must be inclusive");
        file.modifiedTimeMs = 2000;
        require(filter->shouldInclude(file, rules), "Time upper boundary must be inclusive");
        file.modifiedTimeMs = 999;
        require(!filter->shouldInclude(file, rules), "Older file incorrectly included");
        file.modifiedTimeMs = 2001;
        require(!filter->shouldInclude(file, rules), "Newer file incorrectly included");
        file.modifiedTimeMs.reset();
        require(!filter->shouldInclude(file, rules), "Missing modification time must not pass a time filter");
        file.isDirectory = true;
        require(filter->shouldInclude(file, rules), "Time/name filters must not prune directory traversal");

        const auto source = root / fs::u8path(u8"中文源");
        const auto repository = root / "repository";
        fs::create_directories(source / fs::u8path(u8"资料"));
        const auto recent = source / fs::u8path(u8"资料/报告-新版.txt");
        const auto old = source / fs::u8path(u8"资料/报告-旧版.txt");
        const auto draft = source / fs::u8path(u8"资料/报告-草稿.txt");
        std::ofstream(recent) << "recent final report";
        std::ofstream(old) << "old report";
        std::ofstream(draft) << "draft report";
        std::ofstream(source / "notes.txt") << "different name";
        fs::last_write_time(old, fs::file_time_type::clock::now() - std::chrono::hours(24 * 30));
        const auto recentMs = backup::detail::modifiedTimeMilliseconds(recent);
        require(recentMs - backup::detail::modifiedTimeMilliseconds(old) > 29LL * 24 * 3600000,
            "Native file time was not converted to Unix milliseconds correctly");
        backup::FilesystemBackupManager manager(filter);
        backup::FilesystemRestoreManager restore;
        backup::BackupRequest request;
        request.sourceDirectory = source; request.destinationDirectory = repository;
        request.backupName = "name-time";
        request.filters.includedNamePatterns = {u8"报告*.txt"};
        request.filters.excludedNamePatterns = {u8"*草稿*"};
        request.filters.modifiedAfterMs = recentMs - 24 * 3600000;
        request.filters.modifiedBeforeMs = recentMs + 24 * 3600000;
        request.verifyAfterCopy = true;
        auto result = manager.backup(request);
        require(result.succeeded(), result.message.c_str());
        require(result.processedFiles == 1, "Combined name/time filter copied the wrong number of files");
        require(result.filteredFiles == 3 && result.unchangedFiles == 0 && result.skippedFiles == 3,
            "Full backup must count three files excluded by name/time rules");
        require(fs::exists(result.outputPath / "data" / fs::u8path(u8"资料/报告-新版.txt"))
            && !fs::exists(result.outputPath / "data" / fs::u8path(u8"资料/报告-旧版.txt"))
            && !fs::exists(result.outputPath / "data" / fs::u8path(u8"资料/报告-草稿.txt")),
            "Real filesystem name/time selection failed");
        backup::RestoreRequest recovery;
        recovery.backupDirectory = result.outputPath; recovery.destinationDirectory = root / "restored";
        recovery.verifyAfterRestore = true;
        require(restore.restore(recovery).succeeded(), "Name/time snapshot restore failed");
        require(read(recovery.destinationDirectory / fs::u8path(u8"资料/报告-新版.txt")) == "recent final report",
            "Filtered restored content mismatch");

        // Relaxing the filters must copy newly admitted files even in incremental mode.
        request.mode = backup::BackupMode::Incremental;
        request.filters.modifiedAfterMs.reset(); request.filters.modifiedBeforeMs.reset();
        request.filters.excludedNamePatterns.clear();
        auto expanded = manager.backup(request);
        require(expanded.succeeded() && expanded.processedFiles == 2, "Incremental backup did not admit newly selected files");
        require(expanded.filteredFiles == 1 && expanded.unchangedFiles == 1 && expanded.skippedFiles == 2,
            "Incremental skip count must separate filtered and unchanged files");
        recovery.backupDirectory = expanded.outputPath; recovery.destinationDirectory = root / "expanded";
        require(restore.restore(recovery).succeeded()
            && read(recovery.destinationDirectory / fs::u8path(u8"资料/报告-旧版.txt")) == "old report",
            "Expanded filter chain did not restore old files");
        request.filters.modifiedAfterMs = 2000; request.filters.modifiedBeforeMs = 1000;
        require(manager.backup(request).status == backup::OperationStatus::InvalidRequest,
            "Backup core accepted a reversed time range");
        fs::remove_all(root);
        std::cout << "Unicode globs, name exclusion, time boundaries, real-file selection and incremental restore passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
