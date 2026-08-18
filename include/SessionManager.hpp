#ifndef GPTB_SESSION_MANAGER_HPP
#define GPTB_SESSION_MANAGER_HPP

#include <filesystem>
#include <string>
#include <vector>


/**
 * SessionInfo
 * Basic information about one saved per-terminal session
 */
struct SessionInfo {
    std::string id;
    std::string activeProject;
};


/**
 * validateSessionId()
 * Rejects session identifiers that are unsafe to use as filesystem path components
 */
void validateSessionId(const std::string& sessionId);


/**
 * listSessions()
 * Returns information about all saved per-terminal sessions
 */
std::vector<SessionInfo> listSessions();


/**
 * getCurrentSessionId()
 * Returns an identifier for the terminal session that invoked gptb
 */
std::string getCurrentSessionId();


/**
 * getActiveProject()
 * Returns the active project for the current terminal.
 * An empty string means this session has no active project yet.
 */
std::string getActiveProject();


/**
 * getCurrentSessionDirectory()
 * Returns the directory used to store state for the current session
 */
std::filesystem::path getCurrentSessionDirectory();


/**
 * getCurrentSessionPath()
 * Returns the JSON file used to store state for the current session
 */
std::filesystem::path getCurrentSessionPath();


/**
 * setActiveProject()
 * Saves the active project name for the current terminal session
 */
void setActiveProject(const std::string& projectName);


/**
 * getActiveProjectForSession()
 * Returns the active project stored for a specific per-terminal session
 */
std::string getActiveProjectForSession(const std::string& sessionId);


#endif
