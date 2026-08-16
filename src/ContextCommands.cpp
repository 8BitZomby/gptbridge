#include "ContextCommands.hpp"
#include "TerminalContext.hpp"
#include "TemporaryInteractionHistory.hpp"

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>


/**
 * handlePushCommand()
 * Pushes one or more recent terminal interactions from the temporary
 * session history into the persistent terminal context
 */
int handlePushCommand(int argc, char* argv[]) {
    // "gptb push" pushes the most recent terminal interaction
    // "gptb push <count>" pushes the specified number of recent interactions
    if(argc != 2 && argc != 3) {
        std::cout << "Usage: gptb push [count]\n";
        return 1;
    }

    // Push must run inside a gptbridge-managed terminal session so it can
    // locate the temporary history belonging to the active session
    const char* captureId = std::getenv("GPTB_SESSION_NONCE");

    if(captureId == nullptr || *captureId == '\0') {
        std::cout << "gptb push requires an active gptbridge terminal session";
        return 1;
    }

    // With no count supplied, push only the most recent interaction
    std::size_t pushCount = 1;

    if(argc == 3) {
        try {
            // Require the entire argument to contain a positive integer
            std::size_t parsedLength = 0;
            const std::string countText = argv[2];
            pushCount = std::stoull(
                countText,
                &parsedLength
            );

            if(parsedLength != countText.size() || pushCount == 0) {
                std::cout << "Push count must be a positive integer\n";
                return 1;
            }
        }
        catch(const std::exception&) {
            std::cout << "Push count must be a positive integer\n";
            return 1;
        }
    }

    // Load every completed interaction from this live terminal session
    TemporaryInteractionHistory tempHistory(captureId);
    const std::vector<TerminalInteraction> history = tempHistory.loadAll();

    // Nothing can be pushed before at least one command has completed
    if(history.empty()) {
        std::cout << "No terminal history available to push\n";
        return 1;
    }

    // Reject requests for more interactions than currently exist so the
    // user's requested range is never silently changed
    if(pushCount > history.size()) {
        std::cout << "Only " << history.size() << " terminal interaction";
        if(history.size() != 1) {
            std::cout << "s";
        }
        std::cout << " available\n";
        return 1;
    }

    // Copy the requested number of interactions from the end of the
    // temporary history while preserving their original execution order
    const auto firstInteraction = history.end() - static_cast<std::ptrdiff_t>(pushCount);
    const std::vector<TerminalInteraction> selectedInteractions(
        firstInteraction,
        history.end()
    );

    // Persist the selected terminal I/O as ChatGPT terminal context
    // Append/replace policy will be configurable separately
    TerminalContext terminalContext;
    terminalContext.append(selectedInteractions);

    std::cout << "Pushed " << pushCount << " terminal interaction";

    if(pushCount != 1) {
        std::cout << "s";
    }
    std::cout << '\n';

    return 0;
}


/**
 * handleShowCommand()
 * Displays all terminal interactions currently stored as persistent context
 */
int handleShowCommand(int argc, char* argv[]) {
    // "gptb show" displays all terminal I/O currently selected as context
    if(argc != 2) {
        std::cout << "Usage: gptb show\n";
        return 1;
    }

    // Load the persistent terminal context for the current gptbridge session
    TerminalContext terminalContext;
    const std::vector<TerminalInteraction> interactions = terminalContext.loadAll();

    // An empty context means notheing has been pushed yet
    if(interactions.empty()) {
        std::cout << "No terminal context\n";
        return 0;
    }

    // Display each stored interaction in the order it was pushed
    for(std::size_t idx = 0; idx < interactions.size(); ++idx) {
        const TerminalInteraction& interaction = interactions[idx];

        // Number interactions from 1 so the same number can later be used
        // by remove/select commands
        std::cout << "[" << (idx + 1) << "] " << interaction.command << '\n';

        // Show the captured output directly beneath its command
        if(!interaction.output.empty()) {
            std::cout << interaction.output;
        }

        // Keep consecutive interactions visually separated
        std::cout << '\n';
    }

    return 0;
}


/**
 * handleClearCommand()
 * Clears terminal context. The final CLI behaviour will be implemented when
 * context-management semantics are completed
 */
int handleClearCommand(int argc, char* argv[]) {
    // Preserve the current unimplemented behavious during this refactor
    std::cout << "Command recognized but not implemented yet\n";
    return 0;
}


/**
 * handleRemoveCommand()
 * Removes selected terminal context. The final selection behavior will be
 * implemented after push/context indexing is finalized.
 */
int handleRemoveCommand(int argc, char* argv[]) {
    // Preserve the current unimplemented behavior during this refactor.
    std::cout << "Command recognized but not implemented yet\n";
    return 0;
}
