#include "CommandLine.hpp"


/**
 * parseCommand()
 * Converts a top-level CLI command string into the corresponding Command value
 */
Command parseCommand(const std::string& command) {
    if(command == "add") { return Command::Add; }
    if(command == "capture") { return Command::Capture; }
    if(command == "clear") { return Command::Clear; }
    if(command == "init") { return Command::Init; }
    if(command == "list") { return Command::List; }
    if(command == "push") { return Command::Push; }
    if(command == "remove") { return Command::Remove; }
    if(command == "session") { return Command::Session; }
    if(command == "shell-init") { return Command::ShellInit; }
    if(command == "shell-event") { return Command::ShellEvent; }
    if(command == "show") { return Command::Show; }
    if(command == "status") { return Command::Status; }
    if(command == "use") { return Command::Use; }
    return Command::Unknown;

}
