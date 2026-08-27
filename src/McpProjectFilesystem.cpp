#include "McpProjectFilesystem.hpp"

#include "GptIgnore.hpp"
#include "ProjectVisibility.hpp"
#include "SensitivePath.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>


namespace {

    /**
     * isBinaryFile()
     * Samples the beginning of a file and treats it as binary when a NUL byte
     * is present. This avoids sending common binary formats through MCP text
     * reads or searching arbitrary binary contents as lines.
     */
    bool isBinaryFile(const std::filesystem::path& filePath) {
        // Max number of bytes to inspect
        constexpr std::size_t sampleSize = 8 * 1024;

        // Open the file without text-mode transformations
        std::ifstream input(filePath, std::ios::binary);

        // Report "not binary" when the file cannot be opened here
        if(!input) {
            return false;
        }

        // Store the sampled bytes
        std::array<char, sampleSize> buffer{};

        // Read up to sampleSize bytes from the start of the file
        input.read(
            buffer.data(),
            static_cast<std::streamsize>(buffer.size())
        );

        // Record how many bytes were actually read
        const std::streamsize bytesRead = input.gcount();

        // Search only the valid sample bytes for a NUL byte
        return std::find(buffer.begin(), buffer.begin() + bytesRead, '\0') != buffer.begin() + bytesRead;
    }


    /**
     * containsPrivateKeyBlock()
     * Returns true when text contains a complete recognized PEM/OpenSSH
     * private-key block with matching BEGIN and END boundary lines.
     */
    bool containsPrivateKeyBlock(const std::string& text) {
        // Supported private-key boundary pairs.
        constexpr std::array<std::pair<std::string_view, std::string_view>, 4> boundaries = {{
            {"-----BEGIN PRIVATE KEY-----", "-----END PRIVATE KEY-----"},
            {"-----BEGIN RSA PRIVATE KEY-----", "-----END RSA PRIVATE KEY-----"},
            {"-----BEGIN OPENSSH PRIVATE KEY-----", "-----END OPENSSH PRIVATE KEY-----"},
            {"-----BEGIN EC PRIVATE KEY-----", "-----END EC PRIVATE KEY-----"}
        }};

        // Record which opening boundaries have already appeared.
        std::array<bool, boundaries.size()> openBlocks{};

        // Inspect complete lines so marker strings embedded in source code do not match.
        std::istringstream input(text);
        std::string line;

        while(std::getline(input, line)) {
            // Remove the carriage return left by getline() for CRLF line endings.
            if(!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // Check the current line against each supported key format.
            for(std::size_t index = 0; index < boundaries.size(); ++index) {
                const auto& [beginMarker, endMarker] = boundaries[index];

                // Remember a complete opening boundary.
                if(line == beginMarker) {
                    openBlocks[index] = true;
                }

                // A matching closing boundary completes the private-key block.
                else if(openBlocks[index] && line == endMarker) {
                    return true;
                }
            }
        }
        // No complete recognized private-key block was found.
        return false;
    }
}


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

    // Reject binary contents before reading them as MCP text.
    if(isBinaryFile(filePath)) {
        return {
            false,
            "Requested project file appears to be binary"
        };
    }

    std::ifstream input(filePath);

    if(!input) {
        return {
            false,
            "Failed to open requested project file"
        };
    }

    // Read the complete file so private-key content can be checked before
    // anything is returned through MCP.
    std::ostringstream buffer;
    buffer << input.rdbuf();

    const std::string contents = buffer.str();

    // Reject files containing a complete recognized private-key block.
    if(containsPrivateKeyBlock(contents)) {
        return {
            false,
            "Requested project file contains private key material"
        };
    }

    return {
        true,
        contents
    };
}


/**
 * searchMcpProjectFiles()
 * Searches readable project files through the same filesystem policy used by
 * the MCP search_project_files tool.
 */
