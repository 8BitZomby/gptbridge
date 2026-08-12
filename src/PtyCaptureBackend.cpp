#include "PtyCaptureBackend.hpp"

#include <cerrno>
#include <cstdlib>
#include <poll.h>
#include <stdexcept>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>


// SIGWINCH is delivered when the real terminal window changes size.
// The signal handler only records that a resize occurred; the normal
// PTY forwarding loop performs the actual terminal-size update safely
volatile sig_atomic_t windowSizeChanged = 0;


/**
 * handleWindowSizeChange()
 * Records that the real terminal was resized so the PTY session can update
 * the child terminal dimensions during normal program execution
 */
void handleWindowSizeChange(int) {
    // sig_atomic_t can be written atomically by a signal handler.
    // Keep the handler minimal because most normal C/C++ operations are not
    // guaranteed to be safe while a signal handler is executing.
    windowSizeChanged = 1;
}


/**
 * WindowResizeSignalGuard
 * Uses RAII to install gptbirdge's SIGWINCH handler for the lifetime of a
 * PTY session and restores the process's previous signal action afterwards
 */
class WindowResizeSignalGuard {
    public:
        // Saves the existing SIGWINCH action and installs the gptb terminal-resize handler
        WindowResizeSignalGuard() {
            // Describe the signal action gptb wants to install
            struct sigaction resizeAction{};
            // Call handleWindowSizeChange() whenever SIGWINCH is delivered
            resizeAction.sa_handler = handleWindowSizeChange;
            // Do not additionally block unrelated signals while the handler runs
            sigemptyset(&resizeAction.sa_mask);
            // Install the new action while saving the previous SIGWINCH action
            // so it can be restored when this guiard is destroyed
            if(sigaction(SIGWINCH, &resizeAction, &previousAction) == -1) {
                throw std::runtime_error("Failed to install terminal resize handler");
            }
        }

        // Destructor - Restores the SIGWINCH action that existed before this guard
        ~WindowResizeSignalGuard() {
            // Cleanup destructors must not throw. Restore the previous action
            // directly and allow process teardown to continue if restoration fails
            sigaction(SIGWINCH, &previousAction, nullptr);
        }
    private:
        // Signal action that was installed before gptbridge replaced it
        struct sigaction previousAction{};
};



/**
 * TerminalModeGuard
 * Uses RAII to temporarily place the real terminal in raw mode. The constructor
 * saves and applied terminal settings, while the destructor automatically
 * restores the original settins when the guard leaves scope.
 */
class TerminalModeGuard {
    public:
        // Saves the current terminal settins and enables raw terminal mode
        TerminalModeGuard() {
            // Read and save the terminal settings currently applied to stdin.
            // These exact settings are restored when this object is destroyed
            if(tcgetattr(STDIN_FILENO, &originalSettings) == -1) {
                throw std::runtime_error("Failed to read terminal attributes");
            }

            // Start with a copy of settings so raw mode modifies current
            // settings rather than starting with nothing
            termios rawSettings = originalSettings;

            // Modify the copied settings for raw byte-oriented terminal input.
            // This disables features such as canonical line processing,
            // character echoing, and terminal interpretation of control keys.
            cfmakeraw(&rawSettings);

            // Apply the raw settings to the real terminal connected to stdin
            if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &rawSettings) == -1) {
                throw std::runtime_error("Failed to enable raw terminal mode");
            }
            // tcgetattr - only reads setting
            // cfmakeraw - only modifies a copy in memory
            // tcsetattr - applies that copy to terminal device
        }

        // Destructor - Restores the terminal settings that were active before raw mode
        ~TerminalModeGuard() {
            // Destructors used for cleanup must not throw. If resoration
            // fails, there is no safe exception to propagate while another
            // exception may already be unwinding the stack
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalSettings);
        }
    private:
        // Original terminal config saved before raw mode
        termios originalSettings;
};


/**
 * FileDescriptorGuard
 * Owns one POSIX file descriptor and closes it automatically when the guard
 * goes out of scope. This prevents descriptor leaks on normal returns and exceptions
 */
class FileDescriptorGuard {
    public:
        // Takes ownership of the supplied descriptor
        // Explicit prevents an int from being implicitly
        // converted into a FileDescriptorGuard
        explicit FileDescriptorGuard(int fd) : fd(fd) {}

