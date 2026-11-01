#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <shlobj.h>
#include "platform/windows/shared_hall_storage.h"
#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace scr::runtime {
namespace {
[[noreturn]]void failed(const char* operation,DWORD code=GetLastError()){
    throw std::runtime_error(std::string(operation)+" (Windows error "+std::to_string(code)+")");
}
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);}
};
struct LocalMemory {void* value{};~LocalMemory(){if(value)LocalFree(value);}};
constexpr DWORD usersAccess=FILE_GENERIC_READ|FILE_GENERIC_WRITE|FILE_GENERIC_EXECUTE|DELETE;
struct HallSecurity {
    std::array<std::array<BYTE,SECURITY_MAX_SID_SIZE>,3> sids{};
    LocalMemory acl;SECURITY_DESCRIPTOR descriptor{};SECURITY_ATTRIBUTES attributes{};
    HallSecurity(){
        std::array<EXPLICIT_ACCESSW,3> entries{};
        const std::array<WELL_KNOWN_SID_TYPE,3> types{WinLocalSystemSid,WinBuiltinAdministratorsSid,WinBuiltinUsersSid};
        for(unsigned n=0;n<3;++n){DWORD size=SECURITY_MAX_SID_SIZE;if(!CreateWellKnownSid(types[n],nullptr,sids[n].data(),&size))failed("Create Hall access SID");
            auto& e=entries[n];e.grfAccessPermissions=n==2?usersAccess:FILE_ALL_ACCESS;e.grfAccessMode=GRANT_ACCESS;e.grfInheritance=SUB_CONTAINERS_AND_OBJECTS_INHERIT;
            e.Trustee.TrusteeForm=TRUSTEE_IS_SID;e.Trustee.TrusteeType=TRUSTEE_IS_WELL_KNOWN_GROUP;e.Trustee.ptstrName=reinterpret_cast<LPWSTR>(sids[n].data());
        }
        PACL value{};const auto code=SetEntriesInAclW(DWORD(entries.size()),entries.data(),nullptr,&value);acl.value=value;
        if(code!=ERROR_SUCCESS)failed("Build Hall directory access",code);
        if(!InitializeSecurityDescriptor(&descriptor,SECURITY_DESCRIPTOR_REVISION)||!SetSecurityDescriptorDacl(&descriptor,TRUE,value,FALSE)||
           !SetSecurityDescriptorControl(&descriptor,SE_DACL_PROTECTED,SE_DACL_PROTECTED))failed("Build Hall security descriptor");
        attributes={sizeof(attributes),&descriptor,FALSE};
    }
};
std::wstring installationPath(const std::filesystem::path& directory){
    Handle h;h.value=CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(h.value==INVALID_HANDLE_VALUE)failed("Open Hall installation directory");
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(h.value,&info))failed("Read Hall installation directory");
    if(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw std::invalid_argument("Hall installation path must be a directory");
    const auto size=GetFinalPathNameByHandleW(h.value,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);if(!size)failed("Resolve Hall installation directory");
    std::wstring path(size,L'\0');const auto used=GetFinalPathNameByHandleW(h.value,path.data(),size,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!used||used>=size)failed("Resolve Hall installation directory");path.resize(used);
    const auto length=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,path.data(),int(path.size()),nullptr,0,nullptr,nullptr,0);
    if(!length)failed("Normalise Hall installation directory");std::wstring normal(std::size_t(length),L'\0');
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,path.data(),int(path.size()),normal.data(),length,nullptr,nullptr,0))failed("Normalise Hall installation directory");
    return normal;
}
std::string installationKey(const std::wstring& path){
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    auto status=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0);
    if(status<0)throw std::runtime_error("Cannot open Hall installation hash");
    std::array<UCHAR,32> bytes{};
    status=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);
    if(status>=0)status=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(path.data())),ULONG(path.size()*sizeof(wchar_t)),0);
    if(status>=0)status=BCryptFinishHash(hash,bytes.data(),ULONG(bytes.size()),0);
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0)throw std::runtime_error("Cannot hash Hall installation directory");
    std::ostringstream out;for(auto b:bytes)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return out.str();
}
}
SharedHallPaths sharedHallPaths(const std::filesystem::path& directory,const std::filesystem::path& commonRoot){
    auto common=commonRoot;
    if(common.empty()){PWSTR folder{};if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData,KF_FLAG_DEFAULT,nullptr,&folder)))throw std::runtime_error("Cannot locate common Windows application data");
        common=folder;CoTaskMemFree(folder);}
    const auto key=installationKey(installationPath(directory));
    return {key,std::filesystem::absolute(common)/L"Stunt Car Racer"/L"instances"/key/L"hall"};
}
void prepareSharedHallDirectory(const std::filesystem::path& root){
    const auto target=std::filesystem::absolute(root).lexically_normal();
    if(target==target.root_path())throw std::invalid_argument("Hall directory cannot be a filesystem root");
    std::vector<std::filesystem::path> missing;auto cursor=target;
    while(!std::filesystem::exists(cursor)){missing.push_back(cursor);cursor=cursor.parent_path();}
    if(!std::filesystem::is_directory(cursor))throw std::invalid_argument("Hall parent is not a directory");
    HallSecurity security;
    for(auto n=missing.rbegin();n!=missing.rend();++n){if(!CreateDirectoryW(n->c_str(),&security.attributes)){
        const auto code=GetLastError();if(code!=ERROR_ALREADY_EXISTS||!std::filesystem::is_directory(*n))failed("Create shared Hall directory",code);
    }}
    if(!std::filesystem::is_directory(target))throw std::invalid_argument("Hall root is not a directory");
}
bool sharedHallUsersAccess(const std::filesystem::path& root){
    PACL acl{};PSECURITY_DESCRIPTOR descriptor{};
    const auto code=GetNamedSecurityInfoW(root.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&descriptor);LocalMemory memory{descriptor};
    if(code!=ERROR_SUCCESS)failed("Read shared Hall directory access",code);
    if(!acl)return false; // A null DACL is not the intended scoped policy.
    // Verify our deliberately small ACL policy directly. A general effective-
    // rights query can invoke account/group lookup; this check neither needs
    // credentials nor claims to replace the real second-account proof.
    std::array<std::array<BYTE,SECURITY_MAX_SID_SIZE>,3> sids{};
    const std::array<WELL_KNOWN_SID_TYPE,3> types{WinLocalSystemSid,WinBuiltinAdministratorsSid,WinBuiltinUsersSid};
    for(unsigned n=0;n<3;++n){DWORD size=SECURITY_MAX_SID_SIZE;if(!CreateWellKnownSid(types[n],nullptr,sids[n].data(),&size))failed("Create Hall access SID");}
    bool users=false;
    for(DWORD n=0;n<acl->AceCount;++n){void* raw{};if(!GetAce(acl,n,&raw))failed("Read Hall access entry");
        const auto* header=static_cast<ACE_HEADER*>(raw);if(header->AceType!=ACCESS_ALLOWED_ACE_TYPE)return false;
        const auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);const auto* sid=&ace->SidStart;bool known=false;
        for(unsigned group=0;group<3;++group)if(EqualSid(const_cast<DWORD*>(sid),sids[group].data())){known=true;if(group==2&&(ace->Mask&usersAccess)==usersAccess)users=true;}
        if(!known)return false;
    }
    return users;
}
struct SharedHallLock::Impl {Handle file;OVERLAPPED overlap{};bool locked{};~Impl(){if(locked)UnlockFileEx(file.value,0,1,0,&overlap);}};
SharedHallLock::SharedHallLock(const std::filesystem::path& root,std::chrono::milliseconds timeout):impl_(std::make_unique<Impl>()){
    if(timeout.count()<0)throw std::invalid_argument("Negative Hall lock timeout");
    impl_->file.value=CreateFileW((root/L"hall.lock").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(impl_->file.value==INVALID_HANDLE_VALUE)failed("Open shared Hall transaction lock");
    const auto end=std::chrono::steady_clock::now()+timeout;
    for(;;){if(LockFileEx(impl_->file.value,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&impl_->overlap)){impl_->locked=true;break;}
        const auto code=GetLastError();if(code!=ERROR_LOCK_VIOLATION)failed("Lock shared Hall transaction",code);
        if(std::chrono::steady_clock::now()>=end)failed("Shared Hall transaction lock timed out",WAIT_TIMEOUT);Sleep(5);
    }
}
SharedHallLock::~SharedHallLock()=default;
} // namespace scr::runtime
