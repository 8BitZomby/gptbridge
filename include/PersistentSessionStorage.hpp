#ifndef GPTB_PERSISTENT_SESSION_STORAGE_HPP
#define GPTB_PERSISTENT_SESSION_STORAGE_HPP

#include <filesystem>
#include <string>


/**
 * PersistentSessionStorage
 * Represents the persistent storage location belonging to one logical
 * gptbridge session.
 *
 * Each logical session owns its own directory under sessions/<id>. This class
 * centalizes construction of the persistent paths used by that session.
 *
 * TemporaryInteractionHistory remains separate because live capture history is
 * identified by a capture nonce rather than the persistent logical session ID.
 */
class PersistentSessionStorage {
    public:
        /**
         * forCurrentSession()
         * Resolves persistent storage for the logical session attached to the
         * current managed shell
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
