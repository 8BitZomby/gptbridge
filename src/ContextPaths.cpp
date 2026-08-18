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

    const std::filesystem::path sessionsDirectory = getStorageRoot() / "sessions";
    // Keep the shared sessions directory private so saved session IDs
    // cannot be enumerated by other local users
    ensurePrivateDirectory(sessionsDirectory);

    const std::filesystem::path sessionDirectory = sessionsDirectory / sessionId;
    // Ensure the specific session directory is also owner only
    ensurePrivateDirectory(sessionDirectory);

    return sessionDirectory / "terminal-context.jsonl";
}
