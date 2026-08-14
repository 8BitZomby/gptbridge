#ifndef GPTB_CONTROL_PIPE_HPP
#define GPTB_CONTROL_PIPE_HPP


/**
 * ControlPipe
 *
 * Owns the two file descriptors of the private one-way channel used to send
 * shell lifecycle control frames from the captured child shell to the parent
 * gptbridge process.
 *
 * Before fork:
 *      readFd  ->  parent will eventually own this end
 *      writeFd ->  child will eventually own this end
 *
 * After fork, each process closes the end it does not use
 */
class ControlPipe {
    public:
        // Creates a new operating-system pipe and takes ownership of both ends
        ControlPipe();

        // Closes any pipe descriptors that are still owned by this object
        ~ControlPipe();

        // A pipe represents uniquely owned OS resources and must not be copied
        ControlPipe(const ControlPipe&) = delete;
        ControlPipe& operator=(const ControlPipe&) = delete;

        // Returns the descriptor from which the parent reads control frames
        int getReadFd() const;

        // Returns the descriptor to which the child writes control frames
        int getWriteFd() const;

        // Closes the read end early and marks it as no longer owned
        void closeReadEnd();

        // Closes the write end early and marks it as no longer owned
        void closeWriteEnd();

    private:
        // -1 means this object currently owns no valid descriptor for that end
        int readFd = -1;
        int writeFd = -1;
};


#endif
