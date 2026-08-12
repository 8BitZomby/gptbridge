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

    // Read the dimensions of the real terminal so the child PTY sharts with
    // the same number of rows and columns seen by the user
    winsize terminalSize{};

    // TIOCGWINSZ asks the terminal driver to copy the current window size
    // into terminalSize
    if(ioctl(STDIN_FILENO, TIOCGWINSZ, &terminalSize) == -1) {
        throw std::runtime_error("Failed to read terminal window size");
    }

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
        // SHELL normally contains the path to the user's configured shell,
        // for example "/bin/zsh" on macOS.
        const char* shell = std::getenv("SHELL");

        // Without a shell path, the child cannot start an interactive shell
        if(shell == nullptr) {
            _exit(1);
        }

        // execl() replaces this child process with the shell process.
        //
        // Arguments:
        //   shell   -> executable path
        //   shell   -> argv[0], conventionally the program name/path
        //   "-i"    -> request interactive shell mode
        //   nullptr -> marks the end of the argument list
        //
        // If execl() succeeds, execution never returns to this function
        execl(
                shell,
                shell,
                "-i",
                static_cast<char*>(nullptr)
        );

        // Reaching this line means execl() failed
        // _exit() terminates only the child process immediately
        _exit(1);
    }

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
    pollfd descriptors[2];

    // Descriptor 0 watches the parent's stdin. In an interactive
    // terminal this is where keyboard input becomes available to gptb
    descriptors[0].fd = STDIN_FILENO;

    // POLLIN means "tell us when this descriptor has data available to read"
    descriptors[0].events = POLLIN;

    // revents is filled in by poll() to report which events actually occurred.
    // Initialize it to zero before the first poll
    descriptors[0].revents = 0;

    // Descriptor 1 watches the PTY master. Data becomes available here when
    // zsh or one of its child programs writes output to the PTY slave
    descriptors[1].fd = masterFd;
    descriptors[1].events = POLLIN;
    descriptors[1].revents = 0;

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
            // Clear the flag now that this resize notification is being handled
            windowSizeChanged = 0;

            // Read the real terminal's new number of rows and columns
            // TIOCGWINSZ - Gets the current size
            winsize newTerminalSize{};
            if(ioctl(STDIN_FILENO, TIOCGWINSZ, &newTerminalSize) == -1) {
                throw std::runtime_error("Failed to read updated terminal window size");
            }
            // Apply the new dimensions to the PTY used by the child shell
            // TIOCSWINSZ - sets the current size
            if(ioctl(masterFd, TIOCSWINSZ, &newTerminalSize) == -1) {
                throw std::runtime_error("Failed to update PTY window size");
            }
        }
        // poll() blocks the parent until at least one watched descriptor has
        // and event we asked for, or until an error occurs
        //   If you press a key, STDIN_FILNO may become readable
        //   If zsh prints something, masterFd may become readble
        // Poll returns a positive number telling us how many descriptors have events pending.
        // Then we inspect each descriptor's revents field to find out which one woke up.
        //   pollResult == -1 -> error
        //   pollResult == 1  -> one watch descriptor has an event
        //   pollResult == 2  -> both watched descriptors have events
        const int pollResult = poll(
                descriptors,    // Array of descriptors we want the kernel to monitor
                2,              // Number of entries in the array
                -1              // -1 means wait indefinitely until an event occurs
        );

        // A negative result means poll() itself failed
        if(pollResult == -1) {
            // EINTR means a signal interrupted poll() before an event was reported.
            // Nothing is wrong with the descriptors, so restart the polling loop.
            if(errno == EINTR) {
                continue;
            }

            // Any other error means the parent can no longer reliably monitor the
            // terminal descriptors, so the PTY session cannot safely continue
            throw std::runtime_error("Failed while polling terminal descriptors");
        }

        // revents contains the events that actually occurred on each descriptor.
        // Check stdin first to see whether the real terminal has input available
        if(descriptors[0].revents & POLLIN) {
            // Keyboard or other terminal input is ready to be read from stdin

            // Temporary buffer used to receive bytes from the real terminal
            char buffer[4096];

            // Read whatever input is currently available from standard input
            const ssize_t bytesRead = read(
                    STDIN_FILENO,   // Read from the real terminal's standard input
                    buffer,         // Store the received bytes in this buffer
                    sizeof(buffer)  // Do not read more bytes than the buffer can hold
            );

            // A negative result means the read operation itself failed
            if(bytesRead == -1) {
                // EINTR means a signal interrupted the read before input was returned.
                // Restart the outer polling loop and wait for terminal activity again.
                if(errno == EINTR) {
                    continue;
                }
                // Any other error means terminal input can no longer be read reliably
                throw std::runtime_error("Failed to read terminal input");
            }

            // A result of zero means the real terminal input has reached EOF.
            // Close the PTY master so the child shell loses its terminal connection
            // and can terminate before the parent later collects it with waitpid()
            if(bytesRead == 0) {
                close(masterFd);
                masterFd = -1;
                sessionRunning = false;
                continue;
            }

            // Forward exactly the bytes we received into the PTY master
            //
            // Data written to the master becomes input on the PTY slave,
            // which is where the child shell is attached
            writeAll(
                    masterFd,
                    buffer,
                    static_cast<std::size_t>(bytesRead)
            );
        }
        // Check the PTY master to see whether the child shell produced output
        if(descriptors[1].revents & POLLIN) {
            // Shell/program output is ready to be read from the PTY master

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
                // EINTR means a signal interrupted the read before output was returned.
                // Restart the outer polling loop and wait for terminal activity again
                if(errno == EINTR) {
                    continue;
                }
                // Some PTY implementation report EIO when the slave side has closed.
                // In that case the child can no longer produce terminal output. Treat
                // it as the normal end of the PTY forwarding session
                if(errno == EIO) {
                    // EIO can indicate that the PTY slave has closed. The master is no
                    // longer useful, so release the descriptor before ending the session
                    close(masterFd);
                    masterFd = -1;
                    sessionRunning = false;
                    continue;
                }
                // Otherwise, all other errors means PTY output can no longer read reliably
                throw std::runtime_error("Failed to read PTY output");
            }

            // A zero-byte read means the PTY has reached EOF. The child side is no
            // longer producing terminal output, so the forwarding loop can finish
            if(bytesRead == 0) {
                // EOF means the child side of the PTY has closed. Release the master
                // descriptor before leaving the forwarding loop
                close(masterFd);
                masterFd = -1;
                sessionRunning = false;
                continue;
            }

            // Forward the bytes from the PTY to the real terminal's standard output
            //
            // This is what makes output from the child shell visible in the terminal
            // where gptb itself is running
            writeAll(
                    STDOUT_FILENO,
                    buffer,
                    static_cast<std::size_t>(bytesRead)
            );
        }

        // -- Invalid descriptpors checks -- //
        // POLLNVAL on stdin means the real terminal input descriptor is invalid
        if(descriptors[0].revents & POLLNVAL) {
            throw std::runtime_error("Terminal input file descriptor is invalid");
        }
        // POLLNVAL means the file descriptor stored for the PTY master is no longer
        // valid, so it cannot be used for further terminal communication
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
            close(masterFd);
            masterFd = -1;
            sessionRunning = false;
            continue;
        }
        // POLLHUP means the process connected to the PTY slave has closed its side
        // of the terminal. No future terminal traffic can arrive from that session
        // POLLHUP -> child closed its PTY side -> session ends normally
        if(descriptors[1].revents & POLLHUP) {
            close(masterFd);
            masterFd = -1;
            sessionRunning = false;
        }
    }

    // The live forwarding phase is now over. waitpid() collects the child
    // shell's termination status and prevents it from remaining a zombie
    int childStatus = 0;

    // Wait specifically for the child originally created by forkpty()
    //
    //   childPid: identifies which child the parent wants to collect
    //
    //   &childStatus: allows waitpid() to write encoded termination information here
    //
    //   0: wait normally until that child reaches a waitable terminated state

    // waitpid() waits specifically for the child created by forkpyt().
    // A return value of -1 means the wait operation itself failed
    const pid_t waitResult = waitpid(
            childPid,       // PID of the specific child process we are waiting for
            &childStatus,   // Receives the child's termination status
            0               // Block until the child changes to a waitable state
    );

    // -1 means waitpid() itself failed
    if(waitResult == -1) {
        throw std::runtime_error("Failed to wait for child shell");
    }
}
