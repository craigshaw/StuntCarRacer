#pragma once
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace scr::runtime {
struct SharedHallPaths {std::string installationKey;std::filesystem::path root;};
// Resolves an existing installation directory, independent of account/EXE hash.
// The optional common root is for isolated proof/test data. No directories are
// created and no player data is read by resolution.
SharedHallPaths sharedHallPaths(const std::filesystem::path& installationDirectory,
    const std::filesystem::path& commonRoot={});
// Creates only missing directories. Existing directories are not re-permissioned.
// New Hall directories grant local Users inherited read/write/execute/delete,
// and Administrators/System full access. Never call on private career folders.
void prepareSharedHallDirectory(const std::filesystem::path& root);
bool sharedHallUsersAccess(const std::filesystem::path& root);
// Short transaction lock; file sharing prevents lock-file replacement while
// held. The OS releases the byte-range lock when a process exits.
class SharedHallLock {
public:
    explicit SharedHallLock(const std::filesystem::path& root,
        std::chrono::milliseconds timeout=std::chrono::seconds(5));
    ~SharedHallLock();
    SharedHallLock(const SharedHallLock&)=delete;
    SharedHallLock& operator=(const SharedHallLock&)=delete;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace scr::runtime
