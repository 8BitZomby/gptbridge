#ifndef GPTB_COMMAND_LINE_HPP
#define GPTB_COMMAND_LINE_HPP

#include <string>


enum class Command {
    Status,
    Add,
    List,
    Remove,
    Use,
    Show,
    Clear,
    Push,
    Session,
    Unknown
};


/**
 * parseCommand()
 * Converts the user's top-level command text into a strongly typed value
 */
Command parseCommand(const std::string& command);


#endif
