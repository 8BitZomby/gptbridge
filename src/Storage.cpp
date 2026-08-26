#include "Storage.hpp"

#include "SessionId.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/_types/_s_ifmt.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>


/**
 * allocateNextSessionNumber()
 * Finds, reserves, and returns the next available logical session number
 */
std::uint64_t allocateNextSessionNumber() {
    // Ensure global storage directory exists (~/.gptbridge)
    ensureStorageRoot();

    // Store the next session-ID search position separately from individual sessions
    const std::filesystem::path sequencePath = getStorageRoot() / "session-sequence.json";

    // Separate file used only for synchronization
    const std::filesystem::path lockPath = getStorageRoot() / "session-sequence.lock";

    // Temporary replacement file used for atomic updates
    const std::filesystem::path temporaryPath = getStorageRoot() / "session-sequence.tmp";

    // Open or create the dedicated lock file
    // Create it on first use with owner only permissions
    const int lockDescriptor = ::open(
            lockPath.c_str(),               // Lock file path
            O_RDWR | O_CREAT,               // Read/write; create if missing
            S_IRUSR | S_IWUSR               // Owner read/write permissions
    );

    // Allocation cannot continue without access to the lock file
    if(lockDescriptor == -1) {
        throw std::runtime_error("Failed to open session sequence lock file");
    }

    // Prevent concurrent processes from allocating the same number
    if(::flock(lockDescriptor, LOCK_EX) == -1) {
        ::close(lockDescriptor);
        throw std::runtime_error("Failed to lock session sequence");
    }

    // Track whether the reserved ID has been committed to the sequence file
    bool allocationCommitted = false;

    // Start searching at session 1 if no sequence has been saved yet
    std::uint64_t nextSessionNumber = 1;

    // Remember the directory so it can be removed if allocation fails
    std::filesystem::path allocatedSessionPath;

    try {
        // Read the saved position where the next search should begin
        if(std::filesystem::exists(sequencePath)) {
            std::ifstream input(sequencePath);

            if(!input) {
                throw std::runtime_error("Failed to open session sequence file for reading");
            }

            nlohmann::json sequence;

            try {
                input >> sequence;
            }
            catch(const nlohmann::json::parse_error&) {
                throw std::runtime_error("Failed to parse session sequence file");
            }

            // The saved number is the first ID to check during this allocation
            nextSessionNumber = sequence.at("next_session_number").get<std::uint64_t>();

            // Corrupt or unsupported counter values must not enter the search
            if(nextSessionNumber == 0 || nextSessionNumber > MaxSessionNumber) {
                throw std::runtime_error("Invalid next session number");
            }
        }

        // Make sure the parent directory exists before reserving a session
        // All logical session directories live beneath ~/.gptbridge/sessions
        const std::filesystem::path sessionsPath = getStorageRoot() / "sessions";
        ensurePrivateDirectory(sessionsPath);

        // Zero means no session number has been successfully reserved yet
        std::uint64_t allocatedSessionNumber = 0;

        // Search every possible ID at most once
        for(std::uint64_t attempts = 0; attempts < MaxSessionNumber; ++attempts) {
            // Convert the numeric candidate to user-facing form (e.g. 1 -> "s-0001")
            const std::string sessionId = formatSessionId(nextSessionNumber);

            // A session number is considered occupied when its directory exists
            const std::filesystem::path candidatePath = sessionsPath / sessionId;

            // An existing directory means this ID is already in use
            if(!std::filesystem::exists(candidatePath)) {
                // Create the directory while the allocation lock is still held
                ensurePrivateDirectory(candidatePath);

                allocatedSessionNumber = nextSessionNumber;
                allocatedSessionPath = candidatePath;
                break;
            }

            // Move to the next ID, wrapping 9999 back to 0001
            nextSessionNumber = (nextSessionNumber == MaxSessionNumber) ? 1 : nextSessionNumber + 1;
        }

        // Every possible session ID is already occupied
        if(allocatedSessionNumber == 0) {
            throw std::runtime_error("No available gptbridge session IDs");
        }

        // Begin the next allocation search after the ID we just received
        const std::uint64_t nextSearchNumber = (allocatedSessionNumber == MaxSessionNumber) ? 1 : allocatedSessionNumber + 1;

        // Persist only the next place to begin searching, not the formatted ID
        const nlohmann::json updatedSequence = { {"next_session_number", nextSearchNumber} };

        // Keep the JSON file readable
        const std::string updatedContents = updatedSequence.dump(4) + '\n';

        // Write the new counter to a temp file first
        const int temporaryDescriptor = ::open(
            temporaryPath.c_str(),          // Temporary file path
            O_WRONLY | O_CREAT | O_TRUNC,   // Write/create/replace contents
            S_IRUSR | S_IWUSR               // Owner read/write only
        );

        // Track whether the temporary descriptor still needs to be closed
        bool temporaryDescriptorOpen = true;

        if(temporaryDescriptor == -1) {
            throw std::runtime_error("Failed to open temporary session sequence file");
        }

        try {
            // Write the complete updated JSON
            const ssize_t bytesWritten = ::write(
                temporaryDescriptor,
                updatedContents.data(),
                updatedContents.size()
            );

            // Reject incomplete writes
            if(bytesWritten != static_cast<ssize_t>(updatedContents.size())) {
                throw std::runtime_error("Failed to write temporary session sequence file");
            }

            // Flush the new counter to disk before replacing the old file
            if(::fsync(temporaryDescriptor) == -1) {
                throw std::runtime_error("Failed to sync temporary session sequence file");
            }

            // Close before the temporary file is renamed
            const int closeResult = ::close(temporaryDescriptor);
            temporaryDescriptorOpen = false;
            if(closeResult == -1) {
                throw std::runtime_error("Failed to close temporary session sequence file");
            }
        }
        catch(...) {
            // Close only if earlier operation failed while it was still open
            if(temporaryDescriptorOpen) {
                ::close(temporaryDescriptor);
            }

            // Remove any incomplete replacement file
            std::filesystem::remove(temporaryPath);
            throw;
        }

        // Atomically replace the committed counter with the complete new file
        if(::rename(temporaryPath.c_str(), sequencePath.c_str()) == -1) {
            std::filesystem::remove(temporaryPath);
            throw std::runtime_error("Failed to replace session sequence file");
        }

        // The session reservation and updated sequence are now committed
        allocationCommitted = true;

        // Allow another process to allocate the next number
        if(::flock(lockDescriptor, LOCK_UN) == -1) {
            throw std::runtime_error("Failed to unlock session sequence");
        }

        // Release the lock file descriptor
        if(::close(lockDescriptor) == -1) {
            throw std::runtime_error("Failed to close session sequence lock file");
        }

        // Return the number allocated before the counter was incremented
        return allocatedSessionNumber;
    }
    catch(...) {
        // Roll back only if the sequence update was never committed
        if(!allocationCommitted && !allocatedSessionPath.empty()) {
            std::filesystem::remove(allocatedSessionPath);
        }

        // Release the allocation lock before propagating the original error
        ::flock(lockDescriptor, LOCK_UN);
        ::close(lockDescriptor);

        throw;
    }
}


