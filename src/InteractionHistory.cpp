#include "InteractionHistory.hpp"
#include "SessionManager.hpp"
#include "Storage.hpp"
#include "TerminalInteraction.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>


/**
 * append()
 *
 * Adds one completed terminal interaction to the current
 * session's persistent history file.
 */
void InteractionHistory::append(const TerminalInteraction& interaction) {
    // Resolve the directory that owns all persisten state for this session
    const std::filesystem::path sessionDirectory = getCurrentSessionDirectory();

    // History may be the first state written for a new session, so ensure the
    // session directory exists rather than depending on another component to
    // have created it previously.

    // Keep persistent session history private to the current user
    ensurePrivateDirectory(sessionDirectory);

    // Store interaction history beside the other files belonging to the session
    const std::filesystem::path historyPath = sessionDirectory / "history.json";

    // Ensure the persistent history file exists with owner-only permissions
    ensurePrivateFile(historyPath);

    nlohmann::json history = nlohmann::json::array();

    // Preserve existing interactions if a history file already exists
    if(std::filesystem::exists(historyPath)) {
        std::ifstream input(historyPath);
        if(!input) {
            throw std::runtime_error("Failed to open interaction history for reading");
        }
        try {
            input >> history;
        } catch(const nlohmann::json::parse_error&) {
            throw std::runtime_error("Failed to parse interaction history");
        }
    }

    // Convert the strongly typed interaction into a JSON record
    history.push_back({
            {"command", interaction.command},
            {"output", interaction.output},
            {"exit_code", interaction.exitCode},
            {"working_directory", interaction.workingDirectory},
            {"started_at", interaction.startedAt},
            {"finished_at", interaction.finishedAt}
    });

    std::ofstream output(historyPath);

    if(!output) {
        throw std::runtime_error("Failed to open interaction history for writing");
    }

    // Keep the file readable because history will be useful for debugging capture
    output << history.dump(4) << '\n';
}


/**
 * loadAll()
 *
 * Loads all terminal interactions recorded for the current session in the same order they were stored
 */
std::vector<TerminalInteraction> InteractionHistory::loadAll() const {
    // Interaction history is stored inside the current session directory
    const std::filesystem::path historyPath = getCurrentSessionDirectory() / "history.json";

    // No history file means this session has not recorded any interactions yet
    if(!std::filesystem::exists(historyPath)) {
        return {};
    }

    std::ifstream input(historyPath);

    if(!input) {
        throw std::runtime_error("Failed to open interaction history for reading");
    }

    nlohmann::json history;

    try {
        // Parse the stored interaction array from disk
        input >> history;
    } catch(const nlohmann::json::parse_error&) {
        throw std::runtime_error("Failed to parse interaction history");
    }

    std::vector<TerminalInteraction> interactions;

    // Convert each JSON record back into the strongly typed interaction model
    for(const auto& record : history) {
        TerminalInteraction interaction;
        interaction.command = record.at("command").get<std::string>();
        interaction.output = record.at("output").get<std::string>();
        interaction.exitCode = record.at("exit_code").get<int>();
        interaction.workingDirectory = record.at("working_directory").get<std::string>();
        interaction.startedAt = record.at("started_at").get<std::string>();
        interaction.finishedAt = record.at("finished_at").get<std::string>();
        interactions.push_back(interaction);
    }
    return interactions;
}
