#ifndef GPTB_CONTROL_PROTOCOL_PARSER_HPP
#define GPTB_CONTROL_PROTOCOL_PARSER_HPP

#include "ControlProtocol.hpp"

#include <functional>
#include <string>
#include <string_view>


/**
 * ControlProtocolParser
 * Parses the PTY byte stream, separating ordinary terminal output from
 * gptbridge control frames. Valid control frames are decoded into ControlEvent
 * objects, while non-control bytes are passed through unchanged.
 */
class ControlProtocolParser {
    public:
        /**
         * OutputHandler
         * Receives ordinary terminal bytes that are not part of a valid
         * gptbridge control frame.
         */
        using OutputHandler = std::function<void(std::string_view)>;

        /**
         * EventHandler
         * Receives one decoded gptbridge control event
         */
        using EventHandler = std::function<void(const ControlEvent&)>;

        /**
         * ControlProtocolParser()
         * Creates a parser for one capture session using the nonce/
         * session token that legitimate control frames must contain.
         */
        ControlProtocolParser(std::string sessionNonce, OutputHandler outputHandler, EventHandler eventHandler);

        /**
         * consume()
         * Processes the next chunk of PTY bytes. The parser preserves any
         * incomplete control-frame data internally until more bytes arrive.
         */
        void consume(std::string_view bytes);

    private:
        // None/session token required for control frames in this capture
        std::string sessionNonce;
        // Receives ordinary terminal bytes
        OutputHandler outputHandler;
        // Receives successfully decoded control events
        EventHandler eventHandler;
        // Holds bytes that cannot yet be classified because a control frame
        // may be split across multiple PTY reads
        std::string pendingBytes;
};


#endif