/**
 * ensurePrivateDirectory()
 * Creates a directory tree and enforces owner-only permissions
 */
void ensurePrivateDirectory(const std::filesystem::path &path) {
    // Create any missing directories before applying the security policy
    std::filesystem::create_directories(path);

    // Restrict traversal, reading, and writing to the current user
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace
    );
}


/**
 * ensurePrivateFile()
 * Creates a file with owner-only permissions or repairs an existing file's permissions
 */
void ensurePrivateFile(const std::filesystem::path& path) {
    // Open or create the file without truncating existing contents.
    // A newly created file starts with mode 0600 before any higher-level stream opens it
    const int fileDescriptor = ::open(
            path.c_str(),
            O_WRONLY | O_CREAT,
            S_IRUSR | S_IWUSR
    );

    if(fileDescriptor == -1) {
        throw std::runtime_error("Failed to create private file");
    }

    // Existing files may have weaker permissions from older gptbridge versions,
    // so explicitly repair them to owner-read/write only
    if(::fchmod(fileDescriptor, S_IRUSR | S_IWUSR) == -1) {
        ::close(fileDescriptor);
        throw std::runtime_error("Failed to secure private file");
    }

    if(::close(fileDescriptor) == -1) {
        throw std::runtime_error("Failed to close private file");
    }
}


