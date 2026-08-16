#include "ContextPaths.hpp"
#include "SessionManager.hpp"


/**
 * getTerminalContextPath()
 * Resolves the terminal-context file belonging to the current gptbridge session
 */
std::filesystem::path getTerminalContextPath() {
    // Keep pushed terminal context with the rest of the current session state
    const std::filesystem::path sessionDirectory = getCurrentSessionDirectory();

    // The session directory may not exist yes if no persistent state has been written
    std::filesystem::create_directories(sessionDirectory);

    return sessionDirectory / "terminal-context.jsonl";
}
