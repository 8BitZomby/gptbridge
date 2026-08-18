#include "ContextCommands.hpp"
#include "TerminalContext.hpp"
#include "TemporaryInteractionHistory.hpp"
#include "TerminalSecretDetector.hpp"

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>


namespace {
    /**
     * parsePositiveCount()
     * Parses a CLI count argument and requires a positive integer
     */
    bool parsePositiveCount(const std::string& text, std::size_t& value) {
        try {
            // Tracks how much of the argument stoull() successfully parsed
            std::size_t parsedLength = 0;
            value = std::stoull(text, &parsedLength);

            // Reject partial values such as "2abc" and counts of zero
            return parsedLength == text.size() && value > 0;
        }
        catch(const std::exception&) {
            // Non-numeric or out-of-range values are invalid counts
            return false;
        }
    }


    /**
     * selectRecentInteractions()
     * Selects the newest interactions while preserving their execution order
     */
    std::vector<TerminalInteraction> selectRecentInteractions(const std::vector<TerminalInteraction>& history, std::size_t count) {
        // Move backwards from the end to find the first requested interaction
        const auto firstInteraction = history.end() - static_cast<std::ptrdiff_t>(count);

        // Copy the selected tail of the history into a new vector
        return std::vector<TerminalInteraction>(
            firstInteraction,
            history.end()
        );
    }


    /**
     * printInteraction()
     * Prints one stored terminal interaction with its display number
     */
    void printInteraction(const TerminalInteraction& interaction, std::size_t displayIndex) {
        // Show the interaction number and original command
        std::cout << "[" << displayIndex << "] " << interaction.command << '\n';

        // Print captured output directly below the command when present
        if(!interaction.output.empty()) {
            std::cout << interaction.output;
        }

        // Separate this interaction from the next one
        std::cout << '\n';
    }
}

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
        std::cout << "gptb push requires an active gptbridge terminal session\n";
        return 1;
    }

    // With no count supplied, push only the most recent interaction
    std::size_t pushCount = 1;

    // Validate the optional count before reading the temporary history
    if(argc == 3) {
        if(!parsePositiveCount(argv[2], pushCount)) {
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

    // Select the requested number of recent interactions
    const std::vector<TerminalInteraction> selectedInteractions = selectRecentInteractions(history, pushCount);

    // Scan only the interactions selected for this push so unrelated terminal
    // history cannot block or warn about content the user did not choose.
    const std::vector<SecretFinding> secretFindings =
        detectTerminalSecrets(selectedInteractions);

    // Require explicit confirmation before persisting terminal content that
    // contains possible secrets or credentials.
    if(!secretFindings.empty()) {
        std::cout << "Warning: possible sensitive terminal content detected:\n";

        for(const SecretFinding& finding : secretFindings) {
            std::cout << "  - "
                      << finding.location
                      << ": "
                      << finding.reason
                      << '\n';
        }

        // Default to cancellation so pressing Enter cannot accidentally share
        // terminal content that was flagged as potentially sensitive.
        std::cout << "Push anyway? [y/N]: ";

        std::string response;
        std::getline(std::cin, response);

        // Only an explicit y/Y allows the push to continue.
        if(response != "y" && response != "Y") {
            std::cout << "Terminal context was not pushed\n";
            return 1;
        }
    }

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

    // An empty context means nothing has been pushed yet
    if(interactions.empty()) {
        std::cout << "No terminal context\n";
        return 0;
    }

    // Display each stored interaction in the order it was pushed
    for(std::size_t idx = 0; idx < interactions.size(); ++idx) {
        // Display numbers start at 1 so they can later be used by remove/select commands
        printInteraction(interactions[idx], idx + 1);
    }

    return 0;
}


/**
 * handleClearCommand()
 * Clears all persistent terminal context stored for the current session
 */
int handleClearCommand(int argc, char* argv[]) {
    // "gptb clear" removes all pushed terminal context
    // "gptb clear <count> removes only the newest pushed interactions"
    if(argc != 2 && argc != 3) {
        std::cout << "Usage: gptb clear [count]\n";
        return 1;
    }

    TerminalContext terminalContext;

    // With no could supplied, remove all persistent pushed context
    if(argc == 2) {
        terminalContext.clear();
        std::cout << "Cleared terminal context\n";
        return 0;
    }

    std::size_t clearCount = 0;

    // Reject zero, non-numeric, partial, or out-of-range count arguments
    if(!parsePositiveCount(argv[2], clearCount)) {
        std::cout << "Clear count must be a positive integer\n";
        return 1;
    }

    // Load the currently pushed interactions before removing the newest ones
    std::vector<TerminalInteraction> interactions = terminalContext.loadAll();

    if(interactions.empty()) {
        std::cout << "No terminal context to clear\n";
        return 1;
    }

    // Do not silently remove fewer interactions than the user requested
    if(clearCount > interactions.size()) {
        std::cout << "Only " << interactions.size() << " terminal interaction";

        if(interactions.size() != 1) {
            std::cout << "s";
        }
        std::cout << " currently pushed\n";
        return 1;
    }

    // Removing every stored interaction is equivalent to clearing the file
    if(clearCount == interactions.size()) {
        terminalContext.clear();
    }
    else {
        // Remove the requested number of newest interactions while preserving
        // the original execution order of everything that remains
        interactions.resize(interactions.size() - clearCount);
        terminalContext.replace(interactions);
    }

    std::cout << "Cleared " << clearCount << " terminal interaction";
    if(clearCount != 1) {
        std::cout << "s";
    }
    std::cout << '\n';

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
