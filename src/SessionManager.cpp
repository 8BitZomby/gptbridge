#include "Config.hpp"
#include "SessionManager.hpp"
#include "Storage.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unistd.h>


/**
 * getActiveProject()
 * Returns the active project for the current terminal.
 * An empty string means this session has no active project yet.
 */
std::string getActiveProject() {
    // Resolve the state file belonging to the current terminal
    const std::filesystem::path sessionPath = getCurrentSessionPath();

    // A session file does not exist until this terminal has stored state
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
        // Parse the saved state for the current global/per-terminal session
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
 * getCurrentSessionId()
 * Returns an identifier for the terminal session that invoked gptb
 */
std::string getCurrentSessionId() {
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
    return sessionId;
}


/**
 * getCurrentSessionPath()
 * Returns the JSON file used to store the active session state.
 * Global mode uses one shared file, while per-terminal mode uses a
 * separate file based on the current terminal's session ID.
 */
std::filesystem::path getCurrentSessionPath() {
    // Global mode shares one session file across all terminals
    if(getSessionMode() == SessionMode::Global) {
        return getStorageRoot() / "global-session.json";
    }
    // Per-terminal mode keeps independent state for each terminal
    const std::filesystem::path sessionsDir = getStorageRoot() / "sessions";

    // Create the sessions directory on first use
    std::filesystem::create_directories(sessionsDir);

    // Use the terminal device name as the per-terminal session file name
    return sessionsDir / (getCurrentSessionId() + ".json");
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

    // Inspect each file under ~/.gptbridge/sessions/.
    for(const auto& entry : std::filesystem::directory_iterator(sessionsDir)) {
        // Ignore directories or other non-file entries
        if(!entry.is_regular_file()) {
            continue;
        }

        const std::filesystem::path path = entry.path();

        // Session files use names such as "ttys007.json"
        if(path.extension() != ".json") {
            continue;
        }

        std::ifstream input(path);

        if(!input) {
            throw std::runtime_error("Failed to open session file for reading");
        }

        nlohmann::json session;

        try {
            // Parse the saved state so we can inspect this session's metadata
            input >> session;
        }
        catch(const nlohmann::json::parse_error&) {
            throw std::runtime_error("Failed to parse session file: " + path.filename().string());
        }

        SessionInfo info;
        // The file name identifies the terminal session, e.g. "ttys007"
        info.id = path.stem().string();
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
 * Saves the active project name for the current terminal session
 */
void setActiveProject(const std::string& projectName) {
    // Resolve the JSON file belonging to the terminal running this command
    const std::filesystem::path sessionPath = getCurrentSessionPath();

    nlohmann::json session;

    // Preserve any other session state if this terminal already has a file
    if(std::filesystem::exists(sessionPath)) {
        std::ifstream input(sessionPath);

        // An existing session file that cannot be opened indicates an I/O error
        if(!input) {
            throw std::runtime_error("Failed to open session file for reading");
        }

        try {
            // Parse the existing session so unrelated session fields are preserved
            input >> session;
        }
        catch(const nlohmann::json::parse_error&) {
            // Refuse to overwrite a malformed session file with new state
            throw std::runtime_error("Failed to parse session file");
        }
    }

    // Record which registered project is active for this session
    session["active_project"] = projectName;

    std::ofstream output(sessionPath);

    // Make sure the updated session state can actually be written to disk
    if(!output) {
        throw std::runtime_error("Failed to open session file for writing");
    }

    // Write formatted JSON so session files remain easy to inspect manually
    output << session.dump(4) << '\n';
}
