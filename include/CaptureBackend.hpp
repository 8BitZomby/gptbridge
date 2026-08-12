#ifndef GPTB_CAPTURE_BACKEND_HPP
#define GPTB_CAPTURE_BACKEND_HPP


/**
 * CaptureBackend
 * Defines the interface for a terminal capture mechanism that runs an
 * interactive shell shile preserving normal terminal input and output.
 */
class CaptureBackend {
    public:
        // Destructor
        virtual ~CaptureBackend() = default;

        // Starts the capture backend and remains active until session ends
        virtual void run() = 0;
};


#endif
