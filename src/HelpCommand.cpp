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
    // Display help output
    std::cout
        << "gptb - terminal integration for ChatGPT\n"
        << '\n'
        << "Usage:\n"
        << "  gptb <command> [arguments]\n"
        << '\n'
        << "Commands:\n"
        << "  init <path> <project-name>\n"
        << "      Register a project and create a new logical session.\n"
        << '\n'
        << "  restore [session-id]\n"
        << "      Restore the most recent or explicitly requested saved session.\n"
        << '\n'
        << "  status\n"
        << "      Show the session and project attached to this shell.\n"
        << '\n'
        << "  session list\n"
        << "      List saved logical sessions and their runtime state.\n"
        << '\n'
        << "  session close <session-id>\n"
        << "      Close a live session while preserving its saved data.\n"
        << '\n'
        << "  session delete <session-id>\n"
        << "      Permanently delete an inactive logical session.\n"
        << '\n'
        << "  session restore [session-id]\n"
        << "      Restore the most recent or explicitly requested saved session.\n"
        << '\n'
        << "  project list\n"
        << "      List registered projects.\n"
        << '\n'
        << "  project add <name> <path>\n"
        << "      Register a project without changing the current session.\n"
        << '\n'
        << "  project remove <name>\n"
        << "      Unregister a project without deleting its files.\n"
        << '\n'
        << "  use <project-name|.>\n"
        << "      Change the active project for the current session.\n"
        << '\n'
        << "  push <count>\n"
        << "      Copy recent terminal interactions into persistent context.\n"
        << '\n'
        << "  show\n"
        << "      Display persistent terminal context.\n"
        << '\n'
        << "  clear\n"
        << "      Clear persistent terminal context.\n"
        << '\n'
        << "Options:\n"
        << "  -h, --help\n"
        << "      Show this help.\n";

    return 0;
}
