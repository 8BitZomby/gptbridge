#include "Config.hpp"
#include "SessionManager.hpp"
#include "PersistentSessionStorage.hpp"
#include "Random.hpp"
#include "Storage.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unistd.h>


/**
 * generateSessionId()
 * Generates a new stable identifier for a logical gptbridge session
 */
std::string generateSessionId() {
    return "s-" + generateSecureRandomHex(16);
}


/**
 * validateSessionId()
 * Rejects session identifiers that are unsafe to use as filesystem path components
 */
void validateSessionId(const std::string& sessionId) {
    // Session IDs must contain an actual name and must not be special path entries
    if(sessionId.empty() || sessionId == "." || sessionId == "..") {
        throw std::runtime_error("Invalid gptbridge session ID");
    }

    // Keep session IDs reasonably bounded before they are used in filesystem paths
    if(sessionId.size() > 128) {
        throw std::runtime_error("Invalid gptbridge session ID");
    }

    // Only permit characters that are safe inside one filesystem path component
    const bool validCharacters = std::all_of(
            sessionId.begin(),
            sessionId.end(),
            [](unsigned char character) {
                return std::isalnum(character) ||
                        character == '-' ||
                        character == '_' ||
                        character == '.';
            }
    );

    if(!validCharacters) {
        throw std::runtime_error("Invalid gptbridge session ID");
    }
}


/**
 * getActiveProject()
 * Returns the active project stored in the supplied persistent session.
 * An empty string means this session has no active project yet.
 */
std::string getActiveProject(const PersistentSessionStorage& sessionStorage) {
    // Resolve the session-state file through the shared persistent-storage
    // abstraction rather than constructing a session path locally
    const std::filesystem::path sessionPath = sessionStorage.getSessionStatePath();

    // A session file with no saved state has no active project
    if(!std::filesystem::exists(sessionPath)) {
        return "";
    }

    // Open the existing session state for reading
    std::ifstream input(sessionPath);

    if(!input) {
        throw std::runtime_error("Failed to open session file for reading");
    }
    // Parse the stored session state from JSON
    nlohmann::json session;

    try {
        // Parse the saved JSON state for this persistent session
        input >> session;
    }
    catch(const nlohmann::json::parse_error&) {
        // Convert library-specific parser failures into a clear session error
        throw std::runtime_error("Failed to parse session file");
    }

    // Older or incomplete session files may not contain an active project
    if(!session.contains("active_project")) {
        return "";
    }
    // Convert the JSON string value back into a C++ string
    return session.at("active_project").get<std::string>();
}


/**
 * getActiveProjectForCurrentSession()
 * Returns the active project stored for the current logical gptbridge session
 */
std::string getActiveProjectForCurrentSession() {
    // Resolve the current session once, then use the shared storage-aware reader
    return getActiveProject(PersistentSessionStorage::forCurrentSession());
}


/**
 * getCurrentSessionId()
 * Returns the logical gptbridge session associated with the current terminal
 */
std::string getCurrentSessionId() {

    // A managed child shell inherits the session ID of the terminal that
    // launched it, even though forkpty() assigns the child a new PTY device
    const char* inheritedSessionId = std::getenv("GPTB_SESSION_ID");

    if(inheritedSessionId != nullptr && *inheritedSessionId != '\0') {
        // Inherited session IDs originate from the environment, so validate
        // them before they can be used as filesystem path components
        validateSessionId(inheritedSessionId);

        return inheritedSessionId;
    }

    // ttyname() returns the terminal device attached to standard input,
    // for example "/dev/ttys003" on macOS
    const char* terminal = ttyname(STDIN_FILENO);

    // If stdin is not attached to a terminal, there is no per-terminal
    // session identity we can safely use
    if(terminal == nullptr) {
        throw std::runtime_error("Unable to determine current terminal session");
    }

    // Keep only the device name so it can be used safely as a session key/file name
    std::string sessionId = terminal;

    const std::size_t slash = sessionId.find_last_of('/');
    if(slash != std::string::npos) {
        sessionId = sessionId.substr(slash + 1);
    }

    // Keep one invariant: every session ID returned by this function is safe
    // to use as a single filesystem path component
    validateSessionId(sessionId);

    return sessionId;
}


