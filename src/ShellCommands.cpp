#include "ShellCommands.hpp"
#include "PtyCaptureBackend.hpp"
#include "SessionManager.hpp"
#include "ShellIntegration.hpp"
#include "ShellIntegrationEncoder.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>


namespace {

    /**
     * Handles the shell event that marks command execution/output start.
     */
    int handleOscStartedEvent(int argc, char* argv[]) {
        // The event carries the exact command text and working directory.
        if(argc != 5) {
            std::cerr << "Usage: gptb shell-event osc-started "
                      << "<command> <working-directory>\n";
            return 1;
        }

        // Private GPTB metadata must belong to the active capture session.
        const char* sessionNonce = std::getenv("GPTB_SESSION_NONCE");

        if(sessionNonce == nullptr || *sessionNonce == '\0') {
            std::cerr << "gptb: GPTB_SESSION_NONCE is not set\n";
            return 1;
        }

        const std::string command = argv[3];
        const std::filesystem::path workingDirectory = argv[4];

        // OSC 7 communicates the directory in which the command will execute.
        const std::string workingDirectorySequence =
            ShellIntegrationEncoder::encodeWorkingDirectory(workingDirectory);

        // The private GPTB OSC carries the exact shell command text.
        const std::string exactCommandSequence =
            ShellIntegrationEncoder::encodeExactCommand(
                command,
                sessionNonce
            );

        // OSC 133;C is the authoritative transition into command output.
        const std::string outputStartSequence =
            ShellIntegrationEncoder::encodeCommandOutputStart();

        // Keep metadata ordered before the command-output boundary.
        std::cout << workingDirectorySequence
                  << exactCommandSequence
                  << outputStartSequence;

        std::cout.flush();

        return 0;
    }


    /**
     * Handles the private boundary where shell presentation bytes begin.
     */
    int handleOscPresentationStartEvent(int argc, char* argv[]) {
        if(argc != 3) {
            std::cerr << "Usage: gptb shell-event osc-presentation-start\n";
            return 1;
        }

        // Presentation markers are private and must carry the active session nonce.
        const char* sessionNonce = std::getenv("GPTB_SESSION_NONCE");

        if(sessionNonce == nullptr || *sessionNonce == '\0') {
            std::cerr << "gptb: GPTB_SESSION_NONCE is not set\n";
            return 1;
        }

        // Presentation bytes remain visible but are not stored as command output.
        const std::string presentationSequence =
            ShellIntegrationEncoder::encodeShellPresentationStart(sessionNonce);

        std::cout << presentationSequence;
        std::cout.flush();

        return 0;
    }


    /**
     * Handles the shell event that marks command completion.
     */
    int handleOscFinishedEvent(int argc, char* argv[]) {
        // OSC 133;D carries the completed command's exit code.
        if(argc != 4) {
            std::cerr << "Usage: gptb shell-event osc-finished <exit-code>\n";
            return 1;
        }

        int exitCode = 0;

        try {
            // Require the entire argument to be a valid integer.
            std::size_t parsedLength = 0;
            const std::string exitCodeText = argv[3];

            exitCode = std::stoi(exitCodeText, &parsedLength);

            if(parsedLength != exitCodeText.size()) {
                std::cerr << "gptb: invalid shell-event exit code\n";
                return 1;
            }
        }
        catch(const std::exception&) {
            std::cerr << "gptb: invalid shell-event exit code\n";
            return 1;
        }

        // OSC 133;D is the authoritative command-finished boundary.
        const std::string finishedSequence =
            ShellIntegrationEncoder::encodeCommandFinished(exitCode);

        std::cout << finishedSequence;
        std::cout.flush();

        return 0;
    }

}


/**
 * runManagedShell()
 * Launches the PTY-backed managed shell attached to the supplied logical session
 */
int runManagedShell(const std::string& sessionId) {
    // Session identity is chosen by the caller so starting a new session and
    // restoring an existing session can use different lifecycle features
    PtyCaptureBackend backend(sessionId);

    // The backend owns the managed PTY session until the user exits the shell
    backend.run();

    return 0;
}


/**
 * handleCaptureCommand()
 * Launches the temporary PTY-backed capture shell.
 */
int handleCaptureCommand(int argc, char* argv[]) {
    if(argc != 2) {
        std::cout << "Usage: gptb capture\n";
        return 1;
    }

    return runManagedShell(getCurrentSessionId());
}


/**
 * handleShellInitCommand()
 * Prints the initialization script for the requested shell.
 */
int handleShellInitCommand(int argc, char* argv[]) {
    if(argc != 3) {
        std::cout << "Usage: gptb shell-init <shell>\n";
        return 1;
    }

    // The caller evaluates this output in the user's normal shell.
    std::cout << generateShellInit(argv[2]);

    return 0;
}


/**
 * handleShellEventCommand()
 * Routes internal shell lifecycle events to their specific handlers.
 */
int handleShellEventCommand(int argc, char* argv[]) {
    if(argc < 3) {
        std::cerr << "Usage: gptb shell-event "
                  << "<osc-started|osc-presentation-start|osc-finished> ...\n";
        return 1;
    }

    const std::string eventType = argv[2];

    if(eventType == "osc-started") {
        return handleOscStartedEvent(argc, argv);
    }

    if(eventType == "osc-presentation-start") {
        return handleOscPresentationStartEvent(argc, argv);
    }

    if(eventType == "osc-finished") {
        return handleOscFinishedEvent(argc, argv);
    }

    std::cerr << "Unknown shell event type: " << eventType << '\n';
    return 1;
}
