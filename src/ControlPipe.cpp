#include "ControlPipe.hpp"

#include <stdexcept>
#include <unistd.h>


/**
 * ControlPipe()
 * Creates one anonymous POSIX pipe. The OS returns two file descriptors:
 * one for reading and one for writing.
 */
ControlPipe::ControlPipe() {
    // pipe() writes the newly created descriptors into this two-element array:
    //      descriptors[0] -> read end
    //      descriptors[1] -> write end
    int descriptors[2];

    if(pipe(descriptors) == -1) {
        throw std::runtime_error("Failed to create control pipe");
    }

    // Take ownership only after pipe() has successfully created both ends
    readFd = descriptors[0];
    writeFd = descriptors[1];
}


/**
 * ~ControlPipe()
 * Releases any pipe ends still owned by this object. An end may already have
 * been closed explicitly after fork, in which case its value will be -1
 */
ControlPipe::~ControlPipe() {
    // Close read
    if(readFd != -1) {
        close(readFd);
    }

    // Close write
    if(writeFd != -1) {
        close(writeFd);
    }
}


/**
 * getReadFd()
 * Returns the descriptor used to read bytes from the control pipe
 */
int ControlPipe::getReadFd() const {
    return readFd;
}


/**
 * getWriteFd()
 * Returns the descriptor used to write bytes into the control pipe
 */
int ControlPipe::getWriteFd() const {
    return writeFd;
}


/**
 * closeReadEnd()
 * Releases ownership of the pipe's read end before normal object desctruction
 */
void ControlPipe::closeReadEnd() {
    if(readFd != -1) {
        close(readFd);
        readFd = -1;
    }
}


/**
 * closeWriteEnd()
 * Releases ownership of the pipe's write end before normal object desctruction
 */
void ControlPipe::closeWriteEnd() {
    if(writeFd != -1) {
        close(writeFd);
        writeFd = -1;
    }
}