        // Close the owned descriptor when the guard is destroyed
        ~FileDescriptorGuard() {
            if(fd != -1) {
                close(fd);
            }
        }

        // Return the descriptor so it can be passed to POSIX functions
        int get() const {
            return fd;
        }

        // Close the descriptor early and mark the guard as empty so the
        // destructor does not attempt to close the same descriptor again
        void closeNow() {
            if(fd != -1) {
                close(fd);
                fd = -1;
            }
        }
    private:
        // -1 represents that this guard currently owns no valid desctiptor
        int fd;
};


/**
 * writeAll() - run() helper
 * Writes an entire byte buffer to a file descriptor, retrying until every
 * bytes has been transferred or an unrecoverable write error occurs
 */
void writeAll(int fd, const char* buffer, std::size_t byteCount) {
    // Track how many bytes from the original buffer have already been written
    std::size_t totalWritten = 0;

    // write() may transfer fewer bytes than requested, so continue until the
    // complete byte range has been sent to the destination descriptor
    while(totalWritten < byteCount) {
        const ssize_t bytesWritten = write(
                fd,                         // Destination file descriptor
                buffer + totalWritten,      // Start at the first byte not yet written
                byteCount - totalWritten    // Number of bytes still remaining
        );
        if(bytesWritten == -1) {
            // A signal can interrupt write() before it completes. EINTR means
            // no permanent I/O failure occurred, so try the write again
            if(errno == EINTR) {
                continue;
            }
            // Any other write error means the transfer cannot safely continue
            throw std::runtime_error("Failed to write terminal data");
        }

        // A zero-byte write would make no progress and cause this loop
        // to run forever, so treat as failed transfer
        if(bytesWritten == 0) {
            throw std::runtime_error("Terminal write made no progress");
        }

        // A successful write reports how many bytes were actually transferred
        totalWritten += static_cast<std::size_t>(bytesWritten);
    }
}


/**
 * getTerminalSize()
 * Reads and returns the current row and column dimension of the real terminal
 */
winsize PtyCaptureBackend::getTerminalSize() const {
    // Value-initialize the structure before asking the terminal driver to fill it
    winsize terminalSize{};

    // TIOCGWINSZ copies the current terminal dimensions into terminalSize
    if(ioctl(STDIN_FILENO, TIOCGWINSZ, &terminalSize) == -1) {
        throw std::runtime_error("Failed to read terminal window size");
    }
    return terminalSize;
}


/**
 * updatePtyWindowSize
 * Reads the real terminal's current dimensions and applies them to
 * the pseudo-terminal used by the child shell
 */
void PtyCaptureBackend::updatePtyWindowSize(int masterFd) {
    // Reuse the same terminal-size lookup used during initial PTY creation
    winsize terminalSize = getTerminalSize();

    // TIOCSWINSZ applies the new row and column values to the child PTY
    if(ioctl(masterFd, TIOCSWINSZ, &terminalSize) == -1) {
        throw std::runtime_error("Failed to update PTY window size");
    }
}


/**
 * runChildShell()
 * Replaces the forked child process with the user's configured interactive
 * shell. This function never returns to caller:
 *   - if execl() succeeds, the process image is replaced
 *   - if setup or execl() fails, we call _exit()
 */
[[noreturn]] void PtyCaptureBackend::runChildShell() {
    // SHELL normall contains the path to the user's configured shell,
    // for example "/bin/zsh" on macOS
    const char* shell = std::getenv("SHELL");

    // Without a shell path, the child cannot start an interactive shell
    if(shell == nullptr) {
        _exit(1);
    }

    // Replace the child process with the configured shell in interactive mode
    execl(
        shell,                          // Executable path
        shell,                          // argv[0], conventionally the program path/name
        "-i",                           // Start the shell interactively
        static_cast<char*>(nullptr)     // Marks the end of the argument list
    );

    // Reaching this point means execl() failed. _exit() terminates only the
    // child process without running parent-side cleanup code
    _exit(1);
}


/**
 * forwardTerminalInput()
 * Reads one available chunk from the real terminal and forwards those bytes
 * to the child PTY. Returns false when stdin reaches EOF
 */