/**
 * listSessions()
 * Scans the saved per-terminal session files and returns basic information
 * about each session, including its ID and currently active project.
 */
std::vector<SessionInfo> listSessions() {
    const std::filesystem::path sessionsDir = getStorageRoot() / "sessions";

    // No sessions have been saved yet if the directory does not exist
    if(!std::filesystem::exists(sessionsDir)) {
        return {};
    }
    std::vector<SessionInfo> sessions;

    // Inspect each saved per-terminal session directory
    for(const auto& entry : std::filesystem::directory_iterator(sessionsDir)) {
        // Each saved session is represented by its own directory
        if(!entry.is_directory()) {
            continue;
        }

        const std::filesystem::path sessionDir = entry.path();
        const std::filesystem::path statePath = sessionDir / "state.json";

        // Ignore incomplete session directories that do not contain state.json
        if(!std::filesystem::exists(statePath)) {
            continue;
        }

        std::ifstream input(statePath);

        if(!input) {
            throw std::runtime_error("Failed to open session file for reading");
        }

        nlohmann::json session;

        try {
            // Parse the saved state so we can inspect this session's metadata
            input >> session;
        }
        catch(const nlohmann::json::parse_error&) {
            throw std::runtime_error("Failed to parse session file: " + statePath.string());
        }

        SessionInfo info;

        // The directory name identifies the terminal session, e.g. "ttys007"
        info.id = sessionDir.filename().string();

        // A session may exist before any project has been selected
        if(session.contains("active_project")) {
            info.activeProject = session.at("active_project").get<std::string>();
        }
        sessions.push_back(info);
    }
    // Keep session output deterministic by sorting on the terminal session ID
    std::sort(
        sessions.begin(), sessions.end(), [](const SessionInfo& left, const SessionInfo& right) {
            return left.id < right.id;
        }
    );
    return sessions;
}


/**
 * setActiveProject()
 * Saves the active project in the supplied persistent session
 */
void setActiveProject(const PersistentSessionStorage& sessionStorage, const std::string& projectName) {
    // Writing persistent state requires the resolved session directory to exist
    // with the owner-only permissions enforced by PersistentSessionStorage
    sessionStorage.ensureSessionDirectoryExists();

    // Resolve state.json through the shared session-storage abstraction
    const std::filesystem::path sessionPath = sessionStorage.getSessionStatePath();

    nlohmann::json session;

    // Preserve any other session state if this session already has a file
    if(std::filesystem::exists(sessionPath)) {
        std::ifstream input(sessionPath);

        if(!input) {
            throw std::runtime_error("Failed to open session file for reading");
        }

        try {
            // Preserve unrelated fields already stored in this session
            input >> session;
        }
        catch(const nlohmann::json::parse_error&) {
            // Never overwrite malformed persistent state with partial new data
            throw std::runtime_error("Failed to parse session file");
        }
    }

    // Record which registered project is active for this session
    session["active_project"] = projectName;

    // Create or repair the state file with owner-only permissions before writing
    ensurePrivateFile(sessionPath);

    std::ofstream output(sessionPath);

    // Make sure the updated session state can actually be written to disk
    if(!output) {
        throw std::runtime_error("Failed to open session file for writing");
    }

    // Write formatted JSON so session files remain easy to inspect manually
    output << session.dump(4) << '\n';
}


/**
 * setActiveProjectForCurrentSession()
 * Saves the active project for the current logical gptbridge session
 */
void setActiveProjectForCurrentSession(const std::string& projectName) {
    // Resolve the current session once, then use the shared storage-aware writer
    setActiveProject(PersistentSessionStorage::forCurrentSession(), projectName);
}
