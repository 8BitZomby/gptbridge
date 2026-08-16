#include "CaptureCoordinator.hpp"
#include "TerminalInteraction.hpp"

#include <stdexcept>
#include <string_view>


/**
 * Constructor: CaptureCoordinator()
 * Creates capture state for one live gptbridge session and connects it to
 * that capture's temporary terminal history
 */
CaptureCoordinator::CaptureCoordinator(const std::string& captureId) : history(captureId) {}


/**
 * Destructor: ~CaptureCoordinator()
 * Cleans up the temporary terminal history when the live capture ends
 */
CaptureCoordinator::~CaptureCoordinator() {
    try {
        // Temporary history only exists for the lifetime of this capture
        history.clearTmp();
    }
    catch(...) {
        // Cleanup failure must not escapt from a desctuctor
    }
}


/**
 * commandStarted()
 * Begins a new pending interaction using metadata reported immediately
 * before the shell executes a command
 */
void CaptureCoordinator::commandStarted(
    const std::string& interactionId, const std::string& command,
    const std::filesystem::path& workingDirectory, const std::string& startedAt) {

    // Receiving a new start event while another command is still active would
    // overwrite incomplete cature state and make command/output association
    // ambiguous, so reject that state transition
    if(pendingInteraction.has_value()) {
        throw std::runtime_error("Cannot start a command while another interaction is active");
    }

    // Store the new command directly as the interaction currently being
    // captured. output starts empty and will be filled by appendOutput()
    pendingInteraction = PendingInteraction{
        .interactionId = interactionId,
        .command = command,
        .output = "",
        .workingDirectory = workingDirectory,
        .startedAt = startedAt
    };
}


/** appendOutput()
 * Adds terminal output bytes to the interaction for the command currently being captured
 */
void CaptureCoordinator::appendOutput(std::string_view output) {
    // Terminal output can occur while the shell is between commands, such as
    // when displaying a prompt. That output does not belong to an interaction,
    // so only capture output while a command is active
    if(!pendingInteraction.has_value()) {
        return;
    }
    // Append exactly the output bytes associated with the active command
    pendingInteraction->output.append(output.data(), output.size());
}


/**
 * commandFinished()
 * Completes the active interaction using metadata reported after command
 * execution and stores the finished interaction in temporary session history
 */
void CaptureCoordinator::commandFinished(const std::string& interactionId, int exitCode, const std::string& finishedAt) {
    // A finsished event without matching active command would make the capture
    // state inconsistent, so reject it instead of creating a partial history
    if(!pendingInteraction.has_value()) {
        throw std::runtime_error("Cannot finish a command when no interaction is active");
    }

    // The finish event must belong to the command that is currently active
    if(interactionId != pendingInteraction->interactionId) {
        throw std::runtime_error("Control event interation ID does not match active interaction");
    }

    // Build the completed interaction from the metadata and output accumulated
    // while the command was runnning
    TerminalInteraction interaction{
        .command = pendingInteraction->command,
        .output = pendingInteraction->output,
        .exitCode = exitCode,
        .workingDirectory = pendingInteraction->workingDirectory,
        .startedAt = pendingInteraction->startedAt,
        .finishedAt = finishedAt
    };

    // Store only completed interactions in this live capture's temporary history
    history.append(interaction);
    // The command is now finished, so clear the active capture state
    pendingInteraction.reset();
}


/** hasActiveInteraction()
 * Returns whether a command has started but has not yet finished
 */
bool CaptureCoordinator::hasActiveInteraction() const {
    // The optional contains a PendingInteraction only while a command is active
    return pendingInteraction.has_value();
}
