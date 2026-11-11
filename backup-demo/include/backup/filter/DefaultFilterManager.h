#pragma once

#include "backup/filter/FilterManager.h"

namespace backup {

class DefaultFilterManager final : public FilterManager {
public:
    DefaultFilterManager() = default;

    [[nodiscard]] bool shouldInclude(
        const FileCandidate& candidate,
        const FilterRules& rules) const override;
};

} // namespace backup
