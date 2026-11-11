#include "backup/core/ArchiveManager.h"
#include "backup/core/FilesystemRestoreManager.h"
#include "BackupStorage.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace backup {
namespace {
constexpr char magic[] = "DBPACK01";
constexpr std::uint64_t maxEntries = 100000;
constexpr std::uint32_t maxPathBytes = 16384;
constexpr std::uint64_t fnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;

void checkCancelled(const CancellationCheck& cancelled) {
    if (cancelled && cancelled()) throw OperationStatus::Cancelled;
}
void notify(const ProgressCallback& callback, const ProgressInfo& info) {
    if (callback) callback(info);
}
std::uint64_t hashBytes(std::uint64_t hash, const char* data, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        hash ^= static_cast<unsigned char>(data[i]); hash *= fnvPrime;
    }
    return hash;
}
void writeUnsigned(std::ostream& out, std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) out.put(static_cast<char>((value >> (i * 8)) & 255));
}
struct Reader {
    std::ifstream stream;
    std::uintmax_t remaining;
    explicit Reader(const fs::path& path) : stream(path, std::ios::binary), remaining(fs::file_size(path)) {
        if (!stream) throw std::runtime_error("Cannot open backup package");
    }
    void read(char* data, std::size_t count) {
        if (count > remaining) throw std::runtime_error("Truncated backup package");
        stream.read(data, static_cast<std::streamsize>(count));
        if (!stream) throw std::runtime_error("Cannot read backup package");
        remaining -= count;
    }
    std::uint64_t number(unsigned bytes) {
        std::array<char, 8> data{}; read(data.data(), bytes);
        std::uint64_t value = 0;
        for (unsigned i = 0; i < bytes; ++i) value |= std::uint64_t(static_cast<unsigned char>(data[i])) << (i * 8);
        return value;
    }
};
std::string lowerAscii(std::string value) {
    for (auto& ch : value) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return value;
}
bool safeArchivePath(const std::string& text) {
    if (text.empty() || text.size() > maxPathBytes || text.front() == '/' || text.back() == '/') return false;
    for (unsigned char ch : text) {
        if (ch < 32 || std::string("\\:<>\"|?*").find(static_cast<char>(ch)) != std::string::npos) return false;
    }
    const auto path = fs::u8path(text);
    if (!detail::isSafeRelativePath(path)) return false;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('/', start);
        if (end == std::string::npos) end = text.size();
        const auto part = text.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        const auto device = lowerAscii(part.substr(0, part.find('.')));
        if (device == "con" || device == "prn" || device == "aux" || device == "nul"
            || (device.size() == 4 && (device.substr(0, 3) == "com" || device.substr(0, 3) == "lpt")
                && device[3] >= '1' && device[3] <= '9')) return false;
        start = end + 1;
    }
    return true;
}
bool allowedEntry(const std::string& name, bool directory) {
    if (name == detail::metadataFileName || name == detail::manifestFileName) return !directory;
    return (name == "data" && directory) || name.rfind("data/", 0) == 0;
}

// All paths held here are generated siblings inside the explicitly chosen output
// parent. Validate that boundary again before recursive cleanup.
struct Temporary {
    fs::path path;
    fs::path parent;
    ~Temporary() {
        if (path.empty()) return;
        std::error_code ignored;
        if (detail::weakCanonical(path.parent_path()) == parent
            && !fs::is_symlink(fs::symlink_status(path, ignored))) fs::remove_all(path, ignored);
    }
};
fs::path temporaryDirectory(const fs::path& parent, const std::string& prefix) {
    const auto path = detail::makeBackupDirectory(parent, prefix);
    if (!fs::create_directory(path)) throw std::runtime_error("Cannot create package staging directory");
    return path;
}
OperationResult invalid(std::string message) {
    OperationResult result; result.status = OperationStatus::InvalidRequest;
    result.message = std::move(message); return result;
}
OperationResult failure(const std::exception& error) {
    OperationResult result; result.status = OperationStatus::IoError;
    result.message = error.what(); return result;
}
OperationResult cancelledResult() {
    OperationResult result; result.status = OperationStatus::Cancelled;
    result.message = "Package operation cancelled; temporary output removed."; return result;
}

std::uint64_t checksumFile(const fs::path& path, const CancellationCheck& cancelled) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read package input file");
    std::vector<char> buffer(256 * 1024);
    auto hash = fnvOffset;
    while (input) {
        checkCancelled(cancelled);
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        hash = hashBytes(hash, buffer.data(), static_cast<std::size_t>(input.gcount()));
    }
    if (!input.eof()) throw std::runtime_error("Cannot calculate package checksum");
    return hash;
}
}

