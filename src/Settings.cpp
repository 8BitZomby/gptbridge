#include "Settings.hpp"

#include "Storage.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>


namespace {

    /**
     * getSettingsPath()
     * Returns the global settings file stored beneath ~/.gptbridge.
     */
    std::filesystem::path getSettingsPath() {
        return getStorageRoot() / "settings.json";
    }


    /**
     * loadSettings()
     * Loads the complete global settings object.
     * A missing file represents an installation with default settings.
     */
    nlohmann::json loadSettings() {
        const std::filesystem::path settingsPath = getSettingsPath();

        if(!std::filesystem::exists(settingsPath)) {
            return nlohmann::json::object();
        }

        std::ifstream input(settingsPath);

        if(!input) {
            throw std::runtime_error("Failed to open settings file for reading");
        }

        nlohmann::json settings;

        try {
            input >> settings;
        }
        catch(const nlohmann::json::parse_error&) {
            throw std::runtime_error("Failed to parse settings file");
        }

        if(!settings.is_object()) {
            throw std::runtime_error("Invalid settings file");
        }

        return settings;
    }


    /**
     * pushModeToString()
     * Converts the typed push mode into its persistent JSON representation.
     */
    std::string pushModeToString(PushMode mode) {
        switch(mode) {
            case PushMode::Append:
                return "append";

            case PushMode::Replace:
                return "replace";
        }

        throw std::runtime_error("Invalid push mode");
    }

}


/**
 * getPushMode()
 * Loads the globally configured push mode.
 */
PushMode getPushMode() {
    const nlohmann::json settings = loadSettings();

    // Append is the safe default because it preserves context already selected
    // by the user when no explicit push mode has been configured.
    if(!settings.contains("push_mode")) {
        return PushMode::Append;
    }

    if(!settings["push_mode"].is_string()) {
        throw std::runtime_error("Invalid push_mode setting");
    }

    const std::string mode = settings["push_mode"].get<std::string>();

    if(mode == "append") {
        return PushMode::Append;
    }

    if(mode == "replace") {
        return PushMode::Replace;
    }

    throw std::runtime_error("Invalid push_mode setting: " + mode);
}


/**
 * setPushMode()
 * Stores the supplied global push mode while preserving other settings that
 * may be added to settings.json in the future.
 */
void setPushMode(PushMode mode) {
    // Preserve unrelated settings instead of rebuilding the file from only
    // the setting currently understood by this function.
    nlohmann::json settings = loadSettings();

    settings["push_mode"] = pushModeToString(mode);

    ensureStorageRoot();

    const std::filesystem::path settingsPath = getSettingsPath();

    // Create or repair the file with owner-read/write permissions before
    // replacing its contents.
    ensurePrivateFile(settingsPath);

    std::ofstream output(settingsPath);

    if(!output) {
        throw std::runtime_error("Failed to open settings file for writing");
    }

    output << settings.dump(4) << '\n';

    if(!output) {
        throw std::runtime_error("Failed to write settings file");
    }
}
