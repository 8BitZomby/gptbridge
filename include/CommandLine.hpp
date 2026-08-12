#ifndef GPTB_COMMAND_LINE_HPP
#define GPTB_COMMAND_LINE_HPP

#include <string>


enum class Command {
    Add,
    Capture,
    Clear,
    List,
    Push,
    Remove,
    Session,
    Show,
    Status,
    Use,
    Unknown
};


/**
 * parseCommand()
 * Converts the user's top-level command text into a strongly typed value
 */
Command parseCommand(const std::string& command);


#endif
