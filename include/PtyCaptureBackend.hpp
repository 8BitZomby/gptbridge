#ifndef GPTB_PTY_CAPTURE_BACKEND_HPP
#define GPTB_PTY_CAPTURE_BACKEND_HPP

#include "CaptureBackend.hpp"


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
};


#endif
