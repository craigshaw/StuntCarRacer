#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include "platform/windows/shared_hall_storage.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
using namespace scr::runtime;
unsigned checks=0;
void require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
template<class F>void rejected(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,"Invalid Hall storage operation succeeded");}
std::vector<BYTE> dacl(const std::filesystem::path& path){
    PACL acl{};PSECURITY_DESCRIPTOR sd{};
    const auto code=GetNamedSecurityInfoW(path.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&sd);
    if(code!=ERROR_SUCCESS)throw std::runtime_error("Cannot inspect fixture ACL");
    std::vector<BYTE> result;if(acl){const auto* p=reinterpret_cast<BYTE*>(acl);result.assign(p,p+acl->AclSize);}LocalFree(sd);return result;
}
}
int main(int argc,char** argv)try{
    require(argc==2,"Usage: shared_hall_storage_tests isolated-output");
    const auto output=std::filesystem::absolute(argv[1])/std::to_string(GetCurrentProcessId());
    std::filesystem::create_directories(output);const auto parentBefore=dacl(output);
    const auto root=output/"shared-hall";
    prepareSharedHallDirectory(root);require(sharedHallUsersAccess(root),"Local Users cannot modify the shared root");
    require(parentBefore==dacl(output),"Preparing Hall storage changed existing ancestor permissions");
    std::filesystem::create_directory(root/"child");std::ofstream(root/"child"/"probe.txt")<<"probe";
    require(sharedHallUsersAccess(root/"child")&&sharedHallUsersAccess(root/"child"/"probe.txt"),"Files/subdirectories did not inherit shared Users access");
    const auto existing=dacl(root);prepareSharedHallDirectory(root);require(existing==dacl(root),"Reopening storage changed its ACL");
    {SharedHallLock lock(root);rejected([&]{SharedHallLock other(root,std::chrono::milliseconds(20));});
        require(!DeleteFileW((root/L"hall.lock").c_str()),"An active transaction lock file could be deleted");}
    {SharedHallLock reacquired(root,std::chrono::milliseconds(20));}
    rejected([&]{SharedHallLock invalid(root,std::chrono::milliseconds(-1));});
    rejected([&]{prepareSharedHallDirectory(root/"child"/"probe.txt");});
    rejected([&]{prepareSharedHallDirectory(output.root_path());});
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"users_acl_inheritance\":true,\"real_second_account_tested\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
