#ifndef GPTB_PERSISTENT_SESSION_STORAGE_HPP
#define GPTB_PERSISTENT_SESSION_STORAGE_HPP

#include <filesystem>
#include <string>


/**
 * PersistentSessionStorage
 * Represents the persistent storage location belonging to one logical
 * gptbridge session.
 *
 * This class is the single authority for deciding whether persistent session
 * data lives in the shared global-session directory or in a per-terminal sessions/<id> directory
 *
 * TemporaryInteractionHistory is intentionally separate because live capture
 * history is identified by a capture nonce rather than persistent session mode
 */
class PersistentSessionStorage {
    public:
        /**
         * forCurrentSession()
         * Resolves persistent storage for the session associated with the
         * current CLI terminal, honoring the configured session mode
         */
        static PersistentSessionStorage forCurrentSession();

        /**
         * forExplicitSessionId()
         * Resolves persistent storage for a caller that supplies its session
         * identity explicitly, such as the MCP server
         */
        static PersistentSessionStorage forExplicitSessionId(const std::string& sessionId);

        /**
         * getSessionDirectory()
         * Returns the directory containing persistent data for this session
         */
        const std::filesystem::path& getSessionDirectory() const;

        /**
         * getSessionStatePath()
         * Returns the state.json path used to store session state
         */
        std::filesystem::path getSessionStatePath() const;

        /**
         * getTerminalContextPath()
         * Returns the terminal-context.jsonl path used to store pushed terminal
         * interactions for this session
         */
        std::filesystem::path getTerminalContextPath() const;

        /**
         * ensureSessionDirectoryExists()
         * Creates the persistent session directory and applies owner-only
         * permissions before session data is written
         */
        void ensureSessionDirectoryExists() const;

    private:

        /**
         * PersistentSessionStorage()
         * Constructs a resolved session-storage object without modifying the
         * filesystem
         */
        explicit PersistentSessionStorage(std::filesystem::path sessionDirectory);

        // Resolved directory that contains all persistent files for this session
        std::filesystem::path sessionDirectory_;
};


#endif
