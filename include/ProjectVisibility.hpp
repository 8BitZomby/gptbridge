#ifndef GPTB_PROJECT_VISIBILITY_HPP
#define GPTB_PROJECT_VISIBILITY_HPP

#include <filesystem>


/**
 * isProjectPathVisible()
 * Returns true when a project-relative path is allowed to be exposed to MCP clients
 */
bool isProjectPathVisible(const std::filesystem::path& relativePath);


#endif
