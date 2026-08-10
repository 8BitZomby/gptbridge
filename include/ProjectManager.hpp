#ifndef GPTB_PROJECT_MANAGER_HPP
#define GPTB_PROJECT_MANAGER_HPP

#include <filesystem>
#include <string>
#include <vector>


// Basic information about one registered project
struct ProjectInfo {
    std::string name;
    std::filesystem::path path;
};


// Returns all registered projects sorted by project name
std::vector<ProjectInfo> listProjects();


/**
 * findProjectByPath()
 * Returns the registered project name whose path exactly matches the given path.
 * An empty string means no registered project uses that exact path.
 */
std::string findProjectByPath(const std::filesystem::path& path);


/**
 * getProjectPath()
 * Returns the saved path for a registered project.
 * An empty path means the project name was not found.
 */
std::filesystem::path getProjectPath(const std::string& name);


/**
 * normalizeProjectPath()
 * Resolves a user-friendly project path into a normalized absolute path
 */
std::filesystem::path normalizeProjectPath(const std::string& path);


/**
 * projectExists()
 * Returns true if a project with this name exists in the global registry
 */
bool projectExists(const std::string& name);


#endif
