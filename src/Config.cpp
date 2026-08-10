#include "Config.hpp"
#include "Storage.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>


/**
 * getSessionMode()
 * Loads the configured session mode from ~/.gptbridge/config.json.
 * Defaults to per-terminal mode when no saved setting exists.
 */
SessionMode getSessionMode() {
    // Global preferences are stored separately from project/session state
    const std::filesystem::path configPath = getStorageRoot() / "config.json";

    // First use defaults to per-terminal mode until user chooses otherwise
    if(!std::filesystem::exists(configPath)) {
        return SessionMode::PerTerminal;
    }

    // Open and parse the existing global configuration
    std::ifstream input(configPath);

    if(!input) {
        throw std::runtime_error("Failed to open config.json for reading");
    }

    nlohmann::json config;
    try {
        input >> config;
    }
    catch(const nlohmann::json::parse_error&) {
        // Convert library-specific parsing errors into a clear gptbridge error
        throw std::runtime_error("Failed to parse config.json");
    }

    // Missing session_mode also falls back to the default
    if(!config.contains("session_mode")) {
        return SessionMode::PerTerminal;
    }
    // Convert the stored text into the SessionMode enum used by the program
    return sessionModeFromString(config.at("session_mode").get<std::string>());
}


/**
 * sessionModeToString()
 * Returns a human-readable name for a session mode
 */
std::string sessionModeToString(SessionMode mode) {
    // Keep all enum-to-text conversion in one place for consistent CLI output
    switch(mode) {
        case SessionMode::Global:
            return "global";
        case SessionMode::PerTerminal:
            return "per-terminal";
    }
    return "unknown";
}


/**
 * sessionModeFromString()
 * Returns session mode from a string
 */
SessionMode sessionModeFromString(const std::string& mode) {
    // Convert the canonical stored/CLI text back into the strongly typed enum
    if(mode == "global") { return SessionMode::Global; }
    if(mode == "per-terminal") { return SessionMode::PerTerminal; }
    // An unrecognized value means the config or caller supplied invalid data
    throw std::invalid_argument("Unknown session mode: " + mode);
}


/**
 * setSessionMode()
 * Saves the selected session mode to the global config file
 */
void setSessionMode(SessionMode mode) {
    // Make sure ~/.gptbridge exists before attempting to write config.json
    ensureStorageRoot();

    const std::filesystem::path configPath = getStorageRoot() / "config.json";

    nlohmann::json config;

    // Preserve future config fields instead of replacing the whole file
    if(std::filesystem::exists(configPath)) {
        std::ifstream input(configPath);

        if(!input) {
            throw std::runtime_error("Failed to open config.json for reading");
        }
        try {
            // Parse the existing configuration so unrelated settings are preserved
            input >> config;
        }
        catch(const nlohmann::json::parse_error&) {
            // Do not overwrite a malformed config file with new configuration data
            throw std::runtime_error("Failed to parse config.json");
        }
    }

    // Convert the enum into the single canonical string used in config.json
    config["session_mode"] = sessionModeToString(mode);

    std::ofstream output(configPath);
    if(!output) {
        throw std::runtime_error("Failed to open config.json for writing");
    }
    // Keep the configuration human-readable for inspection and debugging
    output << config.dump(4) << '\n';
}
