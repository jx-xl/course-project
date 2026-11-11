#include "backup/core/ArchiveManager.h"
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
void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path()); std::ofstream(path, std::ios::binary) << text;
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void number(std::string& data, std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) data += static_cast<char>((value >> (i * 8)) & 255);
}
std::string malicious(const std::string& path, std::uint64_t entries = 1) {
    std::string data = "DBPACK01"; number(data, entries, 8);
    for (std::uint64_t i = 0; i < entries; ++i) {
        number(data, 2, 1); number(data, path.size(), 4); number(data, 0, 8); number(data, 0, 8); data += path;
    }
    return data;
}
int main() {
    const auto root = fs::temp_directory_path() / ("backup-package-test-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto source = root / fs::u8path(u8"中文源");
        const auto repository = root / "repository";
        write(source / fs::u8path(u8"资料/原始报告.txt"), "unchanged baseline file");
        write(source / "changed.txt", "version one");
        write(source / "empty.bin", "");
        fs::create_directories(source / "empty-dir");
        auto filter = std::make_shared<backup::DefaultFilterManager>();
        backup::FilesystemBackupManager manager(filter);
        backup::FilesystemRestoreManager restore;
        backup::ArchiveManager archive;
        backup::BackupRequest request;
        request.sourceDirectory = source; request.destinationDirectory = repository;
        request.verifyAfterCopy = true; request.backupName = "archive-test";
        const auto full = manager.backup(request); require(full.succeeded(), full.message.c_str());
        write(source / "changed.txt", "version two, updated");
        write(source / "binary.bin", std::string(1024 * 1024, '\0') + std::string("\0\xff\x01", 3));
        request.mode = backup::BackupMode::Incremental;
        const auto incremental = manager.backup(request); require(incremental.succeeded(), incremental.message.c_str());
        backup::PackRequest pack{incremental.outputPath, root / fs::u8path(u8"单文件备份.dbpack")};
        auto packed = archive.pack(pack); require(packed.succeeded(), packed.message.c_str());
        require(fs::is_regular_file(pack.archiveFile) && read(pack.archiveFile).substr(0, 8) == "DBPACK01", "Package signature missing");
        const auto packageBytes = read(pack.archiveFile);
        require(archive.pack(pack).status == backup::OperationStatus::InvalidRequest
            && read(pack.archiveFile) == packageBytes, "Existing package must not be overwritten");
        auto nested = pack; nested.archiveFile = incremental.outputPath / "bad.dbpack";
        require(!archive.pack(nested).succeeded() && !fs::exists(nested.archiveFile), "Package output inside snapshot was accepted");

        bool cancel = false;
        auto cancelledPack = pack; cancelledPack.archiveFile = root / "cancelled.dbpack";
        auto cancelled = archive.pack(cancelledPack, [&](const backup::ProgressInfo& info) {
            if (info.phase == backup::OperationPhase::Packing) cancel = true;
        }, [&] { return cancel; });
        require(cancelled.status == backup::OperationStatus::Cancelled
            && !fs::exists(cancelledPack.archiveFile)
            && !fs::exists(fs::path(cancelledPack.archiveFile.wstring() + L".partial")), "Cancelled package left partial output");

        // The exported incremental snapshot must work after its whole original chain is gone.
        require(repository.parent_path() == root, "Unexpected test repository path");
        fs::remove_all(repository);
        backup::UnpackRequest unpack{pack.archiveFile, root / "imported"};
        const auto unpacked = archive.unpack(unpack); require(unpacked.succeeded(), unpacked.message.c_str());
        const auto metadata = backup::detail::readMetadata(unpacked.outputPath);
        require(metadata.mode == backup::BackupMode::Full && metadata.parentBackup.empty(), "Imported package still depends on original chain");
        backup::RestoreRequest recovery;
        recovery.backupDirectory = unpacked.outputPath; recovery.destinationDirectory = root / "restored";
        recovery.verifyAfterRestore = true;
        const auto restored = restore.restore(recovery); require(restored.succeeded(), restored.message.c_str());
        require(read(recovery.destinationDirectory / fs::u8path(u8"资料/原始报告.txt")) == "unchanged baseline file"
            && read(recovery.destinationDirectory / "changed.txt") == "version two, updated"
            && read(recovery.destinationDirectory / "binary.bin") == read(source / "binary.bin")
            && fs::file_size(recovery.destinationDirectory / "empty.bin") == 0
            && fs::is_directory(recovery.destinationDirectory / "empty-dir"), "Unpacked restore content mismatch");
        require(fs::last_write_time(recovery.destinationDirectory / "changed.txt") == fs::last_write_time(source / "changed.txt"), "Package lost modification time metadata");
        require(archive.unpack(unpack).succeeded(), "Repeated import should create a separate snapshot");

        const auto rejects = root / "rejected";
        auto reject = [&](const std::string& name, const std::string& bytes) {
            const auto path = root / fs::u8path(name); write(path, bytes);
            const auto result = archive.unpack({path, rejects});
            require(!result.succeeded(), "Damaged or unsafe package was accepted");
            if (fs::exists(rejects)) require(fs::is_empty(rejects), "Failed unpack left a partial snapshot");
        };
        auto damaged = packageBytes; damaged.back() ^= 1;
        reject("damaged.dbpack", damaged);
        reject("truncated.dbpack", packageBytes.substr(0, packageBytes.size() - 1));
        reject("trailing.dbpack", packageBytes + "unexpected");
        reject("traversal.dbpack", malicious("../escaped"));
        reject("absolute.dbpack", malicious("C:/escaped"));
        reject("stream.dbpack", malicious("data/file:stream"));
        reject("device.dbpack", malicious("data/NUL.txt"));
        reject("duplicate.dbpack", malicious("data", 2));
        require(!fs::exists(root / "escaped"), "Package path escaped the staging directory");
        cancel = false;
        const auto stopped = archive.unpack({pack.archiveFile, rejects}, [&](const backup::ProgressInfo&) {
            cancel = true;
        }, [&] { return cancel; });
        require(stopped.status == backup::OperationStatus::Cancelled && fs::is_empty(rejects), "Cancelled unpack left output");

        const auto emptySource = root / "empty-source"; fs::create_directories(emptySource);
        request.sourceDirectory = emptySource; request.destinationDirectory = repository; request.mode = backup::BackupMode::Full;
        const auto emptyBackup = manager.backup(request); require(emptyBackup.succeeded(), emptyBackup.message.c_str());
        const auto emptyPackage = root / "empty.dbpack";
        require(archive.pack({emptyBackup.outputPath, emptyPackage}).succeeded()
            && archive.unpack({emptyPackage, root / "empty-import"}).succeeded(), "Empty snapshot package roundtrip failed");
        fs::remove_all(root);
        std::cout << "Independent incremental package, Unicode/binary/empty data, timestamps, checksums, unsafe paths and cancellation passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << "\nEvidence: " << root.u8string() << '\n'; return 1; }
}
