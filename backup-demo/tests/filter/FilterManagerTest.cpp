#include "backup/filter/DefaultFilterManager.h"

#include <iostream>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    backup::DefaultFilterManager filter;
    backup::FilterRules rules;
    rules.includedExtensions = {".txt", "JPG"};
    rules.excludedExtensions = {".tmp"};
    rules.excludedPaths = {"cache"};
    rules.minimumFileSize = 2;
    rules.maximumFileSize = 1024;

    backup::FileCandidate candidate;
    candidate.relativePath = "docs/readme.TXT";
    candidate.size = 12;
    if (!filter.shouldInclude(candidate, rules)) {
        return fail("A matching extension should be included");
    }

    candidate.relativePath = "docs/readme.md";
    if (filter.shouldInclude(candidate, rules)) {
        return fail("An extension outside the inclusion list should be excluded");
    }

    candidate.relativePath = "cache/image.jpg";
    if (filter.shouldInclude(candidate, rules)) {
        return fail("An excluded path should be excluded recursively");
    }

    candidate.relativePath = "image.jpg";
    candidate.isHidden = true;
    if (filter.shouldInclude(candidate, rules)) {
        return fail("Hidden entries should be excluded by default");
    }

    candidate.isHidden = false;
    candidate.size = 1;
    if (filter.shouldInclude(candidate, rules)) {
        return fail("Files below the minimum size should be excluded");
    }

    candidate = {};
    candidate.relativePath = "docs/limit.txt";
    candidate.size = 1025;
    if (filter.shouldInclude(candidate, rules)) {
        return fail("Files above the maximum size should be excluded");
    }
    candidate.size = 1024;
    if (!filter.shouldInclude(candidate, rules)) {
        return fail("The maximum size boundary should be inclusive");
    }
    candidate.relativePath = "cache-other/keep.txt";
    if (!filter.shouldInclude(candidate, rules)) {
        return fail("Path exclusion must match complete components");
    }
    candidate.isHidden = true;
    rules.includeHiddenFiles = true;
    if (!filter.shouldInclude(candidate, rules)) {
        return fail("Explicitly included hidden files should be accepted");
    }
    rules.excludedExtensions.push_back("TXT");
    if (filter.shouldInclude(candidate, rules)) {
        return fail("Exclusion should override inclusion regardless of extension case");
    }

    candidate = {};
    candidate.relativePath = "empty";
    candidate.isDirectory = true;
    if (!filter.shouldInclude(candidate, rules)) {
        return fail("Directories should not be rejected by extension or size rules");
    }

    return 0;
}
