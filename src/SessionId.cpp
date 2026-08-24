#include "SessionId.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>


/**
 * formatSessionId()
 * Converts a numeric session value into a fixed-width logical session ID
 */
std::string formatSessionId(std::uint64_t sessionNumber) {
    // Keep session numbers inside the supported 0001-9999 range
    if(sessionNumber == 0 || sessionNumber > MaxSessionNumber) {
        throw std::runtime_error("Invalid session number");
    }

    // Format the numeric value as four digits with leading zeros
    std::ostringstream stream;
    stream << "s-"
           << std::setw(4)
           << std::setfill('0')
           << sessionNumber;

    return stream.str();
}
