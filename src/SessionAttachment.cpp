#include "SessionAttachment.hpp"

#include "SessionManager.hpp"
#include "Storage.hpp"

#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <system_error>
#include <unistd.h>


namespace {
    /**
     * getSessionAttachmentsDirectory()
     * Returns the runtime attachment directory belonging to one logical session
     */
    std::filesystem::path getSessionAttachmentsDirectory(const std::string& sessionId) {
        // Validate the session ID before using it as a filesystem path component
        validateSessionId(sessionId);
        return getStorageRoot() / "sessions" / sessionId / "attachments";
    }

    /**
     * readSessionAttachmentInfo()
     * Reads and validates the runtime metadata stored for one session attachment
     */
    SessionAttachmentInfo readSessionAttachmentInfo(const std::filesystem::path& attachmentPath) {
        std::ifstream input(attachmentPath);

        if(!input) {
            throw std::runtime_error("Failed to open session attachment record");
        }

        nlohmann::json record;

        try {
            input >> record;
        }
        catch(const nlohmann::json::parse_error&) {
            throw std::runtime_error("Failed to parse session attachment record: " + attachmentPath.string());
        }

        // Every attachment record must contain the fields needed to identify and
        // manage the parent gptb process and its child shell
        if(!record.contains("id") ||
            !record.contains("parent_pid") ||
            !record.contains("child_pid") ||
            !record.contains("started_at")) {

            throw std::runtime_error("Incomplete session attachment record: " + attachmentPath.string());
        }

        SessionAttachmentInfo attachmentInfo;
        attachmentInfo.id = record.at("id").get<std::string>();
        attachmentInfo.parentPid = record.at("parent_pid").get<pid_t>();
        attachmentInfo.childPid = record.at("child_pid").get<pid_t>();
        attachmentInfo.startedAt = record.at("started_at").get<std::string>();

        // PIDs must identify actual processes. Zero and negative values have special
        // meaning to kill(), so never allow them to come from a stored record
        if(attachmentInfo.parentPid <= 0 || attachmentInfo.childPid <= 0) {
            throw std::runtime_error("Invalid process ID in session attachment record: " + attachmentPath.string());
        }

        return attachmentInfo;
    }
}


/**
 * isSessionAttachmentLive()
 * Checks whether another process still owns the attachment record's
 * exclusive liveness lock
 */
bool isSessionAttachmentLive(const std::filesystem::path& attachmentPath) {
    // Open the existing runtime record without creating or truncating it
    const int descriptor = ::open(
        attachmentPath.c_str(),
        O_RDWR
    );

    if(descriptor == -1) {
        // A normally exiting attachment can remove its record between directory
        // enumeration and this open(). In that case there is no live attachment
        // left to inspect
        if(errno == ENOENT) {
            return false;
        }

        throw std::runtime_error("Failed to open session attachment record");
    }

    // If another process still owns the exclusive lock, flock() fails with
    // EWOULDBLOCK/EAGAIN and the attachment is live
    if(::flock(descriptor, LOCK_EX | LOCK_NB) == -1) {
        const int lockError = errno;
        ::close(descriptor);

        if(lockError == EWOULDBLOCK || lockError == EAGAIN) {
            return true;
        }

        throw std::runtime_error("Failed to check session attachment lock");
    }

    // Successfully taking the lock means no live owner remains. Release it
    // immediately; the caller may now treat this record as stale
    ::flock(descriptor, LOCK_UN);
    ::close(descriptor);

    return false;
}


/**
 * hasLiveSessionAttachments()
 * Checks whether any attachment belonging to the logical session still has a
 * live process holding its liveness lock
 */