bool PtyCaptureBackend::forwardTerminalInput(int masterFd) {
    // Temporary buffer used to receive bytes from the real terminal
    char buffer[4096];

    // Read whatever input is currently available from standard input
    const ssize_t bytesRead = read(
        STDIN_FILENO,                       // Read from the real terminal
        buffer,                             // Store received bytes here
        sizeof(buffer)                      // Do not read beyond the buffer
    );

    // A negative result means the read operation failed
    if(bytesRead == -1) {
        // EINTR means a signal interrupted the read, so the session can continue
        if(errno == EINTR) {
            return true;
        }

        // Any other error means terminal input can no longer be read reliably
        throw std::runtime_error("Failed to read terminal input");
    }

    // Zero means stdin reached EOF, so there is no more input to forward
    if(bytesRead == 0) {
        return false;
    }

    // Forward exactly the bytes read from the real terminal into the PTY master
    writeAll(
        masterFd,
        buffer,
        static_cast<std::size_t>(bytesRead)
    );

    return true;
}


/**
 * forwardPtyOutput()
 * Reads one available chunk from the child PTY and forwards those bytes to
 * the real terminal. Returns false when the PTY output stream has ended
 */
bool PtyCaptureBackend::forwardPtyOutput(int masterFd) {
    // Temporary buffer used to receive output bytes from the child PTY
    char buffer[4096];

    // Read whatever output is currently available from the PTY master
    //
    // Anything written by zsh or one of its child programs to the PTY slave
    // becomes readble by the parent through masterFd
    const ssize_t bytesRead = read(
            masterFd,       // Read from the parent side of the pseudo-terminal
            buffer,         // Store the received terminal bytes in this buffer
            sizeof(buffer)  // Read at most one buffer-sized chunk at a time
    );

    // A negative result means the read operation itself failed
    if(bytesRead == -1) {

        // EINTR means a signal interrupted the read, so the session can continue
        if(errno == EINTR) {
            return true;
        }

        // EIO can indicate that the PTY slave has closed, which is a normal
        // end-of-session condition for the forwarding loop
        if(errno == EIO) {
            return false;
        }

        // Any other error means PTY output can no longer be read reliably
        throw std::runtime_error("Failed to read PTY output");
    }

    // Zero means the PTY output stream has reached EOF
    if(bytesRead == 0) {
        return false;
    }

    // Forward the bytes produced by the child PTY to the real terminal
    //
    // This is what makes output from the child shell visible in the terminal
    // where gptb itself is running
    writeAll(
            STDOUT_FILENO,                      // Destination: parent side of the child PTY
            buffer,                             // Bytes just read from the real terminal
            static_cast<std::size_t>(bytesRead) // Number of valid bytes currently stored in buffer
    );

    return true;
}


/**
 * waitForChild()
 * Waits for the shell process created by forkpty() to terminate and collects
 * its status so it does not remain as a zombie process
 */
void PtyCaptureBackend::waitForChild(pid_t childPid) {

    // Receives the encoded termination status reported by waitpid()
    int childStatus = 0;

    // waitpid() waits specifically for the child created by forkpyt().
    const pid_t waitResult = waitpid(
            childPid,       // PID of the child shell to collect
            &childStatus,   // Receives the child's encoded termination status
            0               // Block until the child reaches a waitable state
    );

    // -1 means waitpid() itself failed
    if(waitResult == -1) {
        throw std::runtime_error("Failed to wait for child shell");
    }
}


/**
 * createPollDescriptors()
 * Builds the two descriptors monitored during the forwarding loop:
 * real-terminal input and output arriving from the child PTY
 */
std::array<pollfd, 2> PtyCaptureBackend::createPollDescriptors(int masterFd) const {

    std::array<pollfd, 2> descriptors{};

    // Descriptor 0 watches keyboard/input arriving from the real terminal
    descriptors[0].fd = STDIN_FILENO;
    descriptors[0].events = POLLIN; // POLLIN tells us when descriptor has data available to read
    descriptors[0].revents = 0;     // poll fills revents to report which events occured

    // Descriptor 1 watches output produced by the child through the PTY master
    descriptors[1].fd = masterFd;
    descriptors[1].events = POLLIN;
    descriptors[1].revents = 0;

    return descriptors;
}


