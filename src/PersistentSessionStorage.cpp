#include "PersistentSessionStorage.hpp"

#include "Config.hpp"
#include "SessionManager.hpp"
#include "Storage.hpp"

#include <utility>


/**
 * Constructor: PersistentSessionStorage()
 * Constructs a resolved persistent session storage object without creating
 * or modifying anything on disk
 */
PersistentSessionStorage::PersistentSessionStorage(
    std::filesystem::path sessionDirectory) : sessionDirectory_(std::move(sessionDirectory)) {}


/**
 * forCurrentSession()
 * Resolves persistent storage for the logical session associated with
 * the current CLI process
 */
PersistentSessionStorage PersistentSessionStorage::forCurrentSession() {
    // Global mode deliberately has no terminal-specific identity. Avoid
    // resolving a TTY because one is neither required nor meaningful here
    if(getSessionMode() == SessionMode::Global) {
        return PersistentSessionStorage(getStorageRoot() / "global-session");
    }

    // Per-terminal mode uses the logical gptbridge session ID. Inside a managed
    // shell this may come from GPTB_SESSION_ID, otherwise it comes from the TTY
    const std::string sessionId = getCurrentSessionId();

    return PersistentSessionStorage(getStorageRoot() / "sessions" / sessionId);
}


/**
 * forExplicitSessionId()
 * Resolves persistent storage for a caller that supplies the logical session
 * identity explicitly
 */
PersistentSessionStorage PersistentSessionStorage::forExplicitSessionId(const std::string& sessionId) {
    // Reject malformed logical session IDs before using one in a storage path
    validateSessionId(sessionId);

    // An explicit logical session always owns its own persistent directory,
    // independent of the legacy global/per-terminal session mode
    return PersistentSessionStorage(getStorageRoot() / "sessions" / sessionId);
}


/**
 * getSessionDirectory()
 * Returns the resolved directory containing persistent files for this session
 */
const std::filesystem::path& PersistentSessionStorage::getSessionDirectory() const {
    return sessionDirectory_;
}


/**
 * getSessionStatePath()
 * Returns the state.json file belonging to this persistent session
 */
std::filesystem::path PersistentSessionStorage::getSessionStatePath() const {
    return sessionDirectory_ / "state.json";
}


/**
 * getTerminalContextPath()
 * Returns the terminal-context.jsonl file belonging to this persistent session
 */
std::filesystem::path PersistentSessionStorage::getTerminalContextPath() const {
    return sessionDirectory_ / "terminal-context.jsonl";
}


/**
 * ensureSessionDirectoryExists()
 * Creates and hardens the directories required to store persistent session data
 */
void PersistentSessionStorage::ensureSessionDirectoryExists() const {
    // Protect the parent that contains the resolved session directory
    ensurePrivateDirectory(sessionDirectory_.parent_path());

    // The resolved session directory itself must always remain owner-only
    ensurePrivateDirectory(sessionDirectory_);
}
