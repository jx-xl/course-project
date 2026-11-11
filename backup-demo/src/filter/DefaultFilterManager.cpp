#include "backup/filter/DefaultFilterManager.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace backup {
namespace {

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string normalizedExtension(std::string extension)
{
    extension = lower(std::move(extension));
    if (!extension.empty() && extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }
    return extension;
}

bool containsExtension(const std::vector<std::string>& extensions, const std::string& extension)
{
    return std::any_of(extensions.begin(), extensions.end(), [&](const std::string& candidate) {
        return normalizedExtension(candidate) == extension;
    });
}

// Match complete UTF-8 basenames. ASCII case folding mirrors extension rules;
// non-ASCII names match exactly. Decode characters so '?' also matches Chinese.
std::u32string characters(const std::string& utf8)
{
    std::u32string result;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto first = static_cast<unsigned char>(utf8[i++]);
        char32_t point = first;
        unsigned count = 0;
        if ((first & 0xe0) == 0xc0) { point = first & 0x1f; count = 1; }
        else if ((first & 0xf0) == 0xe0) { point = first & 0x0f; count = 2; }
        else if ((first & 0xf8) == 0xf0) { point = first & 0x07; count = 3; }
        for (unsigned j = 0; j < count && i < utf8.size(); ++j) {
            const auto next = static_cast<unsigned char>(utf8[i]);
            if ((next & 0xc0) != 0x80) break;
            point = (point << 6) | (next & 0x3f);
            ++i;
        }
        if (point >= U'A' && point <= U'Z') point += U'a' - U'A';
        result.push_back(point);
    }
    return result;
}

bool nameMatches(const std::string& patternText, const std::string& nameText)
{
    const auto pattern = characters(patternText);
    const auto name = characters(nameText);
    std::size_t p = 0, n = 0, star = std::u32string::npos, retry = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == U'?' || pattern[p] == name[n])) {
            ++p; ++n;
        } else if (p < pattern.size() && pattern[p] == U'*') {
            star = p++; retry = n;
        } else if (star != std::u32string::npos) {
            p = star + 1; n = ++retry;
        } else return false;
    }
    while (p < pattern.size() && pattern[p] == U'*') ++p;
    return p == pattern.size();
}

bool anyNameMatches(const std::vector<std::string>& patterns, const std::string& name)
{
    return std::any_of(patterns.begin(), patterns.end(), [&](const auto& pattern) {
        return nameMatches(pattern, name);
    });
}

bool pathStartsWith(const std::filesystem::path& path, const std::filesystem::path& prefix)
{
    const auto normalizedPath = path.lexically_normal();
    const auto normalizedPrefix = prefix.lexically_normal();
    auto pathIterator = normalizedPath.begin();
    auto prefixIterator = normalizedPrefix.begin();

    for (; prefixIterator != normalizedPrefix.end(); ++prefixIterator, ++pathIterator) {
        if (pathIterator == normalizedPath.end()) {
            return false;
        }
#ifdef _WIN32
        if (lower(pathIterator->u8string()) != lower(prefixIterator->u8string())) {
#else
        if (*pathIterator != *prefixIterator) {
#endif
            return false;
        }
    }
    return true;
}

} // namespace

bool DefaultFilterManager::shouldInclude(
    const FileCandidate& candidate,
    const FilterRules& rules) const
{
    if (!rules.includeHiddenFiles && candidate.isHidden) {
        return false;
    }

    for (const auto& excludedPath : rules.excludedPaths) {
        if (!excludedPath.empty() && pathStartsWith(candidate.relativePath, excludedPath)) {
            return false;
        }
    }

    if (candidate.isDirectory) {
        return true;
    }

    const auto name = candidate.relativePath.filename().u8string();
    if ((!rules.includedNamePatterns.empty() && !anyNameMatches(rules.includedNamePatterns, name))
        || anyNameMatches(rules.excludedNamePatterns, name)) return false;
    if (rules.modifiedAfterMs || rules.modifiedBeforeMs) {
        if (!candidate.modifiedTimeMs) return false;
        if (rules.modifiedAfterMs && *candidate.modifiedTimeMs < *rules.modifiedAfterMs) return false;
        if (rules.modifiedBeforeMs && *candidate.modifiedTimeMs > *rules.modifiedBeforeMs) return false;
    }

    const auto extension = lower(candidate.relativePath.extension().u8string());
    if (!rules.includedExtensions.empty()
        && !containsExtension(rules.includedExtensions, extension)) {
        return false;
    }
    if (containsExtension(rules.excludedExtensions, extension)) {
        return false;
    }
    if (candidate.size < rules.minimumFileSize) {
        return false;
    }
    if (rules.maximumFileSize != 0 && candidate.size > rules.maximumFileSize) {
        return false;
    }
    return true;
}

} // namespace backup
