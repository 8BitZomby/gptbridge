#include "SessionManager.hpp"

#include "McpState.hpp"
#include "PersistentSessionStorage.hpp"
#include "SessionAttachment.hpp"
#include "SessionId.hpp"
#include "Storage.hpp"
#include "TimeUtils.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>


/**
 * allocateSessionId()
 * Reserves and returns the next persistent logical gptbridge session ID
 */
std::string allocateSessionId() {
    const std::uint64_t sessionNumber = allocateNextSessionNumber();
    return formatSessionId(sessionNumber);
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
 * createSession()
 * Creates a new persistent logical gptbridge session and returns its ID
 */
std::string createSession() {
    // Reserve a unique logical session ID and its session directory
    const std::string sessionId = allocateSessionId();

    // Resolve persistent storage directly from the new logical session ID
    const PersistentSessionStorage sessionStorage = PersistentSessionStorage::forExplicitSessionId(sessionId);

    // A valid persistent session has a state.json file even before a project
    // has been selected
    const std::filesystem::path statePath = sessionStorage.getSessionStatePath();

    try {
        // Initialize the session with an empty state object only after the complete
        // JSON has been written successfully to a private temporary file.
        writePrivateFileAtomically(
            statePath,
            nlohmann::json::object().dump(4) + '\n'
        );
    }
    catch(...) {
        // The ID has not been returned to a caller yet, so a failed
        // initialization should not leave a half-created session directory

        // Use the non-throwing filesystem overloads here because cleanup is happening
        // while another exception is already active. If remove() threw a new exception
        // it could hide the original error that actually caused session creation to fail
        std::error_code cleanupError;

        // Remove a partially created state.json if one exists
        std::filesystem::remove(
                statePath,
                cleanupError
        );

        // Reuse the same error_code for the directory cleanup attempt
        cleanupError.clear();

        // Remove the reserved session directory once its state file is gone.
        // Cleanup is best-effort; the original initialization failure is rethrown below
        std::filesystem::remove(
                sessionStorage.getSessionDirectory(),
                cleanupError
        );

        throw;
    }

    return sessionId;
}


/**
 * deleteSession()
 * Permanently deletes an inactive logical session and all of its persistent data
 */
void deleteSession(const std::string& sessionId) {
    // Validate the supplied ID before resolving its persisten filesystem path
    validateSessionId(sessionId);

    // Get data for session
    const PersistentSessionStorage sessionStorage = PersistentSessionStorage::forExplicitSessionId(sessionId);
    const std::filesystem::path sessionDirectory = sessionStorage.getSessionDirectory();
    const std::filesystem::path statePath = sessionStorage.getSessionStatePath();

    // state.json distinguishes an intitialized logical session from an
    // incomplete or unrelated directory under the sessions root
    if(!std::filesystem::exists(statePath)) {
        throw std::runtime_error("Session does not exist: " + sessionId);
    }

    // Remove records left behind by crashed processes before check whether
    // any managed-shell attachments are still genuinely live
    removeStaleSessionAttachments(sessionId);

    if(hasLiveSessionAttachments(sessionId)) {
        throw std::runtime_error("Session is active. Close it before deleting: " + sessionId);
    }

    std::error_code removalError;

    // Deleting a logical session intentionally removes all persistent data
    // belonging to it, including terminal context and attachment records
    std::filesystem::remove_all(sessionDirectory, removalError);

    if(removalError) {
        throw std::runtime_error("Failed to delete session: " + sessionId);
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
 * Returns the logical session attached to the current managed shell.
 * Returns std::nullopt when this process is not inside a managed session.
 */
std::optional<std::string> getCurrentSessionId() {
    // Managed child shells inherit the stable logical session ID chosen by
    // the parent gptb process
    const char* inheritedSessionId = std::getenv("GPTB_SESSION_ID");

    if(inheritedSessionId == nullptr || *inheritedSessionId == '\0') {
        return std::nullopt;
    }

    // Environment-provided session IDs must be validated before they are used
    // as filesystem path components
    validateSessionId(inheritedSessionId);
    return std::string(inheritedSessionId);
}


/**
 * listSessions()
 * Scans the saved logical session directories and returns basic information
 * about each session, including its ID and currently active project.
 */
std::vector<SessionInfo> listSessions() {
    const std::filesystem::path sessionsDir = getStorageRoot() / "sessions";

    // No sessions have been saved yet if the directory does not exist
    if(!std::filesystem::exists(sessionsDir)) {
        return {};
    }
    std::vector<SessionInfo> sessions;

    // Inspect each saved logical session directory
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

        // The directory name identifies the logical session, e.g. "s-0001"
        info.id = sessionDir.filename().string();

        // A session may exist before any project has been selected
        if(session.contains("active_project")) {
            info.activeProject = session.at("active_project").get<std::string>();
        }

        // Remove attachment records left behind by crashed or force-killed gptb
        // processes before determining the session's current runtime state
        removeStaleSessionAttachments(info.id);

        // A session is active when at least one managed-shell attachment is still live
        info.active = hasLiveSessionAttachments(info.id);

        sessions.push_back(info);
    }
    // Keep session output deterministic by sorting on the logical session ID
    std::sort(
        sessions.begin(), sessions.end(), [](const SessionInfo& left, const SessionInfo& right) {
            return left.id < right.id;
        }
    );
    return sessions;
}


/**
 * getMostRecentlyUsedSessionId()
 * Returns the logical session ID with the newest last_used_at timestamp.
 * Returns an empty string when no saved session has a usage timestamp
 */
std::string getMostRecentlyUsedSessionId() {
    const std::filesystem::path sessionsDir = getStorageRoot() / "sessions";

    // No logical session exists yet if the parent directory is missing
    if(!std::filesystem::exists(sessionsDir)) {
        return "";
    }

    std::string mostRecentSessionId;
    std::string mostRecentTimestamp;

    // Inspect each initialized logical session
    for(const auto& entry : std::filesystem::directory_iterator(sessionsDir)) {
        if(!entry.is_directory()) {
            continue;
        }

        const std::filesystem::path statePath = entry.path() / "state.json";

        // Ignore incomplete reserved directories that do not have session state
        if(!std::filesystem::exists(statePath)) {
            continue;
        }

        std::ifstream input(statePath);

        if(!input) {
            throw std::runtime_error("Failed to open session file for reading");
        }

        nlohmann::json session;

        try {
            input >> session;
        }
        catch(const nlohmann::json::parse_error&) {
            throw std::runtime_error("Failed to parse session file: " + statePath.string());
        }

        // Older sessions may predate last_used_at. They cannot participate in
        // most-recent selection until they are explicitly activated again
        if(!session.contains("last_used_at")) {
            continue;
        }

        const std::string timestamp = session.at("last_used_at").get<std::string>();

        // ISO 8601 UTC timestamps in the shared YYYY-MM-DDTHH:MM:SSZ format
        // sort lexicographically in chronological order
        if(mostRecentSessionId.empty() || timestamp > mostRecentTimestamp) {
            mostRecentTimestamp = timestamp;
            mostRecentSessionId = entry.path().filename().string();
        }
    }

    return mostRecentSessionId;
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

    // Replace the committed session state only after the complete updated JSON
    // has been written successfully.
    writePrivateFileAtomically(
        sessionPath,
        session.dump(4) + '\n'
    );

    // Changing a session's active project also makes that logical session the
    // current MCP target so connected clients follow the user's active work
    trySetMcpActiveSessionId(sessionStorage.getSessionId());
}


/**
 * markSessionUsed()
 * Records the current time as the session's most recent use
 */
void markSessionUsed(const PersistentSessionStorage& sessionStorage) {
    // A session must have a persistent state file before its usage timestamp
    // can be updated
    sessionStorage.ensureSessionDirectoryExists();

    const std::filesystem::path sessionPath = sessionStorage.getSessionStatePath();

    nlohmann::json session;

    // Preserve existing session properties such as active_project while
    // updating only the last-used timestamp
    if(std::filesystem::exists(sessionPath)) {
        std::ifstream input(sessionPath);

        if(!input) {
            throw std::runtime_error("Failed to open session file for reading");
        }

        try {
            input >> session;
        }
        catch(const nlohmann::json::parse_error&) {
            // Do not overwrite malformed state just to update its timestamp
            throw std::runtime_error("Failed to parse session file");
        }
    }

    // Store UTC ISO 8601 so timestamps from different sessions can be compared
    // directly without depending on the user's local timezone
    session["last_used_at"] = currentTimestampUtc();

    // Replace the committed session state only after the updated timestamp has
    // been written successfully.
    writePrivateFileAtomically(
        sessionPath,
        session.dump(4) + '\n'
    );
}


/**
 * setActiveProjectForCurrentSession()
 * Saves the active project for the current logical gptbridge session
 */
void setActiveProjectForCurrentSession(const std::string& projectName) {
    // Resolve the current session once, then use the shared storage-aware writer
    setActiveProject(PersistentSessionStorage::forCurrentSession(), projectName);
}
