#ifndef GPTB_INTERACTION_HISTORY_HPP
#define GPTB_INTERACTION_HISTORY_HPP

#include "TerminalInteraction.hpp"

#include <vector>


/**
 * InteractionHistory
 *
 * Provides persistent access to terminal interactions recorded for the current gptbridge session.
 */
class InteractionHistory {
    public:
        /**
         * append()
         * Adds one completed terminal interaction to the current session history
         */
        void append(const TerminalInteraction& interaction);

        /**
         * loadAll()
         * Returns all recorded interactions for the current session in stored order
         */
        std::vector<TerminalInteraction> loadAll() const;
};


#endif