/**
 * waitForPollEvents()
 * Blocks until poll() reports activity on one or more monitored descriptors.
 *
 * poll() waits on every descriptor in the supplied array and returns:
 *   -1  ->  error
 *    1  ->  one monitored descriptor has an event
 *    2  ->  both monitored descriptors have events
 *
 *  When poll() suceeds, each descriptor's revents field identifies which
 *  events occured. If poll() is interrupted by a signal (EINTR), this helper
 *  returns false so the caller can safely retry the forwarding loop
 */
bool PtyCaptureBackend::waitForPollEvents(std::array<pollfd, 2>& descriptors) const {
    // Wait indefinitely for activity on either terminal input or PTY output
    const int pollResult = poll(
            descriptors.data(), // Pointer to first pollfd entry
            descriptors.size(), // Number of descriptors being monitored
            -1                  // Block indefinitely until an event occurs
    );

    // A negative result means poll() itself failed
    if(pollResult == -1) {
        // EINTR means a signal interrupted poll() before an event was reported.
        // Nothing is wrong with the descriptors, so restart the polling loop.
        if(errno == EINTR) {
            return false;
        }

        // Any other poll error means reliable descriptor monitoring cannot continue
        throw std::runtime_error("Failed while polling terminal descriptors");
    }

    // Successful poll() leaves the actual events in each descriptor's revents field
    return true;
}


/**
 * checkDescriptorConditions()
 * Examines descriptor conditions reported by poll(). Invalid descriptors and
 * I/O errors are treated as failures, while normal hangups signal that the
 * forwarding session should end
 */
PtyCaptureBackend::DescriptorCondition PtyCaptureBackend::checkDescriptorConditions(const std::array<pollfd, 2>& descriptors) const {

    // -- Invalid Descriptor Checks -- //
    // POLLNVAL means the real-terminal input descriptor itself is invalid
    if(descriptors[0].revents & POLLNVAL) {
        throw std::runtime_error("Terminal input file descriptor is invalid");
    }
    // POLLNVAL on the PTY master means the descriptor is invalid
    // POLLNVAL -> our fd itself is invalid -> error
    if(descriptors[1].revents & POLLNVAL) {
        throw std::runtime_error("PTY master file descriptor is invalid");
    }

    // -- Error checks -- //
    // POLLERR on stdin means the kernel reported an error condition while
    // monitoring input from the real terminal
    if(descriptors[0].revents & POLLERR) {
        throw std::runtime_error("Terminal input reported an error");
    }
    // POLLERR means the kernel detected an error condition on the PTY master.
    // The descriptor can no longer be assumed to support reliable forwarding.
    // POLLERR -> kernel reports an I/O condition -> error
    if(descriptors[1].revents & POLLERR) {
        throw std::runtime_error("PTY master reported an error");
    }

    // -- Hangup checks -- //
    // POLLHUP on stdin means the real terminal input side has been closed.
    // No further user input can arrive, so the forwarding sesion should end
    if(descriptors[0].revents & POLLHUP) {
        return DescriptorCondition::EndSession;
    }
    // POLLHUP means the process connected to the PTY slave has closed its side
    // of the terminal. No future terminal traffic can arrive from that session
    // POLLHUP -> child closed its PTY side -> session ends normally
    if(descriptors[1].revents & POLLHUP) {
        return DescriptorCondition::EndSession;
    }

    return DescriptorCondition::Continue;
}


/**
 * run()
 * Manages the real terminal's mode for the lifetime of a captured
 * session and delegates the PTY-specific work to runSession()
 */
void PtyCaptureBackend::run() {
    // The guard saves the real terminal settins and enables raw mode.
    // Its destructor restores the original settins automatically when
    // run() returns or if runSession() throws an exception.
    TerminalModeGuard terminaMode;

    // Run the PTY-specific shell and terminal-forwarding logic
    // while the TerminalModeGuard remains alive
    runSession();

}


/**
 * runSession()
 * Creates a pseudo-terminal, launches the interactive child shell,
 * and forwards terminal traffic until the captured session ends
 */
