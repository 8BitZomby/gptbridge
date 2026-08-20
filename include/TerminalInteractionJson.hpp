#ifndef GPTB_TERMINAL_INTERACTION_JSON_HPP
#define GPTB_TERMINAL_INTERACTION_JSON_HPP

#include "TerminalInteraction.hpp"

#include <nlohmann/json.hpp>


/**
 * terminalInteractionToJson()
 * Converts one TerminalInteraction into the JSON representation used by
 * gptbridge's terminal history and terminal-context storage
 */
nlohmann::json terminalInteractionToJson(const TerminalInteraction& interaction);


/**
 * terminalInteractionFromJson()
 * Reconstructs one TerminalInteraction from a stored JSON record
 */
TerminalInteraction terminalInteractionFromJson(const nlohmann::json& record);


#endif