McpProjectFileResult searchMcpProjectFiles(
        const std::filesystem::path& projectRoot,
        const std::string& query) {

    // Resolve the registered project root before traversal so containment
    // checks compare every entry against one canonical filesystem location.
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

    const GptIgnore ignoreRules(canonicalProjectRoot);

    constexpr std::size_t maxResults = 50;
    constexpr std::uintmax_t maxFileSize = 1024 * 1024;

    std::size_t resultCount = 0;
    std::size_t oversizedFileCount = 0;
    std::ostringstream text;

    // Skip directories that cannot be entered because of filesystem
    // permissions rather than failing the entire search immediately.
    std::error_code iteratorError;

    std::filesystem::recursive_directory_iterator itr(
        canonicalProjectRoot,
        std::filesystem::directory_options::skip_permission_denied,
        iteratorError
    );

    const std::filesystem::recursive_directory_iterator end;

    if(iteratorError) {
        return {
            false,
            "Failed to enumerate active project files"
        };
    }

    while(itr != end && resultCount < maxResults) {
        const std::filesystem::path entryPath = itr->path();

        // Preserve the entry's alias path for filtering and result presentation.
        // A symlink target is resolved separately below.
        const std::filesystem::path relativePath =
            entryPath.lexically_relative(canonicalProjectRoot);

        if(!relativePath.empty()) {
            std::error_code typeError;
            const bool isDirectory =
                itr->is_directory(typeError);

            if(!typeError && isDirectory) {
                const std::string directoryName =
                    entryPath.filename().string();

                // Do not descend into generated, cache, sensitive, or ignored
                // directories.
                if(directoryName == "build" ||
                   directoryName == ".cache" ||
                   isSensitiveProjectPath(relativePath) ||
                   ignoreRules.isIgnored(relativePath, true)) {

                    itr.disable_recursion_pending();
                }
            }

            else if(!typeError) {
                typeError.clear();

                const bool isRegularFile =
                    itr->is_regular_file(typeError);

                if(!typeError && isRegularFile) {
                    // Resolve file symlinks before searching their contents.
                    std::error_code resolvedPathError;
                    const std::filesystem::path resolvedEntryPath =
                        std::filesystem::weakly_canonical(
                            entryPath,
                            resolvedPathError
                        );

                    if(!resolvedPathError) {
                        std::error_code resolvedRelativeError;
                        const std::filesystem::path resolvedRelativePath =
                            std::filesystem::relative(
                                resolvedEntryPath,
                                canonicalProjectRoot,
                                resolvedRelativeError
                            );

                        // Both the visible alias and resolved target must remain
                        // inside the project and satisfy project visibility policy.
                        if(!resolvedRelativeError &&
                           !resolvedRelativePath.empty() &&
                           *resolvedRelativePath.begin() != ".." &&
                           !ignoreRules.isIgnored(relativePath) &&
                           !ignoreRules.isIgnored(resolvedRelativePath) &&
                           isProjectPathVisible(relativePath) &&
                           isProjectPathVisible(resolvedRelativePath)) {

                            std::error_code fileSizeError;
                            const std::uintmax_t fileSize =
                                std::filesystem::file_size(
                                    resolvedEntryPath,
                                    fileSizeError
                                );

                            if(!fileSizeError) {
                                if(fileSize > maxFileSize) {
                                    ++oversizedFileCount;
                                }

                                // Binary files are excluded from text search.
                                else if(!isBinaryFile(resolvedEntryPath)) {
                                    std::ifstream input(resolvedEntryPath);

                                    // Unreadable files are skipped without failing
                                    // the complete project search.
                                    if(input) {
                                        // Read the complete file before exposing any
                                        // individual matching lines.
                                        std::ostringstream fileBuffer;
                                        fileBuffer << input.rdbuf();

                                        const std::string contents =
                                            fileBuffer.str();

                                        // Private-key files are excluded from search
                                        // entirely, matching the direct-read policy.
                                        if(!containsPrivateKeyBlock(contents)) {
                                            std::istringstream lines(contents);
                                            std::string line;
                                            std::size_t lineNumber = 0;

                                            // Search the validated text line by line.
                                            while(resultCount < maxResults &&
                                                  std::getline(lines, line)) {

                                                ++lineNumber;

                                                // Search is exact and case-sensitive.
                                                if(line.find(query) == std::string::npos) {
                                                    continue;
                                                }

                                                text << relativePath.generic_string()
                                                     << ':'
                                                     << lineNumber
                                                     << ": "
                                                     << line
                                                     << '\n';

                                                ++resultCount;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        iteratorError.clear();
        itr.increment(iteratorError);

        // Preserve any results already collected if recursive traversal later
        // encounters an entry it cannot advance past.
        if(iteratorError) {
            break;
        }
    }

    if(resultCount == 0) {
        text << "No matches found.";
    }
    else if(resultCount == maxResults) {
        text << "\nSearch stopped after "
             << maxResults
             << " matches.";
    }

    if(oversizedFileCount > 0) {
        text << "\n"
             << oversizedFileCount
             << (oversizedFileCount == 1
                 ? " file was skipped because it exceeds the 1 MiB search limit."
                 : " files were skipped because they exceed the 1 MiB search limit.");
    }

    return {
        true,
        text.str()
    };
}


/**
 * listMcpProjectFiles()
 * Lists readable project files through the same filesystem policy used by the
 * MCP list_project_files tool.
 */
McpProjectFileResult listMcpProjectFiles(
        const std::filesystem::path& projectRoot) {

    // Resolve the registered project root before traversal so containment
    // checks compare every entry against one canonical filesystem location.
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

    // Load project-local ignore rules once for the complete listing operation.
    const GptIgnore ignoreRules(canonicalProjectRoot);

    std::vector<std::string> files;

    std::error_code iteratorError;

    std::filesystem::recursive_directory_iterator itr(
        canonicalProjectRoot,
        std::filesystem::directory_options::skip_permission_denied,
        iteratorError
    );

    const std::filesystem::recursive_directory_iterator end;

    if(iteratorError) {
        return {
            false,
            "Failed to enumerate active project files"
        };
    }

    while(itr != end) {
        const std::filesystem::path entryPath =
            itr->path();

        // Preserve the directory entry's project-relative alias path rather
        // than replacing it with a resolved symlink target.
        const std::filesystem::path relativePath =
            entryPath.lexically_relative(canonicalProjectRoot);

        if(!relativePath.empty()) {
            std::error_code typeError;
            const bool isDirectory =
                itr->is_directory(typeError);

            if(!typeError && isDirectory) {
                const std::string directoryName =
                    entryPath.filename().string();

                // Do not descend into ignored, sensitive, generated, or cache
                // directories.
                if(ignoreRules.isIgnored(relativePath, true) ||
                   isSensitiveProjectPath(relativePath) ||
                   directoryName == "build" ||
                   directoryName == ".cache") {

                    itr.disable_recursion_pending();
                }
            }

            else if(!typeError) {
                typeError.clear();

                const bool isRegularFile =
                    itr->is_regular_file(typeError);

                if(!typeError && isRegularFile) {
                    // Resolve file symlinks before exposing the entry. A file
                    // alias inside the project may point outside the project.
                    std::error_code resolvedPathError;
                    const std::filesystem::path resolvedEntryPath =
                        std::filesystem::weakly_canonical(
                            entryPath,
                            resolvedPathError
                        );

                    if(!resolvedPathError) {
                        std::error_code resolvedRelativeError;
                        const std::filesystem::path resolvedRelativePath =
                            std::filesystem::relative(
                                resolvedEntryPath,
                                canonicalProjectRoot,
                                resolvedRelativeError
                            );

                        // Both the visible alias and resolved target must remain
                        // inside the project and satisfy visibility policy.
                        if(!resolvedRelativeError &&
                           !resolvedRelativePath.empty() &&
                           *resolvedRelativePath.begin() != ".." &&
                           entryPath.filename() != ".DS_Store" &&
                           !ignoreRules.isIgnored(relativePath) &&
                           !ignoreRules.isIgnored(resolvedRelativePath) &&
                           isProjectPathVisible(relativePath) &&
                           isProjectPathVisible(resolvedRelativePath)) {

                            files.push_back(
                                relativePath.generic_string()
                            );
                        }
                    }
                }
            }
        }

        iteratorError.clear();
        itr.increment(iteratorError);

        // Preserve entries already collected if traversal later encounters a
        // filesystem error.
        if(iteratorError) {
            break;
        }
    }

    // Keep listing output stable between requests.
    std::sort(
        files.begin(),
        files.end()
    );

    std::string text;

    for(const std::string& file : files) {
        text += file + '\n';
    }

    return {
        true,
        text
    };
}
