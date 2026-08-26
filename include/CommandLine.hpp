#ifndef GPTB_COMMAND_LINE_HPP
#define GPTB_COMMAND_LINE_HPP

#include <string>


enum class Command {
    Capture,
    Clear,
    Help,
    Init,
    Mcp,
    McpServer,
    Push,
    Project,
    Remove,
    Restore,
    Session,
    ShellInit,
    ShellEvent,
    Show,
    Status,
    Use,
    Version,
    Unknown
};


/**
 * parseCommand()
 * Converts the user's top-level command text into a strongly typed value
 */
Command parseCommand(const std::string& command);


#endif
