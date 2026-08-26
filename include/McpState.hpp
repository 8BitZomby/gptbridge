#ifndef GPTB_MCP_STATE_HPP
#define GPTB_MCP_STATE_HPP

#include <optional>
#include <string>


/**
 * getMcpActiveSessionId()
 * Returns the logical session currently exposed through MCP.
 * std::nullopt means no MCP-active session has been selected yet.
 */
std::optional<std::string> getMcpActiveSessionId();


/**
 * setMcpActiveSessionId()
 * Makes the supplied logical session the session exposed through MCP.
 */
void setMcpActiveSessionId(const std::string& sessionId);


/**
 * trySetMcpActiveSessionId()
 * Attempts to update the MCP-active locical session without allowing an MCP
 * synchronization failure to fail the primary gptbridge command
 */
void trySetMcpActiveSessionId(const std::string& sessionId) noexcept;


#endif
