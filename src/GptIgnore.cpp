#include "GptIgnore.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


namespace {

    /**
     * trimTrailingCarriageReturn()
     * Removes a Windows-style carriage return that may remain after getline().
     */
    void trimTrailingCarriageReturn(std::string& line) {
        if(!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
    }


    /**
     * matchesGlobFrom()
     * Recursively matches the remaining pattern and path while memoizing each
     * pattern/path position so wildcard backtracking does not repeat work.
     */
    bool matchesGlobFrom(
            std::string_view pattern,
            std::string_view path,
            std::size_t patternIndex,
            std::size_t pathIndex,
            std::vector<std::vector<int>>& memo) {

        int& cachedResult = memo[patternIndex][pathIndex];

        if(cachedResult != -1) {
            return cachedResult == 1;
        }

        bool matches = false;

        // Reaching the end of the pattern succeeds only when the path is also
        // completely consumed.
        if(patternIndex == pattern.size()) {
            matches = pathIndex == path.size();
        }
        else if(pattern[patternIndex] == '*') {
            const bool isDoubleStar =
                patternIndex + 1 < pattern.size() &&
                pattern[patternIndex + 1] == '*';

            if(isDoubleStar) {
                // When "**/" appears between path components, the entire globstar
                // component may disappear. This lets "a/**/b" match both "a/b"
                // and paths containing one or more intermediate directories.
                if(patternIndex + 2 < pattern.size() &&
                   pattern[patternIndex + 2] == '/') {

                    matches = matchesGlobFrom(
                        pattern,
                        path,
                        patternIndex + 3,
                        pathIndex,
                        memo
                    );
                }

                // "**" may also match an empty sequence when there is no following
                // directory separator to consume as part of the globstar component.
                if(!matches) {
                    matches = matchesGlobFrom(
                        pattern,
                        path,
                        patternIndex + 2,
                        pathIndex,
                        memo
                    );
                }

                // Or consume another character, including '/', and remain on the
                // same globstar so it can cross any number of directories.
                if(!matches && pathIndex < path.size()) {
                    matches = matchesGlobFrom(
                        pattern,
                        path,
                        patternIndex,
                        pathIndex + 1,
                        memo
                    );
                }
            }
            else {
                // "*" may match an empty sequence.
                matches = matchesGlobFrom(
                    pattern,
                    path,
                    patternIndex + 1,
                    pathIndex,
                    memo
                );

                // Ordinary "*" may consume characters only within the current
                // path component; it must not cross a directory separator.
                if(!matches &&
                   pathIndex < path.size() &&
                   path[pathIndex] != '/') {

                    matches = matchesGlobFrom(
                        pattern,
                        path,
                        patternIndex,
                        pathIndex + 1,
                        memo
                    );
                }
            }
        }
        else if(pattern[patternIndex] == '?') {
            // "?" consumes exactly one non-directory-separator character.
            if(pathIndex < path.size() && path[pathIndex] != '/') {
                matches = matchesGlobFrom(
                    pattern,
                    path,
                    patternIndex + 1,
                    pathIndex + 1,
                    memo
                );
            }
        }
        else if(pathIndex < path.size() &&
                pattern[patternIndex] == path[pathIndex]) {

            // Ordinary characters must match literally.
            matches = matchesGlobFrom(
                pattern,
                path,
                patternIndex + 1,
                pathIndex + 1,
                memo
            );
        }

        cachedResult = matches ? 1 : 0;
        return matches;
    }


    /**
     * matchesGlob()
     * Matches a normalized project-relative path against a .gptignore glob.
     *
     * Supported syntax:
     *   *  -> zero or more characters except '/'
     *   ?  -> exactly one character except '/'
     *   ** -> zero or more characters including '/'
     */
    bool matchesGlob(std::string_view pattern, std::string_view path) {
        // Each state is identified by one pattern position and one path position.
        // -1 means unevaluated, 0 means no match, and 1 means match.
        std::vector<std::vector<int>> memo(
            pattern.size() + 1,
            std::vector<int>(path.size() + 1, -1)
        );

        return matchesGlobFrom(pattern, path, 0, 0, memo);
    }


    /**
     * pathMatchesRule()
     * Applies one parsed rule to a normalized project-relative path.
     */
    bool pathMatchesRule(
            const GptIgnore::Rule& rule,
            std::string_view relativePath,
            bool isDirectory) {

        const std::string pattern = rule.pattern;

        // Anchored rules are matched only from the project root.
        if(rule.anchored) {
            if(matchesGlob(pattern, relativePath)) {
                return !rule.directoryOnly || isDirectory;
            }

            // A directory rule also hides every descendant beneath the
            // matching directory.
            if(rule.directoryOnly) {
                std::size_t separator = relativePath.size();

                while(separator != std::string_view::npos) {
                    const std::string_view prefix =
                        relativePath.substr(0, separator);

                    if(matchesGlob(pattern, prefix)) {
                        return true;
                    }

                    if(separator == 0) {
                        break;
                    }

                    separator = relativePath.rfind('/', separator - 1);
                }
            }

            return false;
        }

        // Patterns containing '/' are project-relative path patterns.
        if(pattern.find('/') != std::string::npos) {
            if(matchesGlob(pattern, relativePath)) {
                return !rule.directoryOnly || isDirectory;
            }

            if(rule.directoryOnly) {
                std::size_t separator = relativePath.size();

                while(separator != std::string_view::npos) {
                    const std::string_view prefix =
                        relativePath.substr(0, separator);

                    if(matchesGlob(pattern, prefix)) {
                        return true;
                    }

                    if(separator == 0) {
                        break;
                    }

                    separator = relativePath.rfind('/', separator - 1);
                }
            }

            return false;
        }

        // A pattern without '/' matches a filename or directory name at any
        // depth, similar to common ignore-file behavior.
        std::size_t componentStart = 0;

        while(componentStart <= relativePath.size()) {
            const std::size_t separator =
                relativePath.find('/', componentStart);

            const std::string_view component =
                separator == std::string_view::npos
                    ? relativePath.substr(componentStart)
                    : relativePath.substr(
                        componentStart,
                        separator - componentStart
                    );

            if(matchesGlob(pattern, component)) {
                if(!rule.directoryOnly) {
                    return true;
                }

                // For a directory-only rule, a matched non-final component is
                // necessarily a directory. A final component counts only when
                // the caller identified the path itself as a directory.
                if(separator != std::string_view::npos || isDirectory) {
                    return true;
                }
            }

            if(separator == std::string_view::npos) {
                break;
            }

            componentStart = separator + 1;
        }

        return false;
    }

}


/**
 * GptIgnore()
 * Loads project-local ignore rules from <project-root>/.gptignore.
 */
GptIgnore::GptIgnore(const std::filesystem::path& projectRoot) {
    const std::filesystem::path ignorePath = projectRoot / ".gptignore";

    if(!std::filesystem::exists(ignorePath)) {
        return;
    }

    std::ifstream input(ignorePath);

    if(!input) {
        throw std::runtime_error("Failed to open .gptignore");
    }

    std::string line;

    while(std::getline(input, line)) {
        trimTrailingCarriageReturn(line);

        // Blank lines and comments do not create rules.
        if(line.empty() || line.front() == '#') {
            continue;
        }

        Rule rule;

        // A leading slash anchors the rule at the project root.
        if(line.front() == '/') {
            rule.anchored = true;
            line.erase(0, 1);

            if(line.empty()) {
                continue;
            }
        }

        // A trailing slash denotes a directory and everything beneath it.
        if(line.back() == '/') {
            rule.directoryOnly = true;
            line.pop_back();

            if(line.empty()) {
                continue;
            }
        }

        rule.pattern = line;
        rules.push_back(std::move(rule));
    }
}


/**
 * isIgnored()
 * Returns true when any loaded .gptignore rule excludes the supplied path
 */
bool GptIgnore::isIgnored(
        const std::filesystem::path& relativePath,
        bool isDirectory) const {

    const std::string normalizedPath =
        relativePath.lexically_normal().generic_string();

    if(normalizedPath.empty() || normalizedPath == ".") {
        return false;
    }

    for(const Rule& rule : rules) {
        if(pathMatchesRule(rule, normalizedPath, isDirectory)) {
            return true;
        }
    }

    return false;
}
