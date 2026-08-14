#include "TimeUtils.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>


/**
 * currentTimestampUtc()
 * Returns the current system time as an ISO 8601 UTC timestamp.
 */
std::string currentTimestampUtc() {

    // Capture the current wall-clock time from the system clock
    const auto now = std::chrono::system_clock::now();

    // Convert the chrono time_point into time_t so the C time library can
    // break it into calendar fields such as year, month, day, hour, etc
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);

    // gmtime_r() converts the timestamp into UTC calendar fields.
    // The _r version writes into caller-owned storage instead of returning
    // a pointer to shared static storage
    std::tm utcTime{};

    if(gmtime_r(&currentTime, &utcTime) == nullptr) {
        throw std::runtime_error("Failed to convert current time to UTC");
    }

    // Format the UTC calendar fields as:
    //   &utcTime,
    //   YYYY-MM-DDTHH:MM:SSZ
    std::ostringstream timestamp;
    timestamp << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");

    return timestamp.str();
}
