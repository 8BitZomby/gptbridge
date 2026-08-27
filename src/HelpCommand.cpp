#include "HelpCommand.hpp"

#include <iostream>


/**
 * handleHelpCommand()
 * Displays the top-level gptb command reference
 */
int handleHelpCommand(int argc, char* argv[]) {
    // Top-level help does not accept additional arguments yet.
    if(argc != 2) {
        std::cout << "Usage: gptb --help\n";
        return 1;
    }

    // Display the command reference in a compact aligned layout.
    std::cout
        << "gptb - terminal integration for AI clients\n"
        << '\n'
        << "Usage: gptb <command> [arguments]\n"
        << '\n'
        << "Commands:\n"
        << "  ask <question...>                 Ask Claude about the active project and pushed terminal context\n"
        << "  clear                             Clear persistent terminal context\n"
        << "  init <path> <project-name>        Register a project and create a new logical session\n"
        << "  mcp sync                          Synchronize MCP with the logical session attached to this shell\n"
        << "  project add <name> <path>         Register a project without changing the current session\n"
        << "  project list                      List registered projects\n"
        << "  project remove <name>             Unregister a project without deleting its files\n"
        << "  push [count|append|replace]        Push terminal interactions or set the persistent push mode\n"
        << "  restore [session-id]              Restore the most recent or explicitly requested saved session\n"
        << "  session close <session-id>        Close a live session while preserving its saved data\n"
        << "  session delete <session-id>       Permanently delete an inactive logical session\n"
        << "  session list                      List saved logical sessions and their runtime state\n"
        << "  session restore [session-id]      Restore the most recent or explicitly requested saved session\n"
        << "  show                              Display persistent terminal context\n"
        << "  status                            Show the session and project attached to this shell\n"
        << "  use <project-name|.>              Change the active project for the current session\n"
        << '\n'
        << "Options:\n"
        << "  -h, --help                        Show this help\n"
        << "  -V, --version                     Show the gptb version\n";

    return 0;
}
