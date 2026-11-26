#pragma once
#include "records/hall_of_fame.h"
#include "persistence/hall_storage_types.h"
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace scr::runtime {
// Creates only missing directories. Existing directories are not re-permissioned.
// New Hall directories grant local Users inherited read/write/execute/delete,
// and Administrators/System full access. Never call on private career folders.
void prepareSharedHallDirectory(const std::filesystem::path& root);
// Portable roots inherit the chosen directory's existing Windows permissions.
void prepareHallDirectory(const std::filesystem::path&,HallDirectoryAccess);
bool sharedHallUsersAccess(const std::filesystem::path& root);
HallAttemptId newHallRunId();
HallAttemptId hallCompletionId(const HallAttemptId& run,std::uint64_t crossing,HallRecordKind);
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
