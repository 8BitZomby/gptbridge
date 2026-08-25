#ifndef GPTB_PROJECT_COMMANDS_HPP
#define GPTB_PROJECT_COMMANDS_HPP

#include <string>


/**
 * ProjectCommand
 * Identifies the operation requested under the `gptb project` namespace
 */
enum class ProjectCommand {
    Add,        // Registers a new project without changing the current session
    List,       // Lists all projects currently stored in the global project registry
    Remove,     // Removes one registered project from the global project registry
    Unknown     // Represents an unsupported project subcommand
};


/**
 * parseProjectCommand()
 * Converts a project subcommand string into the corresponding ProjectCommand
 */
ProjectCommand parseProjectCommand(const std::string& command);


/**
 * handleProjectCommand()
 * Validates and dispatches commands under `gptb project`.
 * Handles `gptb project <add|list|remove>`
 */
int handleProjectCommand(int argc, char* argv[]);


/**
 * Handles "gptb init <path> <project-name>".
 */
int handleInitProjectCommand(int argc, char* argv[]);


/**
 * Handles "gptb use <project|.>".
 */
int handleUseProjectCommand(int argc, char* argv[]);


#endif
