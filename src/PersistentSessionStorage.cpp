#include "PersistentSessionStorage.hpp"

#include "SessionManager.hpp"
#include "Storage.hpp"

#include <optional>
#include <stdexcept>
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
 * Resolves persistent storage for the logical session attached to the
 * current managed shell
 */
PersistentSessionStorage PersistentSessionStorage::forCurrentSession() {
    // Persistent "current session" storage only exists while this process is
    // running inside a managed logical gptbridge session
    const std::optional<std::string> sessionId = getCurrentSessionId();

    if(!sessionId.has_value()) {
        throw std::runtime_error("No logical gptbridge session is attached to the current shell");
    }

    return forExplicitSessionId(*sessionId);
}


/**
 * forExplicitSessionId()
 * Resolves persistent storage for a caller that supplies the logical session
 * identity explicitly
 */
PersistentSessionStorage PersistentSessionStorage::forExplicitSessionId(const std::string& sessionId) {
    // Reject malformed logical session IDs before using one in a storage path
    validateSessionId(sessionId);

    // Every logical session owns its own persistent directory under sessions/<id>
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
