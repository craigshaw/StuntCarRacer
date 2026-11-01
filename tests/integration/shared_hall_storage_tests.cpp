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
    std::filesystem::create_directories(output/"Install A");std::filesystem::create_directories(output/"Install B");
    const auto parentBefore=dacl(output);
    const auto a=sharedHallPaths(output/"Install A",output/"common");
    const auto alias=sharedHallPaths(output/"Install A"/".."/"Install A",output/"common");
    auto upper=(output/"Install A").wstring();std::transform(upper.begin(),upper.end(),upper.begin(),[](wchar_t c){return wchar_t(towupper(c));});
    require(a.root==alias.root&&a.installationKey==sharedHallPaths(upper,output/"common").installationKey,"Path aliases/case created multiple Halls");
    require(a.installationKey!=sharedHallPaths(output/"Install B",output/"common").installationKey,"Separate installations shared identity");
    std::ofstream(output/"Install A"/"game.exe")<<"replacement executable";
    require(a.root==sharedHallPaths(output/"Install A",output/"common").root,"Executable replacement changed the Hall");
    require(!std::filesystem::exists(a.root),"Path resolution created real data");
    prepareSharedHallDirectory(a.root);require(sharedHallUsersAccess(a.root),"Local Users cannot modify the shared root");
    require(parentBefore==dacl(output),"Preparing Hall storage changed existing ancestor permissions");
    std::filesystem::create_directory(a.root/"child");std::ofstream(a.root/"child"/"probe.txt")<<"probe";
    require(sharedHallUsersAccess(a.root/"child")&&sharedHallUsersAccess(a.root/"child"/"probe.txt"),"Files/subdirectories did not inherit shared Users access");
    const auto existing=dacl(a.root);prepareSharedHallDirectory(a.root);require(existing==dacl(a.root),"Reopening storage changed its ACL");
    {SharedHallLock lock(a.root);rejected([&]{SharedHallLock other(a.root,std::chrono::milliseconds(20));});
        require(!DeleteFileW((a.root/L"hall.lock").c_str()),"An active transaction lock file could be deleted");}
    {SharedHallLock reacquired(a.root,std::chrono::milliseconds(20));}
    rejected([&]{SharedHallLock invalid(a.root,std::chrono::milliseconds(-1));});
    rejected([&]{sharedHallPaths(output/"missing",output);});
    rejected([&]{sharedHallPaths(a.root/"child"/"probe.txt",output);});
    rejected([&]{prepareSharedHallDirectory(a.root/"child"/"probe.txt");});
    rejected([&]{prepareSharedHallDirectory(output.root_path());});
    const auto defaultPath=sharedHallPaths(output/"Install A");
    require(defaultPath.installationKey==a.installationKey&&defaultPath.root!=a.root,"Account-independent key depends on test/common root");
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"users_acl_inheritance\":true,\"real_second_account_tested\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
