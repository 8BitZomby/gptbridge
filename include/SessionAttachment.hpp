#ifndef GPTB_SESSION_ATTACHMENT_HPP
#define GPTB_SESSION_ATTACHMENT_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <sys/types.h>


/**
 * SessionAttachmentInfo
 * Runtime information describing one managed-shell attachment to a logical
 * gptbridge session
 */
struct SessionAttachmentInfo {
    // Unique identifier for this particular managed-shell attachment
    std::string id;

    // PID of the parent gptb process that owns the PTY master and attachment lock
    pid_t parentPid = -1;

    // PID of the interactive child shell connected to the PTY slave
    pid_t childPid = -1;

    // UTC time at which this attachment became active
    std::string startedAt;
};


/**
 * isSessionAttachmentLive()
 * Returns true when another process still holds the attachment record's
 * exclusive liveness lock. Returns false when the record is stale
 */
bool isSessionAttachmentLive(const std::filesystem::path& attachmentPath);


/**
 * hasLiveSessionAttachments()
 * Returns true when the logical session has at least one currently live
 * managed-shell attachment
 */
bool hasLiveSessionAttachments(const std::string& sessionId);


/**
 * removeStaleSessionAttachments()
 * Removes attachment records that no longer have a live process holding
 * their liveness lock
 */
void removeStaleSessionAttachments(const std::string& sessionId);


/**
 * closeSessionAttachments()
 * Requests termination of every live managed-shell attachment belonging to
 * the logical session. Returns the number of live attachments signalled
 */
std::size_t closeSessionAttachments(const std::string& sessionId);


/**
 * SessionAttachmentRegistration
 * Registers one live managed-shell attachment and holds its liveness lock for
 * the lifetime of the object.
 *
 * If the process exits normally, the destructor removes the attachment record.
 * If the process crashes, the kernel releases the file lock automatically and
 * the leftover record can later be recognized as stale
 */
class SessionAttachmentRegistration {
    public:
        /**
         * SessionAttachmentRegistration()
         * Creates and locks the persistent runtime record for one attachment
         */
        SessionAttachmentRegistration(const std::string& sessionId, const SessionAttachmentInfo& attachmentInfo);

        /**
         * ~SessionAttachmentRegistration()
         * Releases the attachment lock and removes the runtime record
         */
        ~SessionAttachmentRegistration();

        // The registration uniquely owns an open file descriptor and therefore
        // must not be copied
        SessionAttachmentRegistration(const SessionAttachmentRegistration&) = delete;
        SessionAttachmentRegistration& operator=(const SessionAttachmentRegistration&) = delete;

    private:
        // Open descriptor whose flock represents this attachment's liveness
        int lockDescriptor_ = -1;

        // Record path retained so normal destruction can remove the file
        std::filesystem::path attachmentPath_;
};


#endif
