#include "Storage.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>


/**
 * ensureStorageRoot()
 * Creates the global storage directory if it does not already exist.
 */
void ensureStorageRoot() {
    // create_directories() creates missing parent directories and does
    // nothing if the directory already exists.
    std::filesystem::create_directories(getStorageRoot());
}


/**
 * getStorageRoot()
 * Returns the global gptbridge storage directory, such as ~/.gptbridge.
 */
std::filesystem::path getStorageRoot() {
    // HOME gives the current user's home directory on macOS/Linux
    const char* home = std::getenv("HOME");
    // Without HOME, we cannot safely determine where global state belongs
    if(home == nullptr) {
        throw std::runtime_error("HOME environment variable is not set");
    }
    // std::filesystem joins path components without manual string concatenation
    return std::filesystem::path(home) / ".gptbridge";
}


/**
 * loadProjectRegistry()
 * Loads the full project registry from ~/.gptbridge/projects.json.
 */
nlohmann::json loadProjectRegistry() {
    // Build the full path to the global project registry
    const std::filesystem::path registryPath = getStorageRoot() / "projects.json";

    // If no registry exists yet, return an empty projects object
    if(!std::filesystem::exists(registryPath)) {
        return {
            {"projects", nlohmann::json::object()}
        };
    }
    // Open the existing registry so its JSON can be read from disk
    std::ifstream input(registryPath);

    // The file exists, so failing to open it indicates an I/O problem
    if(!input) {
        throw std::runtime_error("Failed to open projects.json for reading");
    }

    nlohmann::json registry;

    try {
        // Parse the JSON text into the in-memory registry object
        input >> registry;
    }
    catch(const nlohmann::json::parse_error&) {
        // Hide library-specific parser details behind a clearer error
        throw std::runtime_error("Failed to parse projects.json");
    }

    return registry;
}


/**
 * removeProject()
 * Removes a project from the global registry.
 * Returns true if the project existed and was removed.
 */
bool removeProject(const std::string& name) {
    // Load the current registry so the existing project set can be modified
    nlohmann::json registry = loadProjectRegistry();

    // Access the existing projects object once so reads and updates use same verified part of registry
    auto& projects = registry.at("projects");

    // If the project name is not registered, there is nothing to remove
    if(!projects.contains(name)) {
        return false;
    }
    // Erase the named project entry from the in-memory registry
    projects.erase(name);

    const std::filesystem::path registryPath = getStorageRoot() / "projects.json";

    // Rewrite the registry with the updated project set
    std::ofstream output(registryPath);

    if(!output) {
        throw std::runtime_error("Failed to open projects.json for writing");
    }
    // Keep the JSON human-readable for manual inspection and debugging
    output << registry.dump(4) << '\n';

    return true;
}


/**
 * saveProject()
 * Writes or updates a registered project in ~/.gptbridge/projects.json.
 */
void saveProject(const std::string& name, const std::filesystem::path& path) {

    // Make sure the global storage directory exists before opening the registry
    ensureStorageRoot();

    const std::filesystem::path registryPath = getStorageRoot() / "projects.json";

    // Load the current registry so adding a project preserves existing entries
    nlohmann::json registry = loadProjectRegistry();

    // Store paths as strings because JSON has no filesystem path type
    registry["projects"][name]["path"] = path.string();

    std::ofstream output(registryPath);

    if(!output) {
        throw std::runtime_error("Failed to open projects.json for writing");
    }

    // Indent by four spaces so the registry remains easy to inspect manually
    output << registry.dump(4) << '\n';
}
