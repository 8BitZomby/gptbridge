#ifndef GPTB_TIME_UTILS_HPP
#define GPTB_TIME_UTILS_HPP

#include <string>


/**
 * currentTimestampUtc()
 * Returns the current UTC time in ISO 8601 format
 * Using one shared timestamp function keeps command-start and command-finish
 * events consistent across every supported shell integration
 */
std::string currentTimestampUtc();


#endif
