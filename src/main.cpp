#include "CommandLine.hpp"
#include "ContextCommands.hpp"
#include "HelpCommand.hpp"
#include "InitCommand.hpp"
#include "McpCommands.hpp"
#include "ProjectCommands.hpp"
#include "RestoreCommand.hpp"
#include "SessionCommands.hpp"
#include "ShellCommands.hpp"
#include "StatusCommands.hpp"
#include "UseCommand.hpp"

#include <exception>
#include <iostream>


int main(int argc, char* argv[]) {

    // argc is number of CL arguments
    // argv contains argument strings -> argv[0] = gptb
    if(argc < 2) {
        std::cout << "Usage: gptb <command>\n";
        return 0;
    }
    // Parse first argument
    const Command command = parseCommand(argv[1]);

    try {
        // Command handler may throw for malformed JSON, filesystem failures,
        // or other storage errors. Handle those at the CLI boundary below.
        switch(command) {
            case Command::Capture: return handleCaptureCommand(argc, argv);
            case Command::Clear: return handleClearCommand(argc, argv);
            case Command::Help: return handleHelpCommand(argc, argv);
            case Command::Init: return handleInitCommand(argc, argv);
            case Command::Mcp: return handleMcpCommand(argc, argv);
            case Command::McpServer: return handleMcpServerCommand(argc, argv);
            case Command::Push: return handlePushCommand(argc, argv);
            case Command::Project: return handleProjectCommand(argc, argv);
            case Command::Remove: return handleRemoveCommand(argc, argv);
            case Command::Restore: return handleRestoreCommand(argc, argv);
            case Command::Session: return handleSessionCommand(argc, argv);
            case Command::ShellEvent: return handleShellEventCommand(argc, argv);
            case Command::ShellInit: return handleShellInitCommand(argc, argv);
            case Command::Show: return handleShowCommand(argc, argv);
            case Command::Status: return handleStatusCommand(argc, argv);
            case Command::Use: return handleUseCommand(argc, argv);

            case Command::Unknown:
                std::cout << "Unknown command: " << argv[1] << '\n';
                return 1;

            default:
                std::cout << "Command recognized but not implemented yet\n";
                return 0;
        }
    }
    catch(const std::exception& error) {
        // Convert internal failures into readable CLI error
        std::cerr << "gptb: " << error.what() << '\n';
        return 1;
    }
}