void PtyCaptureBackend::runSession() {
    // forkpty() writes the PTY master file descriptor into this variable
    // for the parent process. -1 is used as an invalid value.
    int masterFd = -1;

    // Match the child PTY's initial dimensions to the real terminal
    winsize terminalSize = getTerminalSize();

    // forkpty() performs both PTY creation and process creation
    //
    // After this call:
    //   childPid == -1 -> creation failed
    //   childPid == 0  -> this code is running in the child
    //   childPid > 0   -> this code is running in the parent
    //
    // The child is automatically attached to the PTY slave as its terminal,
    // while the parent receives the PTY master through masterFd
    const pid_t childPid = forkpty(
            &masterFd,      // Receives the PTY master file descriptor
            nullptr,        // We do not need the PTY device name
            nullptr,        // Use the default terminal attributes for now
            &terminalSize   // Start the child PTY at the real terminal's size
    );

    // forkpty() returns -1 when it cannot create the PTY or child process
    if(childPid == -1) {
        throw std::runtime_error("Filed to create pseudo-terminal");
    }

    // A return value of 0 means this is the created child process
    if(childPid == 0) {
        runChildShell();
    }

    // The parent owns the PTY master descriptor from this point onward.
    // The guard closes it automatically on every return or exception path
    FileDescriptorGuard masterDescriptor(masterFd);

    // Reaching this point means we are in the parent process
    //
    // masterFd is connected to the child shell's terminal. Later, the parent
    // will read shell output from masterFd and write keyboard input to it.
    //
    // childPid identifies the shell process so the parent can later monitor
    // when that shell exits


    // poll() will monitor two input sources for the parent:
    //
    //   1. STDIN_FILENO -> keyboard/input arriving from the real terminal
    //   2. masterFd     -> output produced by the shell through the PTY
    //
    // pollfd stores both the file descriptor to watch and the kinds of
    // events the parent is interested in receiving from that descriptor

    // Prepare the real-terminal and PTY descriptors monitored by poll()
    std::array<pollfd, 2> descriptors = createPollDescriptors(masterDescriptor.get());

    // Keep gptb's SIGWINCH handler installed for the lifetime of the PTY
    // session. The guard restores the process's previous signal action
    // when runSession() returns or throws
    WindowResizeSignalGuard resizeSignalGuard;

    // Session Running Loop
    bool sessionRunning = true;
    while(sessionRunning) {

        // The SIGWINCH handler sets windowSizeChanged when the real terminal is
        // resized. Perform the actual resize here in normal program execution
        if(windowSizeChanged) {

            // Clear the signal flag before handling the resize in normal execution
            windowSizeChanged = 0;

            // Synchronize the child PTY with the real terminal's latest dimensions
            updatePtyWindowSize(masterDescriptor.get());
        }

        // Wait for terminal or PTY activity. A signal interruption restarts the
        // forwarding loop so resize and other signa;-driven state can be handled
        if(!waitForPollEvents(descriptors)) {
            continue;
        }

        // revents contains the events that actually occurred on each descriptor.
        // Check stdin first to see whether the real terminal has input available
        if(descriptors[0].revents & POLLIN) {
            // Keyboard or other terminal input is ready to be read from stdin
            // Forward available real-terminal input to the child PTY
            if(!forwardTerminalInput(masterDescriptor.get())) {
                masterDescriptor.closeNow();
                sessionRunning = false;
                continue;
            }
        }

        // Check the PTY master to see whether the child shell produced output
        if(descriptors[1].revents & POLLIN) {
            // Shell/program output is ready to be read from the PTY master
            // Forward available child-PTY output to the real terminal
            if(!forwardPtyOutput(masterDescriptor.get())) {
                masterDescriptor.closeNow();
                sessionRunning = false;
                continue;
            }
        }

        // Check descriptor error and hangup conditions reported by poll()
        if(checkDescriptorConditions(descriptors) == DescriptorCondition::EndSession) {
            // A normal terminal or PTY hangup end the forwarding session
            masterDescriptor.closeNow();
            sessionRunning = false;
            continue;
        }
    }

    // The live forwarding phase is now over. waitpid() collects the child
    // shell's termination status and prevents it from remaining a zombie
    // The forwarding session is complete. Collect the child shell process
    waitForChild(childPid);
}
