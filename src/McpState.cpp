#include "McpState.hpp"

#include "SessionManager.hpp"
#include "Storage.hpp"

#include <exception>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>


namespace {

    /**
     * getMcpStatePath()
     * Returns the path to the global state file that tells the MCP server
     * which logical gptb session it should currently expose
     */
    std::filesystem::path getMcpStatePath() {
        return getStorageRoot() / "mcp-state.json";
    }
}


/**
 * getMcpActiveSessionId()
 * Loads the logical session currently selected for MCP access.
 *
 * std::nullopt means no session has been selected yet. A malformed or invalid
 * state file is treated as an error because silently choosing another session
 * could expose the wrong project or terminal context
 */
std::optional<std::string> getMcpActiveSessionId() {
    const std::filesystem::path statePath = getMcpStatePath();

    // No state file means gptbridge has not selected an MCP-active session yet
    if(!std::filesystem::exists(statePath)) {
        return std::nullopt;
    }

    // Open the existing state file so the selected logical session can be read
    std::ifstream input(statePath);

    if(!input) {
        throw std::runtime_error("Failed to open MCP state file for reading");
    }

    nlohmann::json state;

    try {
        // Parse the saved JSON
        input >> state;
    }
    catch(const nlohmann::json::parse_error&) {
        // Do not silently ignore malformed global state, since that could cause
        // MCP to resolve a different session than the one the user intended
        throw std::runtime_error("Failed to parse MCP state file");
    }

    // Older or incomplete state may not yet contain an active-session pointer
    if(!state.contains("active_session")) {
        return std::nullopt;
    }

    // Convert the saved JSON value back into the logical session identifier
    const std::string sessionId = state.at("active_session").get<std::string>();

    // Validate persisted input before it is later used to resolve a session path
    validateSessionId(sessionId);

    return sessionId;
}


/**
 * setMcpActiveSessionId()
 * Makes the supplied logical session the session exposed through MCP.
 *
 * This function only changes the global MCP target. It does not modify the
 * session's own active_project value or any terminal context
 */
void setMcpActiveSessionId(const std::string& sessionId) {
    // Validate before using the session ID as persistent global state.
    validateSessionId(sessionId);

    // Make sure ~/.gptbridge exists before creating the global state file
    ensureStorageRoot();

    const std::filesystem::path statePath = getMcpStatePath();

    // MCP state contains only the currently selected logical session.
    const nlohmann::json state = {
        {"active_session", sessionId}
    };

    // Keep the file human-readable while replacing the committed state only
    // after the complete new JSON has been written successfully.
    writePrivateFileAtomically(
        statePath,
        state.dump(4) + '\n'
    );
}


/**
 * trySetMcpActiveSessionId()
 * Updates the global MCP target when possible, but converts failures into a
 * warning so project/session operations remain successful
 */
void trySetMcpActiveSessionId(const std::string& sessionId) noexcept {
    try {
        setMcpActiveSessionId(sessionId);
    }
    catch(const std::exception& error) {
        // MCP synchronication is secondary to the command that changed or
        // restored the session. Preserve that successful operation and tell
        // the user that MCP can be synchronized explicitly afterward
        std::cerr << "gptb warning: failed to update MCP active session: " << error.what() << '\n';
        std::cerr << "Run `gptb mcp sync` to retry.\n";
    }
}
