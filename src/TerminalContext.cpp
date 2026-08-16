#include "ContextPaths.hpp"
#include "SessionManager.hpp"
#include "TerminalContext.hpp"
#include "TerminalInteractionJson.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>


/**
 * append()
 * Adds selected terminal interactions to the existing terminal context.
 * JSON Lines allow new records to be added without rewriting the whole file
 */
void TerminalContext::append(const std::vector<TerminalInteraction>& interactions) {
    // Nothing needs to be written if no interactions were selected
    if(interactions.empty()) {
        return;
    }

    // Resolve the persistent terminal-context file for the current session
    const std::filesystem::path contextPath = getTerminalContextPath();

    // Open in append mode so existing pushed terminal context is preserved
    std::ofstream output(contextPath, std::ios::app);

    if(!output) {
        throw std::runtime_error("Failed to open terminal context for appending");
    }

    // Write each interaction as one independent JSON Lines record
    for(const TerminalInteraction& interaction : interactions) {
        output << terminalInteractionToJson(interaction).dump() << '\n';
    }
}


/**
 * replace()
 * Removes the previous terminal context and stores only the supplied interactions
 */
void TerminalContext::replace(const std::vector<TerminalInteraction>& interactions) {

    // Resolve the persistent terminal-context file for the current session
    const std::filesystem::path contextPath = getTerminalContextPath();

    // Normal ofstream mode truncates the existing file before writing
    std::ofstream output(contextPath);

    if(!output) {
        throw std::runtime_error("Failed to open terminal context for writing");
    }

    // Write the replacement context as one JSON object per line
    for(const TerminalInteraction& interaction : interactions) {
        output << terminalInteractionToJson(interaction).dump() << '\n';
    }
}


/**
 * loadAll()
 * Returns all terminal interactions currently stores as persistent context
 */
std::vector<TerminalInteraction> TerminalContext::loadAll() const {

    // Resolve the context file without duplicating path logic here
    const std::filesystem::path contextPath = getTerminalContextPath();

    // A missing context file means no terminal interactions have been pushed yet
    if(!std::filesystem::exists(contextPath)) {
        return {};
    }

    // Open the JSON Lines context file for reading
    std::ifstream input(contextPath);

    if(!input) {
        throw std::runtime_error("Failed to open terminal context for reading");
    }

    std::vector<TerminalInteraction> interactions;
    std::string line;

    // Each non-empty line represents one complete TerminalInteraction
    while(std::getline(input, line)) {
        if(line.empty()) {
            continue;
        }

        try {
            // Parse one JSON record and rebuild the typed interaction
            const nlohmann::json record = nlohmann::json::parse(line);
            interactions.push_back(terminalInteractionFromJson(record));
        }
        catch(const nlohmann::json::parse_error&) {
            throw std::runtime_error("Failed to parse terminal context");
        }
    }

    return interactions;
}


/**
 * clear()
 * Removes all terminal I/O currently stored for ChatGPT context
 */
void TerminalContext::clear() {

    // Resolve the persistent terminal-context file for this session
    const std::filesystem::path contextPath = getTerminalContextPath();

    // Delete the persistent context file if this session currently has one
    if(std::filesystem::exists(contextPath)) {
        std::filesystem::remove(contextPath);
    }
}
