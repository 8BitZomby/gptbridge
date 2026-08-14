#ifndef GPTB_CONTROL_FRAME_ENCODER_HPP
#define GPTB_CONTROL_FRAME_ENCODER_HPP

#include "ControlProtocol.hpp"

#include <string>
#include <string_view>


/**
 * ControlFrameEncoder
 *
 * Encodes structured gptbridge control events into the framed byte format
 * consumed by ControlProtocolParser
 *
 * Keeping encoding separate from shell-specific code gives every supported
 * shell one canonical implementation of the control protocol
 */
class ControlFrameEncoder {
    public:
        // Serializes one control event and wraps it with the protocol metadata
        // required for transmission through the shared PTY byte stream
        static std::string encode(const ControlEvent& event, std::string_view sessionNonce);
};


#endif
