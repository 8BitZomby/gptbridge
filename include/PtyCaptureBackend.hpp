#ifndef GPTB_PTY_CAPTURE_BACKEND_HPP
#define GPTB_PTY_CAPTURE_BACKEND_HPP

#include "CaptureBackend.hpp"
#include "CaptureCoordinator.hpp"
#include "ControlProtocolParser.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/types.h>


/**
 * PtyCaptureBackend
 * Captures an interactive terminal session by running the shell through a
 * pseudo-terminal and transparently forwarding terminal input and output.
 */
class PtyCaptureBackend : public CaptureBackend {

    public:

        // Manages the terminal state for a captured shell session and restores terminal setting when session ends
        void run() override;

    private:

        // Runs the interactive shell through pseudo-terminal and forwards traffic until session ends
        void runSession();

        // Returns the current dimensions of the real terminal
        winsize getTerminalSize() const;

        // Applies the real terminal's current dimensions to the child PTY
        void updatePtyWindowSize(int masterF);

        // Replaces the child process with the configured interactive shell
        [[noreturn]] void runChildShell(
                const std::string& sessionNonce, const std::string& sessionId, const std::filesystem::path& executablePath);

        // Reads available input from the real terminal and forwards it to the child PTY.
        // Returns false when terminal input has reached EOF and the session should end
        bool forwardTerminalInput(int masterFd);

        // Reads one available output chunk from the child PTY and passes it through the
        // control-protocol parser. Returns false when the PTY output stream has ended
        bool forwardPtyOutput(int masterFd, ControlProtocolParser& parser);

        // Waits for the child shell to terminate and collects its process status
        void waitForChild(pid_t childPid);

        // Creates the poll descriptor set for real-terminal input and child-PTY output
        std::array<pollfd, 2> createPollDescriptors(int masterFd) const;

        // Waits until one of the monitored descriptors reports an event. Returns false
        // only when poll() was interrupted by a signal and should be retried
        bool waitForPollEvents(std::array<pollfd, 2>& descriptors) const;

        // Describes whether poll-reported descriptor conditions allow the session
        // to continue or recquire the forwarding loop to end
        enum class DescriptorCondition {
            Continue,
            EndSession
        };

        // Checks descriptor conditions and determines whether the session should continue
        DescriptorCondition checkDescriptorConditions(const std::array<pollfd, 2>& descriptors) const;

        /**
         * OscCaptureState
         * Holds command metadata and lifecycle state accumulated from shell-integration
         * events for the current PTY session.
         *
         * OSC 7 and private GPTB command metadata are accumulated before OSC 133;C.
         * OSC 133;C begins the interaction, while a private GPTB presentation marker
         * can temporarily stop shell-generated presentation bytes from being persisted
         * before OSC 133;D completes the interaction.
         */
        struct OscCaptureState {
            // Working directory reported by the standard OSC 7 sequence
            std::optional<std::filesystem::path> workingDirectory;

            // Exact command text reported by the private GPTB OSC sequence
            std::optional<std::string> exactCommand;

            // Parent-generated ID for the currently active OSC interaction
            std::optional<std::string> activeInteractionId;

            // True after the private shell-presentation marker has been received.
            // Output remains visible in the terminal but is not stored in history
            bool suppressCapturedOutput = false;

            // Monotonically increasing value used to generate interaction IDs within this capture session
            std::uint64_t nextInteractionId = 1;
        };

        // Dispatches decoded lifecycle and metadata events to the capture state or the
        // appropriate CaptureCoordinator operation
        void handleControlEvent(const ControlEvent& event, CaptureCoordinator& captureCoordinator, OscCaptureState& oscCaptureState);
};


#endif
