#ifndef GPTB_TERMINAL_CONTEXT_HPP
#define GPTB_TERMINAL_CONTEXT_HPP

#include "PersistentSessionStorage.hpp"
#include "TerminalInteraction.hpp"

#include <vector>


/**
 * TerminalContext
 * Stores terminal interactions explicitly selected for ChatGPT context.
 * Unlike temporary capture history, this data persists beyond the live shell.
 *
 * Each TerminalContext is bound to one PersistentSessionStorage so all
 * operations use the same Global or PerTerminal session resolution.
 */
class TerminalContext {
    public:

        /**
         * TerminalContext()
         * Binds terminal context operations to the supplied persistent session
         */
        explicit TerminalContext(PersistentSessionStorage sessionStorage);

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

    private:

        // Persistent session whose terminal context file this object manages
        PersistentSessionStorage sessionStorage_;
};


#endif
