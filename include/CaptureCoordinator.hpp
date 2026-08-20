#ifndef GPTB_CAPTURE_COORDINATOR_HPP
#define GPTB_CAPTURE_COORDINATOR_HPP

#include "TemporaryInteractionHistory.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>


/**
 * CaptureCoordinator
 * Coordinates the lifetime of one shell command at a time. Command-start
 * metadata and terminal output are accumulated temporarily until a matching
 * command-finish event completes the interaction and stores it in history
 */
class CaptureCoordinator {
    public:
        /**
         * Constructor: CaptureCoordinator()
         * Creates a coordinator for one live capture and connects completed
         * interactions to that capture's temporary history
         */
        CaptureCoordinator(const std::string& captureId);

        /**
         * Destructor: ~CaptureCoordinator()
         * Removes temporary terminal history when this live capture ends
         */
        ~CaptureCoordinator();

        /**
         * commandStarted()
         * Begins a new command interaction using metadata reported by the
         * shell immediately before command execution
         */
        void commandStarted(
                const std::string& interactionId, const std::string& command, const std::filesystem::path&
                workingDirectory, const std::string& startedAt);

        /**
         * appendOutput()
         * Appends terminal bytes to the command currently being captured.
         * Output received while no command is active is intentionally ignored
         */
        void appendOutput(std::string_view output);

        /**
         * commandFinished()
         * Completes the active interaction using metadata reported after
         * command execution and persists the completed interaction to history
         */
        void commandFinished(const std::string& interactionId, int exitCode, const std::string& finishedAt);

        /**
         * hasActiveInteraction()
         * Returns whether a command has started but has not yet finished
         */
        bool hasActiveInteraction() const;

    private:
        /**
         * PendingInteraction
         * Holds the portion of a terminal interaction that exists while a
         * command is still running and therefore cannot yet be persisted as
         * a completed TerminalInteraction
         */
        struct PendingInteraction {
            // Used to link start and finish events
            std::string interactionId;
            std::string command;
            std::string output;
            std::filesystem::path workingDirectory;
            std::string startedAt;
        };

        // Contains the command currently being captured. An empty optional
        // means the shell is presently between command executions
        std::optional<PendingInteraction> pendingInteraction;

        // Stores every completed interaction for this live capture so terminal
        // I/O can later be selected with `gptb push`
        TemporaryInteractionHistory history;
};


#endif
