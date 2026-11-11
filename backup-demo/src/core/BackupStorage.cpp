#include "BackupStorage.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace backup::detail {
namespace {

std::string encodeHex(const std::string& value)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.size() * 2);
    for (const unsigned char byte : value) {
        encoded.push_back(digits[byte >> 4]);
        encoded.push_back(digits[byte & 0x0f]);
    }
    return encoded;
}

std::string decodeHex(const std::string& value)
{
    if (value.size() % 2 != 0) {
        throw std::runtime_error("Invalid hexadecimal text in backup metadata");
    }
    const auto nibble = [](char character) -> unsigned char {
        if (character >= '0' && character <= '9') {
            return static_cast<unsigned char>(character - '0');
        }
        if (character >= 'a' && character <= 'f') {
            return static_cast<unsigned char>(character - 'a' + 10);
        }
        if (character >= 'A' && character <= 'F') {
            return static_cast<unsigned char>(character - 'A' + 10);
        }
        throw std::runtime_error("Invalid hexadecimal digit in backup metadata");
    };

    std::string decoded;
    decoded.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2) {
        decoded.push_back(static_cast<char>((nibble(value[index]) << 4) | nibble(value[index + 1])));
    }
    return decoded;
}

std::string modeName(BackupMode mode)
{
    switch (mode) {
    case BackupMode::Full:
        return "full";
    case BackupMode::Incremental:
        return "incremental";
    case BackupMode::Differential:
        return "differential";
    }
    throw std::runtime_error("Unknown backup mode");
}

BackupMode parseMode(const std::string& value)
{
    if (value == "full") {
        return BackupMode::Full;
    }
    if (value == "incremental") {
        return BackupMode::Incremental;
    }
    if (value == "differential") {
        return BackupMode::Differential;
    }
    throw std::runtime_error("Unsupported backup mode in metadata");
}

std::map<std::string, std::string> readKeyValues(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open backup metadata: " + path.u8string());
    }

    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator != std::string::npos) {
            values[line.substr(0, separator)] = line.substr(separator + 1);
        }
    }
    return values;
}

std::string safeName(std::string value)
{
    for (char& character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!std::isalnum(byte) && character != '-' && character != '_') {
            character = '_';
        }
    }
    while (!value.empty() && value.front() == '_') {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == '_') {
        value.pop_back();
    }
    return value.empty() ? "backup" : value;
}

std::string timestampNow()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y%m%d-%H%M%S");
    return stream.str();
}

} // namespace

bool isSafeRelativePath(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const auto& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

bool isHiddenPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
#else
    const auto name = path.filename().string();
    return !name.empty() && name.front() == '.';
#endif
}

std::filesystem::path weakCanonical(const std::filesystem::path& path)
{
    std::error_code error;
    auto result = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : result;
}

std::int64_t modifiedTimeMilliseconds(const std::filesystem::path& path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        throw std::filesystem::filesystem_error("Cannot read file modification time", path,
            std::error_code(static_cast<int>(GetLastError()), std::system_category()));
    }
    const auto ticks = (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32)
        | data.ftLastWriteTime.dwLowDateTime;
    return (static_cast<std::int64_t>(ticks) - 116444736000000000LL) / 10000;
#else
    const auto value = std::filesystem::last_write_time(path);
    const auto systemValue = value - std::filesystem::file_time_type::clock::now()
        + std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(systemValue.time_since_epoch()).count();
#endif
}

