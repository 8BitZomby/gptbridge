#ifndef GPTB_STORAGE_HPP
#define GPTB_STORAGE_HPP

#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>


/**
 * allocateNextSessionNumber()
 * Finds, reserves, and returns the next available logical session number
 */
std::uint64_t allocateNextSessionNumber();


/**
 * ensurePrivateDirectory()
 * Creates a directory tree and restricts it to the current user
 */
void ensurePrivateDirectory(const std::filesystem::path& path);


/**
 * ensurePrivateFile()
 * Creates a file with owner-only permission or repairs an existing file's permission
 */
void ensurePrivateFile(const std::filesystem::path& path);


/**
 * writePrivateFileAtomically()
 * Replaces a persistent file only after its complete new contents have been
 * written and synchronized to a private temporary file in the same directory
 */
void writePrivateFileAtomically(const std::filesystem::path& path, const std::string& contents);


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