bool hasLiveSessionAttachments(const std::string &sessionId) {
    // Resolve the runtime attachment directory for this logical session
    const std::filesystem::path attachmentsDirectory = getSessionAttachmentsDirectory(sessionId);

    // A session with no attachment directory has no live managed shells
    if(!std::filesystem::exists(attachmentsDirectory)) {
        return false;
    }

    // Inspect each runtime attachment record and stop as soon as one live
    // attachment is found
    for(const auto& entry : std::filesystem::directory_iterator(attachmentsDirectory)) {
        // Ignore unrelated files or directories that are not attachment records
        if(!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        if(isSessionAttachmentLive(entry.path())) {
            return true;
        }
    }
    return false;
}


/**
 * removeStaleSessionAttachments()
 * Removes attachment records whose owning process is no longer live
 */
void removeStaleSessionAttachments(const std::string& sessionId) {
    // Resolve the runtime attachment directory for this logical session
    const std::filesystem::path attachmentsDirectory = getSessionAttachmentsDirectory(sessionId);

    // A session with no attachment directory has nothing to clean up
    if(!std::filesystem::exists(attachmentsDirectory)) {
        return;
    }

    // Inspect each attachment record and remove only those whose liveness
    // lock is no longer held by another process
    for(const auto& entry : std::filesystem::directory_iterator(attachmentsDirectory)) {
        if(!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        if(isSessionAttachmentLive(entry.path())) {
            continue;
        }

        // Stale runtime records are best-effort cleanup. Failure to remove one
        // should not prevent the rest of the session state from being inspected
        std::error_code cleanupError;
        std::filesystem::remove(entry.path(), cleanupError);
    }
}


/**
 * closeSessionAttachments()
 * Requests termination of every live managed-shell attachment belonging to
 * the logical session
 */
std::size_t closeSessionAttachments(const std::string &sessionId) {
    // Resolve the runtime attachment directory for this logical session
    const std::filesystem::path attachmentsDirectory = getSessionAttachmentsDirectory(sessionId);

    if(!std::filesystem::exists(attachmentsDirectory)) {
        return 0;
    }

    std::size_t closedAttachmentCount = 0;

    for(const auto& entry : std::filesystem::directory_iterator(attachmentsDirectory)) {
        // Only JSON files in this directory represent attachment records
        if(!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        // An unlocked record belongs to a process that has already exited
        // Leave its removal to the dedicated stale-record cleanup operation
        if(!isSessionAttachmentLive(entry.path())) {
            continue;
        }

        const SessionAttachmentInfo attachmentInfo = readSessionAttachmentInfo(entry.path());

        // Send SIGHUP to the child shell to end its PTY session. The parent gptb
        // process can then observe the PTY ending, reap the child, and unwind normally
        if(::kill(attachmentInfo.childPid, SIGHUP) == -1) {
            // ESRCH means the shell disappeared after the liveness check. That
            // race does not make the close operation itself an error
            if(errno == ESRCH) {
                continue;
            }
            throw std::runtime_error("Failed to terminate session attachment: " + attachmentInfo.id);
        }

        ++closedAttachmentCount;
    }

    return closedAttachmentCount;
}


/**
 * SessionAttachmentRegistration()
 * Creates and locks the persistent runtime record for one attachment
 */
SessionAttachmentRegistration::SessionAttachmentRegistration(
        const std::string& sessionId, 
        const SessionAttachmentInfo& attachmentInfo) {

    // Resolve and create the runtime attachment directory for this logical session
    const std::filesystem::path attachmentsDirectory = getSessionAttachmentsDirectory(sessionId);
    ensurePrivateDirectory(attachmentsDirectory);

    // The attachment ID becomes the filename for this runtime record.
    // Reuse session-ID validation because the same single-component path
    // safety rules apply here
    validateSessionId(attachmentInfo.id);

    attachmentPath_ = attachmentsDirectory / (attachmentInfo.id + ".json");

    // Create the attachment record with owner-only permissions
    lockDescriptor_ = ::open(
        attachmentPath_.c_str(),
        O_RDWR | O_CREAT | O_EXCL,
        S_IRUSR | S_IWUSR
    );

    // open() may fail because the record cannot be created or because an
    // attachment with the same ID already exists. Do not touch the path on failure,
    // because an existing file may belong to another live attachment
    if(lockDescriptor_ == -1) {
        throw std::runtime_error("Failed to create session attachment record");
    }

    // Hold an exclusive lock for the lifetime of this registration object.
    // Other gptb processes use lock availability to distinguish live records
    // from stale files left behind after a crash
    if(::flock(lockDescriptor_, LOCK_EX | LOCK_NB) == -1) {
        ::close(lockDescriptor_);
        lockDescriptor_ = -1;

        // Cleanup is best-effort so a removal failure cannot hide the original
        // attachment-locking error
        std::error_code cleanupError;
        std::filesystem::remove(attachmentPath_, cleanupError);

        throw std::runtime_error("Failed to lock session attachment record");
    }

    // Store descriptive runtime information separately from the file lock.
    // The JSON is useful for inspection and future commands such as
    // `gptb session close`, while flock remains the authoritative liveness check
    const nlohmann::json record = {
        {"id", attachmentInfo.id},
        {"parent_pid", attachmentInfo.parentPid},
        {"child_pid", attachmentInfo.childPid},
        {"started_at", attachmentInfo.startedAt}
    };

    const std::string contents = record.dump(4) + '\n';

    // Write the complete attachment record through the descriptor that also
    // owns the liveness lock
    const ssize_t bytesWritten = ::write(
        lockDescriptor_,
        contents.data(),
        contents.size()
    );

    if(bytesWritten != static_cast<ssize_t>(contents.size())) {
        ::flock(lockDescriptor_, LOCK_UN);
        ::close(lockDescriptor_);
        lockDescriptor_ = -1;

        // Cleanup is best-effort so a removal failure cannot hide the original
        // attachment-write error
        std::error_code cleanupError;
        std::filesystem::remove(attachmentPath_, cleanupError);

        throw std::runtime_error("Failed to write session attachment record");
    }

    // Flush the runtime metadata before the attachment is considered active
    if(::fsync(lockDescriptor_) == -1) {
        ::flock(lockDescriptor_, LOCK_UN);
        ::close(lockDescriptor_);
        lockDescriptor_ = -1;

        // Cleanup is best-effort so a removal failure cannot hide the original
        // attachment-sync error
        std::error_code cleanupError;
        std::filesystem::remove(attachmentPath_, cleanupError);

        throw std::runtime_error("Failed to sync session attachment record");
    }
}


/**
 * ~SessionAttachmentRegistration()
 * Releases the attachment lock and removes the runtime record
 */
SessionAttachmentRegistration::~SessionAttachmentRegistration() {
    // Remove the attachment path while its liveness lock is still held.
    // This prevents another process from briefly seeing a normal shutdown
    // as an unlocked stale attachment record
    std::error_code cleanupError;
    std::filesystem::remove(attachmentPath_, cleanupError);
    
    // The attachment path is already gone, so now release the kernel lock and
    // close the descriptor that represented this attachment's liveness
    if(lockDescriptor_ != -1) {
        ::flock(lockDescriptor_, LOCK_UN);
        ::close(lockDescriptor_);
        lockDescriptor_ = -1;
    }
}
