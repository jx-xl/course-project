#pragma once

#include "backup/core/BackupTypes.h"

#include <cstdint>
#include <filesystem>

namespace backup {

struct FileCandidate {
    std::filesystem::path relativePath;
    std::uintmax_t size{0};
    bool isDirectory{false};
    bool isHidden{false};
    std::optional<std::int64_t> modifiedTimeMs;
};

// Public contract implemented by C and consumed by B during directory scans.
class FilterManager {
public:
    virtual ~FilterManager() = default;

    FilterManager(const FilterManager&) = delete;
    FilterManager& operator=(const FilterManager&) = delete;
    FilterManager(FilterManager&&) = delete;
    FilterManager& operator=(FilterManager&&) = delete;

    [[nodiscard]] virtual bool shouldInclude(
        const FileCandidate& candidate,
        const FilterRules& rules) const = 0;

protected:
    FilterManager() = default;
};

} // namespace backup
