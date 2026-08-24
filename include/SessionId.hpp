#ifndef GPTB_SESSION_ID_HPP
#define GPTB_SESSION_ID_HPP

#include <cstdint>
#include <string>


// Maximum supported logical session number
inline constexpr std::uint64_t MaxSessionNumber = 9999;


/**
 * formatSessionId()
 * Converts a numeric session value into its fixed-width logical session ID
 */
std::string formatSessionId(std::uint64_t sessionNumber);


#endif
