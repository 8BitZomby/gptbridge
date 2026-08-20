#ifndef GPTB_TERMINAL_INTERACTION_HISTORY_HPP
#define GPTB_TERMINAL_INTERACTION_HISTORY_HPP

#include "TerminalInteraction.hpp"

#include <filesystem>
#include <string>
#include <vector>


/**
 * TemporaryInteractionHistory
 *
 * Stores the complete terminal interaction history for one live gptbridge
 * capture session. This history exists only while the session is active so
 * commands can later be selected with `gptb push`.
 */
class TemporaryInteractionHistory {
    public:
        /**
         * Contructor: TemporaryInteractionHistory()
         * Creates storage for onle live capture session
         */
        TemporaryInteractionHistory(const std::string& captureId);

        /**
         * append()
         * Adds one completed terminal interaction to the temporary history
         */
        void append(const TerminalInteraction& interaction);

        /**
         * loadAll()
         * Returns all interactions recorded for this live capture session
         */
        std::vector<TerminalInteraction> loadAll() const;

        /**
         * clear()
         * Removes the temporary history for this capture session
         */
        void clearTmp();

    private:
        // File containing the complete temporary interaction history for
        // this specific live capture session
        std::filesystem::path historyPath;
};


#endif