bool isPathInside(const std::filesystem::path& child, const std::filesystem::path& parent)
{
    const auto normalizedChild = weakCanonical(child);
    const auto normalizedParent = weakCanonical(parent);
    auto childIterator = normalizedChild.begin();
    auto parentIterator = normalizedParent.begin();
    for (; parentIterator != normalizedParent.end(); ++parentIterator, ++childIterator) {
        if (childIterator == normalizedChild.end()) {
            return false;
        }
#ifdef _WIN32
        std::string childPart = childIterator->u8string();
        std::string parentPart = parentIterator->u8string();
        const auto lowerByte = [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        };
        std::transform(childPart.begin(), childPart.end(), childPart.begin(), lowerByte);
        std::transform(parentPart.begin(), parentPart.end(), parentPart.begin(), lowerByte);
        if (childPart != parentPart) {
#else
        if (*childIterator != *parentIterator) {
#endif
            return false;
        }
    }
    return true;
}

std::int64_t serializeWriteTime(std::filesystem::file_time_type value)
{
    return static_cast<std::int64_t>(value.time_since_epoch().count());
}

std::filesystem::file_time_type deserializeWriteTime(std::int64_t value)
{
    return std::filesystem::file_time_type(
        std::filesystem::file_time_type::duration(value));
}

void writeManifest(const std::filesystem::path& backupDirectory, const Manifest& manifest)
{
    std::ofstream output(backupDirectory / manifestFileName, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot create backup manifest");
    }
    output << "BACKUP_MANIFEST\t1\n";
    for (const auto& [path, entry] : manifest) {
        output << (entry.isDirectory ? 'D' : 'F') << '\t'
               << encodeHex(path.generic_u8string()) << '\t'
               << entry.size << '\t' << entry.writeTime << '\n';
    }
    if (!output) {
        throw std::runtime_error("Failed while writing backup manifest");
    }
}

Manifest readManifest(const std::filesystem::path& backupDirectory)
{
    std::ifstream input(backupDirectory / manifestFileName, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open backup manifest");
    }

    std::string line;
    if (!std::getline(input, line) || line != "BACKUP_MANIFEST\t1") {
        throw std::runtime_error("Unsupported or corrupt backup manifest");
    }

    Manifest manifest;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string type;
        std::string encodedPath;
        std::string size;
        std::string writeTime;
        if (!std::getline(row, type, '\t') || !std::getline(row, encodedPath, '\t')
            || !std::getline(row, size, '\t') || !std::getline(row, writeTime)) {
            throw std::runtime_error("Corrupt row in backup manifest");
        }

        ManifestEntry entry;
        entry.relativePath = std::filesystem::u8path(decodeHex(encodedPath));
        if (!isSafeRelativePath(entry.relativePath)) {
            throw std::runtime_error("Unsafe path in backup manifest");
        }
        entry.isDirectory = type == "D";
        if (!entry.isDirectory && type != "F") {
            throw std::runtime_error("Unknown entry type in backup manifest");
        }
        entry.size = static_cast<std::uintmax_t>(std::stoull(size));
        entry.writeTime = std::stoll(writeTime);
        manifest[entry.relativePath] = entry;
    }
    return manifest;
}

void writeMetadata(const std::filesystem::path& backupDirectory, const BackupMetadata& metadata)
{
    std::ofstream output(backupDirectory / metadataFileName, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot create backup metadata");
    }
    output << "version=" << metadata.version << '\n';
    output << "status=success\n";
    output << "mode=" << modeName(metadata.mode) << '\n';
    output << "created=" << metadata.createdAt << '\n';
    output << "source=" << encodeHex(metadata.sourceDirectory.u8string()) << '\n';
    output << "parent=" << encodeHex(metadata.parentBackup.u8string()) << '\n';
    output << "name=" << encodeHex(metadata.backupName) << '\n';
    if (!output) {
        throw std::runtime_error("Failed while writing backup metadata");
    }
}

BackupMetadata readMetadata(const std::filesystem::path& backupDirectory)
{
    const auto values = readKeyValues(backupDirectory / metadataFileName);
    if (values.at("status") != "success") {
        throw std::runtime_error("Backup is incomplete");
    }

    BackupMetadata metadata;
    metadata.version = std::stoi(values.at("version"));
    if (metadata.version != 1) {
        throw std::runtime_error("Unsupported backup metadata version");
    }
    metadata.mode = parseMode(values.at("mode"));
    metadata.createdAt = std::stoll(values.at("created"));
    metadata.sourceDirectory = std::filesystem::u8path(decodeHex(values.at("source")));
    metadata.parentBackup = std::filesystem::u8path(decodeHex(values.at("parent")));
    metadata.backupName = decodeHex(values.at("name"));
    return metadata;
}

