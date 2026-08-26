#include "McpProjectFilesystem.hpp"

#include "GptIgnore.hpp"
#include "ProjectVisibility.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>


/**
 * readMcpProjectFile()
 * Reads one project-relative file through the same filesystem policy used by
 * the MCP read_project_file tool.
 */
McpProjectFileResult readMcpProjectFile(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& requestedPath) {

    // MCP project reads are always relative to the registered project root.
    if(requestedPath.is_absolute()) {
        return {
            false,
            "Requested project path must be relative"
        };
    }

    // Preserve the client-visible path independently from its eventual symlink
    // target so policy can be enforced against both names.
    const std::filesystem::path normalizedRequestedPath =
        requestedPath.lexically_normal();

    // Reject paths that lexically escape the registered project before any
    // filesystem resolution is attempted.
    if(normalizedRequestedPath.empty() ||
       normalizedRequestedPath == "." ||
       *normalizedRequestedPath.begin() == "..") {

        return {
            false,
            "Requested project path escapes the active project"
        };
    }

    // Resolve the project root once so all subsequent containment checks use
    // the same canonical filesystem location.
    std::error_code projectPathError;
    const std::filesystem::path canonicalProjectRoot =
        std::filesystem::canonical(
            projectRoot,
            projectPathError
        );

    if(projectPathError) {
        return {
            false,
            "Failed to resolve active project path"
        };
    }

    // Resolve symlinks and relative components in the requested path.
    std::error_code filePathError;
    const std::filesystem::path filePath =
        std::filesystem::weakly_canonical(
            canonicalProjectRoot / normalizedRequestedPath,
            filePathError
        );

    if(filePathError) {
        return {
            false,
            "Failed to resolve requested project path"
        };
    }

    // Convert the resolved target back to project-relative form. A target that
    // begins with ".." escaped through a symlink or other path resolution.
    std::error_code relativePathError;
    const std::filesystem::path relativePath =
        std::filesystem::relative(
            filePath,
            canonicalProjectRoot,
            relativePathError
        );

    if(relativePathError ||
       relativePath.empty() ||
       *relativePath.begin() == "..") {

        return {
            false,
            "Requested project path escapes the active project"
        };
    }

    // Both the requested alias and the resolved target must satisfy the
    // project's .gptignore policy.
    const GptIgnore ignoreRules(canonicalProjectRoot);

    if(ignoreRules.isIgnored(normalizedRequestedPath) ||
       ignoreRules.isIgnored(relativePath)) {

        return {
            false,
            "Requested project file is ignored by .gptignore"
        };
    }

    // Visibility rules are likewise enforced against both paths so an allowed
    // alias cannot expose a target that would normally remain hidden.
    if(!isProjectPathVisible(normalizedRequestedPath) ||
       !isProjectPathVisible(relativePath)) {

        return {
            false,
            "Requested project file is not visible to MCP"
        };
    }

    std::error_code fileTypeError;
    const bool isRegularFile =
        std::filesystem::is_regular_file(
            filePath,
            fileTypeError
        );

    if(fileTypeError || !isRegularFile) {
        return {
            false,
            "Requested project file does not exist or is not a regular file"
        };
    }

    // Keep one direct MCP read bounded to 1 MiB.
    constexpr std::uintmax_t maxFileSize = 1024 * 1024;

    std::error_code fileSizeError;
    const std::uintmax_t fileSize =
        std::filesystem::file_size(
            filePath,
            fileSizeError
        );

    if(fileSizeError) {
        return {
            false,
            "Failed to determine requested project file size"
        };
    }

    if(fileSize > maxFileSize) {
        return {
            false,
            "Requested project file exceeds the 1 MiB read limit"
        };
    }

    std::ifstream input(filePath);

    if(!input) {
        return {
            false,
            "Failed to open requested project file"
        };
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();

    return {
        true,
        buffer.str()
    };
}
