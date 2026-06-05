#include "GlobExpander.hpp"

#include <algorithm>
#include <filesystem>

bool GlobExpander::matchPattern(const std::string& pattern, const std::string& name) {
    std::size_t p = 0;
    std::size_t n = 0;
    std::size_t starIdx = std::string::npos;
    std::size_t matchIdx = 0;

    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            starIdx = p;
            matchIdx = n;
            ++p;
        } else if (starIdx != std::string::npos) {
            p = starIdx + 1;
            n = ++matchIdx;
        } else {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }

    return p == pattern.size();
}

std::vector<std::string> GlobExpander::matchInCurrentDirectory(const std::string& pattern) {
    std::vector<std::string> matches;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::current_path())) {
            std::string filename = entry.path().filename().string();

            // Skip hidden files unless the pattern explicitly starts with '.'
            if (!filename.empty() && filename[0] == '.' && (pattern.empty() || pattern[0] != '.')) {
                continue;
            }

            if (matchPattern(pattern, filename)) {
                matches.push_back(std::move(filename));
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        return {};
    }

    std::sort(matches.begin(), matches.end());
    return matches;
}

std::vector<std::string> GlobExpander::expand(const std::vector<shell::Token>& tokens) {
    std::vector<std::string> result;
    for (const auto& t : tokens) {
        if (t.quoted || t.text.find_first_of("*?") == std::string::npos) {
            result.push_back(t.text);
            continue;
        }

        auto matches = matchInCurrentDirectory(t.text);
        if (matches.empty()) {
            result.push_back(t.text);
        } else {
            result.insert(result.end(), matches.begin(), matches.end());
        }
    }
    return result;
}
