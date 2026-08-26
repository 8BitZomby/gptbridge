#include "CommandLine.hpp"


/**
 * parseCommand()
 * Converts a top-level CLI command string into the corresponding Command value
 */
Command parseCommand(const std::string& command) {
    if(command == "-h") { return Command::Help; }
    if(command == "--help") { return Command::Help; }
    if(command == "-V") { return Command::Version; }
    if(command == "--version") { return Command::Version; }
    if(command == "capture") { return Command::Capture; }
    if(command == "clear") { return Command::Clear; }
    if(command == "init") { return Command::Init; }
    if(command == "mcp") { return Command::Mcp; }
    if(command == "mcp-server") { return Command::McpServer; }
    if(command == "push") { return Command::Push; }
    if(command == "project") { return Command::Project; }
    if(command == "remove") { return Command::Remove; }
    if(command == "restore") { return Command::Restore; }
    if(command == "session") { return Command::Session; }
    if(command == "shell-init") { return Command::ShellInit; }
    if(command == "shell-event") { return Command::ShellEvent; }
    if(command == "show") { return Command::Show; }
    if(command == "status") { return Command::Status; }
    if(command == "use") { return Command::Use; }

    return Command::Unknown;

}
