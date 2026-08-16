#include "TerminalInteractionJson.hpp"

#include <string>


/**
 * terminalInteractionToJson()
 * Conerts one captured terminal interaction into a JSON object. Keeping this
 * conversion in one place ensures temp history and persistent terminal
 * context use the same storage format
 */
nlohmann::json terminalInteractionToJson(const TerminalInteraction& interaction) {
    // Preserve the exact command text entered in the shell
    // and the output captured while that command executed
    return {
        // Store command input
        {"command", interaction.command},
        // Store terminal output
        {"output", interaction.output},
        // Store the command's completion status
        {"exit_code", interaction.exitCode},
        // Store the directory in which the command was executed
        {"working_directory", interaction.workingDirectory},
        // Store execution timestamps for ordering and timing
        {"started_at", interaction.startedAt},
        {"finished_at", interaction.finishedAt}
    };
}

/**
 * terminalInteractionFromJson()
 * Converts one stored JSON record back into the strongly typed
 * TerminalInteraction model used throughout gptbridge
 */
TerminalInteraction terminalInteractionFromJson(const nlohmann::json& record) {
    // Build one interaction from the fields
    TerminalInteraction interaction;

    // Restore original command text
    interaction.command = record.at("command").get<std::string>();
    // Restore all captured output
    interaction.output = record.at("output").get<std::string>();
    // Restore the process exit status
    interaction.exitCode = record.at("exit_code").get<int>();
    // Restore the working directory
    interaction.workingDirectory = record.at("working_directory").get<std::string>();
    // Restore the timestamps at command boundaries
    interaction.startedAt = record.at("started_at").get<std::string>();
    interaction.finishedAt = record.at("finished_at").get<std::string>();

    return interaction;
}
