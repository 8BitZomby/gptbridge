#ifndef GPTB_STORAGE_HPP
#define GPTB_STORAGE_HPP

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>


/**
 * ensureStorageRoot()
 * Creates the global storage directory if it does not already exist
 */
void ensureStorageRoot();


/**
 * getStorageRoot()
 * Returns the global gptbridge storage directory, such as ~/.gptbridge.
 */
std::filesystem::path getStorageRoot();


/**
 * loadProjectRegistry()
 * Loads the full project registry from ~/.gptbridge/projects.json.
 */
nlohmann::json loadProjectRegistry();


/**
 * removeProject()
 * Removes a project from the global registry.
 * Returns true if the project existed and was removed.
 */
bool removeProject(const std::string& name);


/**
 * saveProject()
 * Writes or updates a registered project in ~/.gptbridge/projects.json.
 */
void saveProject(const std::string& name, const std::filesystem::path& path);


#endif
