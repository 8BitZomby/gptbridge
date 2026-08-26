#ifndef GPTB_MCP_PROJECT_FILESYSTEM_HPP
#define GPTB_MCP_PROJECT_FILESYSTEM_HPP

#include <filesystem>
#include <string>


/**
 * McpProjectFileResult
 * Represents the result of one project-file operation independently from the
 * MCP/JSON-RPC transport that requested it.
 */
struct McpProjectFileResult {
    bool success = false;
    std::string text;
};


/**
 * readMcpProjectFile()
 * Reads one MCP-visible project file while enforcing project containment,
 * symlink, .gptignore, visibility, and file-size policies.
 */
McpProjectFileResult readMcpProjectFile(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& requestedPath
);


/**
 * searchMcpProjectFiles()
 * Searches MCP-visible project files for exact case-sensitive text while
 * enforcing project containment, symlink, .gptignore, visibility, and
 * file-size policies.
 */
McpProjectFileResult searchMcpProjectFiles(
    const std::filesystem::path& projectRoot,
    const std::string& query
);


#endif
