#ifndef GPTB_SESSION_MANAGER_HPP
#define GPTB_SESSION_MANAGER_HPP

#include "PersistentSessionStorage.hpp"

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
 * generateSessionId()
 * Generates a new stable identifier for a logical gptbridge session
 */
std::string generateSessionId();


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
 * Returns the active project stored in the supplied persistent session.
 * An empty string means the session has no active project.
 */
std::string getActiveProject(const PersistentSessionStorage& sessionStorage);


/**
 * getActiveProjectForCurrentSession()
 * Returns the active project stored for the current logical gptbridge session
 */
std::string getActiveProjectForCurrentSession();


/**
 * setActiveProject()
 * Saves the active project in the supplied persistent session.
 */
void setActiveProject(const PersistentSessionStorage& sessionStorage, const std::string& projectName);


/**
 * setActiveProjectForCurrentSession()
 * Saves the active project for the current logical gptbridge session
 */
void setActiveProjectForCurrentSession(const std::string& projectName);


#endif
