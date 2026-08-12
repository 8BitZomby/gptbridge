#ifndef GPTB_PTY_CAPTURE_BACKEND_HPP
#define GPTB_PTY_CAPTURE_BACKEND_HPP

#include "CaptureBackend.hpp"

#include <array>
#include <poll.h>
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
        [[noreturn]] void runChildShell();

        // Reads available input from the real terminal and forwards it to the child PTY.
        // Returns false when terminal input has reached EOF and the session should end
        bool forwardTerminalInput(int masterFd);

        // Reads one available output chunk from the child PTY and forwards it to the
        // real terminal. Returns false when the PTY output stream has ended
        bool forwardPtyOutput(int masterFd);

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
};


#endif
