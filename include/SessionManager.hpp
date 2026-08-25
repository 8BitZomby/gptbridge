#ifndef GPTB_SESSION_MANAGER_HPP
#define GPTB_SESSION_MANAGER_HPP

#include "PersistentSessionStorage.hpp"

#include <string>
#include <vector>


/**
 * SessionInfo
 * Basic information about one saved logical gptbridge session
 */
struct SessionInfo {
    // Stable logical session identifier
    std::string id;

    // Project currently associated with this session, if any
    std::string activeProject;

    // True when at least one managed-shell attachment is currently live
    bool active = false;
};


/**
 * allocateSessionId()
 * Reserves and returns the next persistent logical gptbridge session ID
 */
std::string allocateSessionId();


/**
 * createSession()
 * Creates a new persistent logical gptbridge session and returns its ID
 */
std::string createSession();


/**
 * validateSessionId()
 * Rejects session identifiers that are unsafe to use as filesystem path components
 */
void validateSessionId(const std::string& sessionId);


/**
 * listSessions()
 * Returns information about all saved logical gptbridge sessions
 */
std::vector<SessionInfo> listSessions();


/**
 * getMostRecentlyUsedSessionId()
 * Returns the logical session ID with the newest last_used_at timestamp.
 * Returns an empty string when no saved session has a usage timestamp
 */
std::string getMostRecentlyUsedSessionId();


/**
 * getCurrentSessionId()
 * Returns the current logical session ID, falling back to a legacy
 * terminal-derived ID when no managed-session identity is available
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
 * markSessionUsed()
 * Records the current time as the session's most recent use
 */
void markSessionUsed(const PersistentSessionStorage& sessionStorage);


/**
 * setActiveProjectForCurrentSession()
 * Saves the active project for the current logical gptbridge session
 */
void setActiveProjectForCurrentSession(const std::string& projectName);


#endif