/**
 * ensureStorageRoot()
 * Creates the global storage directory if it does not already exist.
 */
void ensureStorageRoot() {
    // Keep all persistent gptbridge state private to the current user
    ensurePrivateDirectory(getStorageRoot());
}


/**
 * getStorageRoot()
 * Returns the global gptbridge storage directory, such as ~/.gptbridge.
 */
std::filesystem::path getStorageRoot() {
    // HOME gives the current user's home directory on macOS/Linux
    const char* home = std::getenv("HOME");
    // Without HOME, we cannot safely determine where global state belongs
    if(home == nullptr) {
        throw std::runtime_error("HOME environment variable is not set");
    }
    // std::filesystem joins path components without manual string concatenation
    return std::filesystem::path(home) / ".gptbridge";
}


/**
 * loadProjectRegistry()
 * Loads the full project registry from ~/.gptbridge/projects.json.
 */
nlohmann::json loadProjectRegistry() {
    // Build the full path to the global project registry
    const std::filesystem::path registryPath = getStorageRoot() / "projects.json";

    // If no registry exists yet, return an empty projects object
    if(!std::filesystem::exists(registryPath)) {
        return {
            {"projects", nlohmann::json::object()}
        };
    }
    // Open the existing registry so its JSON can be read from disk
    std::ifstream input(registryPath);

    // The file exists, so failing to open it indicates an I/O problem
    if(!input) {
        throw std::runtime_error("Failed to open projects.json for reading");
    }

    nlohmann::json registry;

    try {
        // Parse the JSON text into the in-memory registry object
        input >> registry;
    }
    catch(const nlohmann::json::parse_error&) {
        // Hide library-specific parser details behind a clearer error
        throw std::runtime_error("Failed to parse projects.json");
    }

    return registry;
}


/**
 * removeProject()
 * Removes a project from the global registry.
 * Returns true if the project existed and was removed.
 */
bool removeProject(const std::string& name) {
    // Load the current registry so the existing project set can be modified
    nlohmann::json registry = loadProjectRegistry();

    // Access the existing projects object once so reads and updates use same verified part of registry
    auto& projects = registry.at("projects");

    // If the project name is not registered, there is nothing to remove
    if(!projects.contains(name)) {
        return false;
    }
    // Erase the named project entry from the in-memory registry
    projects.erase(name);

    const std::filesystem::path registryPath = getStorageRoot() / "projects.json";

    // Ensure the project registry exists with owner-only permissions
    ensurePrivateFile(registryPath);

    // Rewrite the registry with the updated project set
    std::ofstream output(registryPath);

    if(!output) {
        throw std::runtime_error("Failed to open projects.json for writing");
    }
    // Keep the JSON human-readable for manual inspection and debugging
    output << registry.dump(4) << '\n';

    return true;
}


/**
 * saveProject()
 * Writes or updates a registered project in ~/.gptbridge/projects.json.
 */
void saveProject(const std::string& name, const std::filesystem::path& path) {

    // Make sure the global storage directory exists before opening the registry
    ensureStorageRoot();

    const std::filesystem::path registryPath = getStorageRoot() / "projects.json";

    // Load the current registry so adding a project preserves existing entries
    nlohmann::json registry = loadProjectRegistry();

    // Store paths as strings because JSON has no filesystem path type
    registry["projects"][name]["path"] = path.string();

    // Ensure the project registry exists with owner-only permissions
    ensurePrivateFile(registryPath);

    std::ofstream output(registryPath);

    if(!output) {
        throw std::runtime_error("Failed to open projects.json for writing");
    }

    // Indent by four spaces so the registry remains easy to inspect manually
    output << registry.dump(4) << '\n';
}
