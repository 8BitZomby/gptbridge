#ifndef GPTB_TERMINAL_CONTEXT_HPP
#define GPTB_TERMINAL_CONTEXT_HPP

#include "TerminalInteraction.hpp"

#include <vector>


/**
 * TerminalContext
 * Stores terminal interactions explicitly selected for ChatGPT context.
 * Unlike termporary capture history, this data persits beyond the live shell
 */
class TerminalContext {
    public:
        /**
         * append()
         * Adds selected terminal interactions while preserving existing context
         */
        void append(const std::vector<TerminalInteraction>& interactions);

        /**
         * replace()
         * Replaces the existing terminal context with the selected interactions
         */
        void replace(const std::vector<TerminalInteraction>& interactions);

        /**
         * loadAll()
         * Returns all terminal interactions currently stored as context
         */
        std::vector<TerminalInteraction> loadAll() const;

        /**
         * clear()
         * Removes all terminal I/O currently stored for ChatGPT context
         */
        void clear();
};


#endif
