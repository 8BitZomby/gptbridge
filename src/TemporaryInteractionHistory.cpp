#include "SessionManager.hpp"
#include "TemporaryInteractionHistory.hpp"
#include "TerminalInteraction.hpp"
#include "TerminalInteractionJson.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>


/**
 * TemporaryInteractionHistory()
 * Creates the temporary history location for one live gptbridge capture.
 * Each capture receives its own directory beneath the system temporary
 * directory so independent capture session cannot share terminal history.
 */
TemporaryInteractionHistory::TemporaryInteractionHistory(const std::string& captureId) {
    // Every live capture needs its own temporary history directory
    if(captureId.empty()) {
        throw std::runtime_error("Cannot create temporary interaction history with empty capture ID");
    }

    // Keep temporary history isolated by capture ID
    const std::filesystem::path captureDirectory =
        std::filesystem::temp_directory_path() / "gptbridge" / captureId;

    std::filesystem::create_directories(captureDirectory);

    // Full terminal history for this live capture
    historyPath = captureDirectory / "history.jsonl";
}


/**
 * append()
 * Adds one completed interaction to this capture's temporary history
 */
void TemporaryInteractionHistory::append(const TerminalInteraction& interaction) {

    // Open in append mode so previous interactions are preserved
    // without rereading or rewriting the entire history file
    std::ofstream output(historyPath, std::ios::app);

    if(!output) {
        throw std::runtime_error("Failed to open temporary interaction history for appending");
    }

    output << terminalInteractionToJson(interaction).dump() << '\n';
}


/**
 * loadAll()
 * Returns all terminal interactions recorded for this live capture in the
 * same order in which they completed
 */
std::vector<TerminalInteraction> TemporaryInteractionHistory::loadAll() const {

    // No history file means this capture has not completed and commands yet
    if(!std::filesystem::exists(historyPath)) {
        return {};
    }

    // Open the JSON Lines history file for reading
    std::ifstream input(historyPath);

    std::vector<TerminalInteraction> interactions;
    std::string line;

    while(std::getline(input, line)) {
        if(line.empty()) {
            continue;
        }

        try {
            // Parse one JSON record and restore the TerminalInteraction
            const nlohmann::json record = nlohmann::json::parse(line);
            interactions.push_back(terminalInteractionFromJson(record));
        }
        catch(const nlohmann::json::parse_error&) {
            throw std::runtime_error("Failed to parse temporary interaction history");
        }
    }

    return interactions;
}


/**
 * clearTmp()
 * Removes all temporary state belonging to this live capture
 */
void TemporaryInteractionHistory::clearTmp() {
    // Remove all temporary state belonging to this live capture
    const std::filesystem::path captureDirectory = historyPath.parent_path();

    if(std::filesystem::exists(captureDirectory)) {
        std::filesystem::remove_all(captureDirectory);
    }
}