std::vector<std::filesystem::path> findBackups(
    const std::filesystem::path& repositoryDirectory,
    const std::filesystem::path& sourceDirectory)
{
    std::vector<std::pair<std::int64_t, std::filesystem::path>> matches;
    std::error_code error;
    if (!std::filesystem::is_directory(repositoryDirectory, error)) {
        return {};
    }

    const auto canonicalSource = weakCanonical(sourceDirectory);
    for (std::filesystem::directory_iterator iterator(
             repositoryDirectory,
             std::filesystem::directory_options::skip_permission_denied,
             error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        if (!iterator->is_directory(error)) {
            continue;
        }
        try {
            const auto metadata = readMetadata(iterator->path());
            if (weakCanonical(metadata.sourceDirectory) == canonicalSource) {
                matches.emplace_back(metadata.createdAt, weakCanonical(iterator->path()));
            }
        } catch (const std::exception&) {
            // Ignore unrelated or incomplete directories in the repository.
        }
    }

    std::sort(matches.begin(), matches.end());
    std::vector<std::filesystem::path> paths;
    paths.reserve(matches.size());
    for (const auto& match : matches) {
        paths.push_back(match.second);
    }
    return paths;
}

std::optional<std::filesystem::path> latestBackup(
    const std::filesystem::path& repositoryDirectory,
    const std::filesystem::path& sourceDirectory,
    std::optional<BackupMode> requiredMode)
{
    const auto backups = findBackups(repositoryDirectory, sourceDirectory);
    for (auto iterator = backups.rbegin(); iterator != backups.rend(); ++iterator) {
        if (!requiredMode || readMetadata(*iterator).mode == *requiredMode) {
            return *iterator;
        }
    }
    return std::nullopt;
}

std::filesystem::path makeBackupDirectory(
    const std::filesystem::path& repositoryDirectory,
    const std::string& requestedName)
{
    const std::string baseName = safeName(requestedName) + '-' + timestampNow();
    auto candidate = repositoryDirectory / baseName;
    const auto partialExists = [](const std::filesystem::path& path) {
        return std::filesystem::exists(
            std::filesystem::path(path.u8string() + ".partial"));
    };
    for (unsigned int suffix = 2;
         std::filesystem::exists(candidate) || partialExists(candidate);
         ++suffix) {
        candidate = repositoryDirectory / (baseName + '-' + std::to_string(suffix));
    }
    return candidate;
}

bool filesEqual(const std::filesystem::path& first, const std::filesystem::path& second)
{
    std::error_code error;
    if (std::filesystem::file_size(first, error) != std::filesystem::file_size(second, error)
        || error) {
        return false;
    }
    std::ifstream firstInput(first, std::ios::binary);
    std::ifstream secondInput(second, std::ios::binary);
    if (!firstInput || !secondInput) {
        return false;
    }

    // Keep the buffers on the heap. Two 1 MiB stack arrays can exhaust the
    // default Windows thread stack, especially when this runs in a GUI worker.
    std::vector<char> firstBuffer(256 * 1024);
    std::vector<char> secondBuffer(256 * 1024);
    while (firstInput && secondInput) {
        firstInput.read(firstBuffer.data(), static_cast<std::streamsize>(firstBuffer.size()));
        secondInput.read(secondBuffer.data(), static_cast<std::streamsize>(secondBuffer.size()));
        const auto firstCount = firstInput.gcount();
        const auto secondCount = secondInput.gcount();
        if (firstCount != secondCount
            || !std::equal(firstBuffer.begin(), firstBuffer.begin() + firstCount, secondBuffer.begin())) {
            return false;
        }
    }
    return firstInput.eof() && secondInput.eof();
}

std::size_t pruneBackups(const BackupRequest& request, const std::filesystem::path& newest)
{
    if (request.maximumSnapshots == 0) return 0;
    const auto repository = weakCanonical(request.destinationDirectory);
    const auto current = weakCanonical(newest);
    const auto name = readMetadata(current).backupName;
    std::map<fs::path, BackupMetadata> all;
    std::vector<std::pair<std::int64_t, fs::path>> candidates;
    for (const auto& entry : fs::directory_iterator(repository)) {
        if (fs::is_symlink(entry.symlink_status()) || !entry.is_directory()) continue;
        const auto path = weakCanonical(entry.path());
        if (!fs::exists(path / metadataFileName)) continue;
        // Invalid metadata stops cleanup rather than risking a referenced baseline.
        const auto metadata = readMetadata(path);
        all.emplace(path, metadata);
        if (metadata.backupName == name
            && isPathInside(metadata.sourceDirectory, request.sourceDirectory)
            && isPathInside(request.sourceDirectory, metadata.sourceDirectory)) {
            candidates.emplace_back(metadata.createdAt, path);
        }
    }
    std::sort(candidates.rbegin(), candidates.rend());
    std::set<fs::path> removable;
    for (const auto& item : candidates) removable.insert(item.second);
    removable.erase(current);
    std::size_t retained = 1;
    for (const auto& item : candidates) {
        if (item.second != current && retained < request.maximumSnapshots) {
            removable.erase(item.second);
            ++retained;
        }
    }
    // All other snapshots, including other backup names, are live roots.
    std::vector<fs::path> pending;
    for (const auto& item : all) {
        if (!removable.count(item.first)) pending.push_back(item.first);
    }
    std::set<fs::path> visited;
    while (!pending.empty()) {
        auto path = pending.back();
        pending.pop_back();
        if (!visited.insert(path).second) continue;
        removable.erase(path);
        const auto found = all.find(path);
        if (found == all.end()) throw std::runtime_error("Missing backup dependency; cleanup stopped");
        if (!found->second.parentBackup.empty()) {
            pending.push_back(weakCanonical(found->second.parentBackup));
        }
    }
    std::size_t removed = 0;
    for (const auto& path : removable) {
        // Validate the resolved target immediately before recursive removal.
        if (path == repository || weakCanonical(path.parent_path()) != repository
            || !isPathInside(path, repository) || fs::is_symlink(fs::symlink_status(path))) {
            throw std::runtime_error("Unsafe cleanup target rejected");
        }
        fs::remove_all(path);
        ++removed;
    }
    return removed;
}

} // namespace backup::detail
