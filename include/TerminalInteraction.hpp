#ifndef GPTB_TERMINAL_INTERACTION_HPP
#define GPTB_TERMINAL_INTERACTION_HPP

#include <filesystem>
#include <string>


/**
 * TerminalInteraction
 *
 * Represents one shell command execution together with the terminal
 * output and execution context associated with that command.
 */
struct TerminalInteraction {

    // Command text executed by the shell
    std::string command;

    // Combined terminal output produced during the command execution
    std::string output;

    // Exit status reported when the command completed
    int exitCode;

    // Working directory in which the command was executed
    std::filesystem::path workingDirectory;

    // Time at which command execution began
    std::string startedAt;

    // Time at which command execution completed
    std::string finishedAt;
};


#endif
