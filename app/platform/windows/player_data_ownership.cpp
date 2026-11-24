#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "platform/windows/player_data_ownership.h"
#include "persistence/player_data_layout.h"
#include <sstream>
#include <system_error>

namespace scr::runtime {
namespace {
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
[[noreturn]]void failed(const std::error_code& error){
    throw std::system_error(error,
        "Cannot reserve the game data folder. Choose a writable data folder or move the game to one");
}
[[noreturn]]void failed(DWORD code){failed(std::error_code(int(code),std::system_category()));}
}
struct PlayerDataOwnership::Impl {
    Handle directory,file;
    OVERLAPPED overlap{};
    bool locked{};
    std::wstring windowClass;
    ~Impl(){if(locked)UnlockFileEx(file.value,0,1,0,&overlap);}
};
PlayerDataOwnership::PlayerDataOwnership(const std::filesystem::path& directory):impl_(std::make_unique<Impl>()){
    const auto root=playerDataLayout(directory).root;
    std::error_code error;std::filesystem::create_directories(root,error);
    if(error)failed(error);
    // Pin the directory while in use, and identify aliases by the actual object.
    impl_->directory.value=CreateFileW(root.c_str(),FILE_READ_ATTRIBUTES|FILE_ADD_FILE|FILE_ADD_SUBDIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(impl_->directory.value==INVALID_HANDLE_VALUE)failed(GetLastError());
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(impl_->directory.value,&info))failed(GetLastError());
    std::wostringstream key;key<<L"StuntCarRacerNativeGame-"<<std::hex<<info.dwVolumeSerialNumber
        <<L'-'<<info.nFileIndexHigh<<L'-'<<info.nFileIndexLow;
    impl_->windowClass=key.str();
    // Never delete the lock filename: all contenders must lock the same object.
    impl_->file.value=CreateFileW((root/L"player.lock").c_str(),GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(impl_->file.value==INVALID_HANDLE_VALUE)failed(GetLastError());
    if(LockFileEx(impl_->file.value,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&impl_->overlap))
        impl_->locked=true;
    else if(const auto code=GetLastError();code!=ERROR_LOCK_VIOLATION)failed(code);
}
PlayerDataOwnership::~PlayerDataOwnership()=default;
bool PlayerDataOwnership::acquired()const noexcept{return impl_->locked;}
const std::wstring& PlayerDataOwnership::windowClassName()const noexcept{return impl_->windowClass;}
} // namespace scr::runtime
