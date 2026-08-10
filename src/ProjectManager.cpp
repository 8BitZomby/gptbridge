#include "ProjectManager.hpp"
#include "Storage.hpp"

#include <algorithm>
#include <filesystem>


/**
 * listProjects()
 * Loads all registered projects, converts them into ProjectInfo objects,
 * and returns them sorted by project name for deterministic CLI output.
 */
std::vector<ProjectInfo> listProjects() {
    // Load the global project registry from persistent storage
    const nlohmann::json registry = loadProjectRegistry();

    std::vector<ProjectInfo> projects;

    // Convert each saved JSON entry into a strongly typed ProjectInfo object
    for(const auto& [name, project] : registry.at("projects").items()) {
        ProjectInfo info;

        info.name = name;
        info.path = project.at("path").get<std::string>();

        projects.push_back(info);
    }

    // Keep project output stable by sorting alphabetically by project name
    std::sort(
        projects.begin(), projects.end(), [](const ProjectInfo& left, const ProjectInfo& right) {
            return left.name < right.name;
        }
    );
    return projects;
}


/**
 * findProjectByPath()
 * Returns the registered project name whose path exactly matches the given path.
 * An empty string means no registered project uses that exact path.
 */
std::string findProjectByPath(const std::filesystem::path& path) {
    // Normalize the input first so relative forms such as "." compare reliably
    const std::filesystem::path normalizedPath = normalizeProjectPath(path.string());

    // Load all registered projects from persistent storage
    const nlohmann::json registry = loadProjectRegistry();

    // Compare against each saved project path using exact path equality
    for(const auto& [name, project] : registry.at("projects").items()) {
        // Read and normalize the stored project path before comparing it
        const std::filesystem::path savedPath = normalizeProjectPath(project.at("path").get<std::string>());

        if(savedPath == normalizedPath) {
            return name;
        }
    }
    // No registered project uses this exact directory
    return "";
}


/**
 * getProjectPath()
 * Returns the saved path for a registered project.
 * An empty path means the project name was not found.
 */
std::filesystem::path getProjectPath(const std::string& name) {
    // Load the saved project registry from persistent storage
    const nlohmann::json registry = loadProjectRegistry();

    // Access the projects object safely; a malformed registry will throw
    const auto& projects = registry.at("projects");
    if(!projects.contains(name)) {
        return {};
    }
    // Convert the stored JSON string back into a filesystem path
    return projects.at(name).at("path").get<std::string>();
}


/**
 * normalizeProjectPath()
 * Resolves a user-friendly project path into a normalized absolute path.
 * weakly_canonical() resolves relative components such as "." and ".."
 * while still handling paths that may not fully exist yet
 */
std::filesystem::path normalizeProjectPath(const std::string& path) {
    return std::filesystem::weakly_canonical(path);
}


/**
 * projectExists()
 * Returns true if a project with this name exists in the global registry
 */
bool projectExists(const std::string& name) {
    // Load the saved project registry from persistent storage
    const nlohmann::json registry = loadProjectRegistry();

    // A registered project's name appears as a key under the "projects" object
    return registry.at("projects").contains(name);
}
