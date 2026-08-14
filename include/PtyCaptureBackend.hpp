#ifndef GPTB_PTY_CAPTURE_BACKEND_HPP
#define GPTB_PTY_CAPTURE_BACKEND_HPP

#include "CaptureBackend.hpp"
#include "CaptureCoordinator.hpp"
#include "ControlProtocolParser.hpp"

#include <array>
#include <filesystem>
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
        void updatePtyWindowSize(int masterFd);

        // Replaces the child process with the configured interactive shell
        [[noreturn]] void runChildShell(
                const std::string& sessionNonce,
                const std::filesystem::path& executablePath,
                int controlWriteFd
        );

        // Reads available input from the real terminal and forwards it to the child PTY.
        // Returns false when terminal input has reached EOF and the session should end
        bool forwardTerminalInput(int masterFd);

        // Reads one available chunk from the private control pipe and passes it to the
        // control-protocol parser. Returns false when the control pipe reaches EOF.
        bool forwardControlInput(int controlReadFd, ControlProtocolParser& parser);

        // Reads one available output chunk from the child PTY and passes it through the
        // control-protocol parser. Returns false when the PTY output stream has ended
        bool forwardPtyOutput(int masterFd, ControlProtocolParser& parser);

        // Waits for the child shell to terminate and collects its process status
        void waitForChild(pid_t childPid);

        // Creates the poll descriptor set for real-terminal input, child-PTY output,
        // and provate control events arriving from the child through the control pipe
        std::array<pollfd, 3> createPollDescriptors(int masterFd, int controlReadFd) const;

        // Waits until one of the monitored descriptors reports an event. Returns false
        // only when poll() was interrupted by a signal and should be retried
        bool waitForPollEvents(std::array<pollfd, 3>& descriptors) const;

        // Describes whether poll-reported descriptor conditions allow the session
        // to continue or recquire the forwarding loop to end
        enum class DescriptorCondition {
            Continue,
            EndSession
        };

        // Checks descriptor conditions and determines whether the session should continue
        DescriptorCondition checkDescriptorConditions(const std::array<pollfd, 3>& descriptors) const;

        // Dispatches a decoded control event to the appropriate capture-coordinator operation
        void handleControlEvent(const ControlEvent& event, CaptureCoordinator& captureCoordinator);
};


#endif
