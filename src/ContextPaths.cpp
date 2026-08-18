#include "ContextPaths.hpp"
#include "SessionManager.hpp"
#include "Storage.hpp"
#include <filesystem>


/**
 * getTerminalContextPath()
 * Resolves the terminal-context file belonging to the current gptbridge session
 */
std::filesystem::path getTerminalContextPath() {
    // Keep pushed terminal context with the rest of the current session state
    const std::filesystem::path sessionDirectory = getCurrentSessionDirectory();

    // The session directory may not exist yes if no persistent state has been written
    ensurePrivateDirectory(sessionDirectory);

    return sessionDirectory / "terminal-context.jsonl";
}


/**
 * getTerminalContextPathForSession()
 * Resolves the terminal-context file belonging to a specific session
 */
std::filesystem::path getTerminalContextPathForSession(const std::string& sessionId) {
    // reject unsafe identifiers before using the session ID in a filesystem path
    validateSessionId(sessionId);

    // MCP and other non-terminal callers must be able to address a session explicitly
    const std::filesystem::path sessionDirectory = getStorageRoot() / "sessions" / sessionId;

    // Ensure the session directory exists and remains private to the current user
    ensurePrivateDirectory(sessionDirectory);

    return sessionDirectory / "terminal-context.jsonl";
}
