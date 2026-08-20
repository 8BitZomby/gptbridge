#include "ProjectVisibility.hpp"
#include "SensitivePath.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>


namespace {
    // Source-code extensions that are useful project context for MCP clients
    constexpr std::array<std::string_view, 17> visibleExtensions = {
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".h",
        ".hh",
        ".hpp",
        ".hxx",
        ".java",
        ".js",
        ".jsx",
        ".py",
        ".rs",
        ".sh",
        ".ts",
        ".tsx",
        ".zsh"
    };

    // Source-template suffixes that preserve the underlying source file type
    constexpr std::array<std::string_view, 3> visibleCompoundSuffixes = {
        ".cpp.in",
        ".h.in",
        ".hpp.in"
    };

    // Exact project/build files that are useful even though they may not have
    // a conventional source-code extension
    constexpr std::array<std::string_view, 8> visibleExactNames = {
        "Cargo.toml",
        "CMakeLists.txt",
        "Makefile",
        "meson.build",
        "package.json",
        "pyproject.toml",
        "README.md",
        "requirements.txt"
    };
} // End namespace


/**
 * isProjectPathVisible()
 * Returns true when a project-relative path is allowed to be exposed to MCP clients
 */
bool isProjectPathVisible(const std::filesystem::path& relativePath) {
    // Sensitive paths are always denied regardless of file type
    if(isSensitiveProjectPath(relativePath)) {
        return false;
    }

    const std::string filename = relativePath.filename().string();

    if(filename.empty()) {
        return false;
    }

    // Some useful project files are identified by exact filenames rather than extension
    const bool matchesExactName = std::any_of(
        visibleExactNames.begin(),
        visibleExactNames.end(),
        [&filename](std::string_view name) {

        return filename == name;
        }
    );

    if(matchesExactName) {
        return true;
    }

    // Recognize generated-source templates by their full compound suffix
    const bool matchesCompoundSuffix = std::any_of(
            visibleCompoundSuffixes.begin(),
            visibleCompoundSuffixes.end(),
            [&filename](std::string_view suffix) {

                return filename.ends_with(suffix);
            }
    );

    if(matchesCompoundSuffix) {
        return true;
    }

    const std::string extension = relativePath.extension().string();

    // Unknown file types remain hidden by default
    return std::any_of(
        visibleExtensions.begin(),
        visibleExtensions.end(),
        [&extension](std::string_view visibleExtension) {

            return extension == visibleExtension;
        }
    );
}