OperationResult ArchiveManager::pack(const PackRequest& request, ProgressCallback callback,
    CancellationCheck cancelled) const
{
    if (request.backupDirectory.empty() || request.archiveFile.empty()) return invalid("Select a snapshot and an output package file.");
    try {
        checkCancelled(cancelled);
        const auto snapshot = detail::weakCanonical(request.backupDirectory);
        const auto archive = detail::weakCanonical(fs::absolute(request.archiveFile));
        const auto parent = archive.parent_path();
        if (detail::isPathInside(archive, snapshot)) return invalid("The package must be saved outside the snapshot directory.");
        if (fs::exists(archive) || fs::is_symlink(fs::symlink_status(archive))) return invalid("The output package already exists. Choose another file name.");
        auto metadata = detail::readMetadata(snapshot);
        const auto manifest = detail::readManifest(snapshot);
        if (manifest.size() + 3 > maxEntries) return invalid("Too many entries for DBPACK v1.");
        for (const auto& item : manifest) {
            if (!safeArchivePath(item.first.generic_u8string())) return invalid("Snapshot contains an unsupported entry path.");
        }
        fs::create_directories(parent);
        Temporary stage{temporaryDirectory(parent, "pack-stage"), parent};
        RestoreRequest materialize;
        materialize.backupDirectory = snapshot;
        materialize.destinationDirectory = stage.path / "data";
        materialize.verifyAfterRestore = true;
        const auto restored = FilesystemRestoreManager().restore(materialize, callback, cancelled);
        if (!restored.succeeded()) return restored;
        checkCancelled(cancelled);
        // Resolve the entire chain before export so the one package is sufficient.
        metadata.mode = BackupMode::Full; metadata.parentBackup.clear();
        detail::writeMetadata(stage.path, metadata);
        detail::writeManifest(stage.path, manifest);

        std::vector<fs::path> entries;
        for (const auto& entry : fs::recursive_directory_iterator(stage.path)) entries.push_back(entry.path());
        std::sort(entries.begin(), entries.end());
        if (entries.size() > maxEntries) throw std::runtime_error("Too many entries for DBPACK v1 (limit: 100000)");
        ProgressInfo progress; progress.phase = OperationPhase::Packing;
        for (const auto& entry : entries) if (fs::is_regular_file(entry)) {
            ++progress.totalFiles; progress.totalBytes += fs::file_size(entry);
        }
        auto partialPath = archive;
        partialPath += ".partial";
        if (fs::exists(partialPath) || fs::is_symlink(fs::symlink_status(partialPath))) return invalid("A partial package with that name already exists. Choose another output name.");
        Temporary partial{partialPath, parent};
        std::ofstream out(partial.path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot create backup package");
        out.write(magic, 8); writeUnsigned(out, entries.size(), 8);
        std::vector<char> buffer(256 * 1024);
        for (const auto& path : entries) {
            checkCancelled(cancelled);
            const auto name = fs::relative(path, stage.path).generic_u8string();
            const bool directory = fs::is_directory(path);
            if (!safeArchivePath(name) || !allowedEntry(name, directory)) throw std::runtime_error("Unsupported package entry name");
            const auto size = directory ? 0 : fs::file_size(path);
            const auto checksum = directory ? 0 : checksumFile(path, cancelled);
            writeUnsigned(out, directory ? 2 : 1, 1);
            writeUnsigned(out, name.size(), 4); writeUnsigned(out, size, 8); writeUnsigned(out, checksum, 8);
            out.write(name.data(), static_cast<std::streamsize>(name.size()));
            progress.currentPath = fs::u8path(name); notify(callback, progress);
            if (!directory) {
                std::ifstream input(path, std::ios::binary);
                auto copiedHash = fnvOffset;
                std::uintmax_t copied = 0;
                while (input) {
                    checkCancelled(cancelled);
                    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                    const auto count = static_cast<std::size_t>(input.gcount());
                    out.write(buffer.data(), static_cast<std::streamsize>(count));
                    copiedHash = hashBytes(copiedHash, buffer.data(), count);
                    copied += count; progress.processedBytes += count; notify(callback, progress);
                }
                if (!input.eof() || copied != size || copiedHash != checksum) throw std::runtime_error("Package input changed while reading");
                ++progress.processedFiles;
                notify(callback, progress);
            }
            if (!out) throw std::runtime_error("Cannot write backup package");
        }
        out.close(); if (!out) throw std::runtime_error("Cannot finalize backup package");
        checkCancelled(cancelled);
        fs::rename(partial.path, archive); partial.path.clear();
        OperationResult result; result.outputPath = archive;
        result.processedFiles = restored.processedFiles; result.processedBytes = restored.processedBytes;
        result.message = "Self-contained DBPACK package created; all snapshot dependencies included.";
        return result;
    } catch (OperationStatus) { return cancelledResult(); }
    catch (const std::exception& error) { return failure(error); }
}

OperationResult ArchiveManager::unpack(const UnpackRequest& request, ProgressCallback callback,
    CancellationCheck cancelled) const
{
    if (request.archiveFile.empty() || request.destinationDirectory.empty()) return invalid("Select a package and an output parent directory.");
    try {
        checkCancelled(cancelled);
        Reader input(request.archiveFile);
        std::array<char, 8> signature{}; input.read(signature.data(), signature.size());
        if (!std::equal(signature.begin(), signature.end(), magic)) return invalid("Not a supported DBPACK v1 package.");
        const auto count = input.number(8);
        if (count == 0 || count > maxEntries) return invalid("Invalid package entry count.");
        const auto parent = detail::weakCanonical(fs::absolute(request.destinationDirectory));
        fs::create_directories(parent);
        Temporary stage{temporaryDirectory(parent, "unpack-stage"), parent};
        std::set<std::string> seen;
        std::set<fs::path> payloadFiles;
        ProgressInfo progress; progress.phase = OperationPhase::Unpacking;
        progress.totalBytes = input.remaining;
        std::vector<char> buffer(256 * 1024);
        for (std::uint64_t i = 0; i < count; ++i) {
            checkCancelled(cancelled);
            const auto type = input.number(1);
            const auto pathSize = input.number(4);
            const auto size = input.number(8);
            const auto checksum = input.number(8);
            if ((type != 1 && type != 2) || pathSize == 0 || pathSize > maxPathBytes) throw std::runtime_error("Invalid package entry header");
            std::string name(static_cast<std::size_t>(pathSize), '\0'); input.read(name.data(), name.size());
            const bool directory = type == 2;
            if (!safeArchivePath(name) || !allowedEntry(name, directory)
                || !seen.insert(lowerAscii(name)).second) throw std::runtime_error("Unsafe or duplicate package entry path");
            const auto relative = fs::u8path(name);
            const auto target = stage.path / relative;
            if (!detail::isPathInside(target, stage.path)) throw std::runtime_error("Package entry escapes its output directory");
            if (directory) {
                if (size != 0 || checksum != 0) throw std::runtime_error("Invalid directory entry");
                fs::create_directories(target);
            } else {
                if (size > input.remaining || fs::exists(target)) throw std::runtime_error("Invalid file entry size or collision");
                fs::create_directories(target.parent_path());
                std::ofstream output(target, std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error("Cannot create unpacked file");
                auto remaining = size; auto actualHash = fnvOffset;
                while (remaining != 0) {
                    checkCancelled(cancelled);
                    const auto bytes = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                    input.read(buffer.data(), bytes); output.write(buffer.data(), static_cast<std::streamsize>(bytes));
                    actualHash = hashBytes(actualHash, buffer.data(), bytes); remaining -= bytes;
                    progress.currentPath = relative; progress.processedBytes += bytes; notify(callback, progress);
                }
                output.close(); if (!output) throw std::runtime_error("Cannot write unpacked file");
                if (actualHash != checksum) throw std::runtime_error("Package checksum mismatch; damaged file rejected");
                if (name.rfind("data/", 0) == 0) payloadFiles.insert(relative.lexically_relative("data"));
                ++progress.processedFiles;
            }
        }
        if (input.remaining != 0) throw std::runtime_error("Unexpected data after the package entries");
        const auto metadata = detail::readMetadata(stage.path);
        if (metadata.mode != BackupMode::Full || !metadata.parentBackup.empty()) throw std::runtime_error("Package is not an independent snapshot");
        const auto manifest = detail::readManifest(stage.path);
        std::size_t expectedFiles = 0;
        for (const auto& item : manifest) {
            checkCancelled(cancelled);
            if (!safeArchivePath(item.first.generic_u8string())) throw std::runtime_error("Unsafe manifest entry");
            const auto file = stage.path / "data" / item.first;
            if (item.second.isDirectory) {
                if (!fs::is_directory(file)) throw std::runtime_error("Package is missing a manifest directory");
            } else {
                ++expectedFiles;
                if (!payloadFiles.count(item.first) || !fs::is_regular_file(file) || fs::file_size(file) != item.second.size)
                    throw std::runtime_error("Package data does not match its manifest");
            }
        }
        if (expectedFiles != payloadFiles.size() || !fs::is_directory(stage.path / "data")) throw std::runtime_error("Unexpected package payload files");
        progress.phase = OperationPhase::Finalizing;
        progress.processedFiles = expectedFiles; progress.totalFiles = expectedFiles;
        notify(callback, progress);
        const auto finalPath = detail::makeBackupDirectory(parent, "unpacked");
        checkCancelled(cancelled);
        fs::rename(stage.path, finalPath); stage.path.clear();
        OperationResult result; result.outputPath = finalPath;
        result.processedFiles = expectedFiles; result.processedBytes = progress.processedBytes;
        result.message = "Package checksums verified; independent snapshot is ready to restore.";
        return result;
    } catch (OperationStatus) { return cancelledResult(); }
    catch (const std::exception& error) { return failure(error); }
}
}
