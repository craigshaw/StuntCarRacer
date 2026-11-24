#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include "platform/windows/player_data_ownership.h"
#include "persistence/player_data_layout.h"
#include "persistence/shared_hall_store.h"
#include "persistence/enhanced_hall_store.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace scr::runtime;
namespace fs=std::filesystem;
unsigned checks{};
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F f,const char* message){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,message);}
std::wstring executable(){std::wstring path(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));require(n&&n<path.size(),"Cannot locate test helper");path.resize(n);return path;}
// These fixture arguments never end in a slash or contain embedded quotes.
std::wstring quote(const std::wstring& value){return L"\""+value+L"\"";}
struct Process {
    PROCESS_INFORMATION info{};
    explicit Process(const std::wstring& args,const std::wstring& program=executable()){
        auto command=quote(program)+L" "+args;STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        require(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)!=0,"Cannot launch lock fixture");
    }
    ~Process(){if(WaitForSingleObject(info.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(info.hProcess,99);WaitForSingleObject(info.hProcess,5000);}CloseHandle(info.hThread);CloseHandle(info.hProcess);}
    DWORD finish(){require(WaitForSingleObject(info.hProcess,10000)==WAIT_OBJECT_0,"Lock helper timed out");DWORD code{};require(GetExitCodeProcess(info.hProcess,&code)!=0,"Cannot read helper result");return code;}
};
std::vector<BYTE> dacl(const fs::path& path){
    PACL acl{};PSECURITY_DESCRIPTOR sd{};
    require(GetNamedSecurityInfoW(path.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&sd)==ERROR_SUCCESS,"Cannot inspect fixture ACL");
    std::vector<BYTE> result;if(acl){const auto first=reinterpret_cast<BYTE*>(acl);result.assign(first,first+acl->AclSize);}LocalFree(sd);return result;
}
struct DeniedDirectory {
    fs::path root;PSECURITY_DESCRIPTOR descriptor{};PACL original{},blocked{};bool active{};SECURITY_INFORMATION restoreFlags{DACL_SECURITY_INFORMATION};
    explicit DeniedDirectory(fs::path path):root(std::move(path)){
        require(GetNamedSecurityInfoW(root.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&original,nullptr,&descriptor)==ERROR_SUCCESS,"Cannot retain fixture permissions");
        SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision{};
        require(GetSecurityDescriptorControl(descriptor,&control,&revision)!=0,"Cannot inspect fixture inheritance");
        restoreFlags|=(control&SE_DACL_PROTECTED)?PROTECTED_DACL_SECURITY_INFORMATION:UNPROTECTED_DACL_SECURITY_INFORMATION;
        std::array<BYTE,SECURITY_MAX_SID_SIZE> world{};DWORD size=DWORD(world.size());
        require(CreateWellKnownSid(WinWorldSid,nullptr,world.data(),&size)!=0,"Cannot create fixture SID");
        EXPLICIT_ACCESSW entry{};entry.grfAccessPermissions=FILE_GENERIC_WRITE;entry.grfAccessMode=DENY_ACCESS;entry.Trustee.TrusteeForm=TRUSTEE_IS_SID;entry.Trustee.ptstrName=reinterpret_cast<LPWSTR>(world.data());
        require(SetEntriesInAclW(1,&entry,original,&blocked)==ERROR_SUCCESS,"Cannot construct denied fixture");
        require(SetNamedSecurityInfoW(const_cast<LPWSTR>(root.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,blocked,nullptr)==ERROR_SUCCESS,"Cannot deny fixture writes");active=true;
    }
    ~DeniedDirectory(){if(active)SetNamedSecurityInfoW(const_cast<LPWSTR>(root.c_str()),SE_FILE_OBJECT,restoreFlags,nullptr,nullptr,original,nullptr);if(blocked)LocalFree(blocked);if(descriptor)LocalFree(descriptor);}
    void restore(){if(active){require(SetNamedSecurityInfoW(const_cast<LPWSTR>(root.c_str()),SE_FILE_OBJECT,restoreFlags,nullptr,nullptr,original,nullptr)==ERROR_SUCCESS,"Cannot restore fixture permissions");active=false;}}
};
int helper(int argc,wchar_t** argv){
    PlayerDataOwnership owner(argv[2]);
    if(std::wstring(argv[1])==L"--probe")return owner.acquired()?0:23;
    require(argc==4&&owner.acquired(),"Crash fixture did not acquire the root");
    const auto ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,argv[3]);require(ready!=nullptr,"Cannot signal fixture readiness");
    require(SetEvent(ready)!=0,"Cannot signal root ownership");CloseHandle(ready);Sleep(INFINITE);return 1;
}
}
int wmain(int argc,wchar_t** argv)try{
    if(argc>=3&&std::wstring(argv[1]).find(L"--")==0)return helper(argc,argv);
    require(argc==3,"Usage: player_data_ownership_tests isolated-output player-executable");
    const auto output=fs::absolute(argv[1]);const auto root=output/L"data space-\u00e9-\u8eca";
    std::wstring identity;
    {
        PlayerDataOwnership owner(root);require(owner.acquired(),"First owner rejected");identity=owner.windowClassName();
        PlayerDataOwnership other(root);require(!other.acquired()&&other.windowClassName()==identity,"Same root accepted competing writer");
        PlayerDataOwnership alias(root/L".."/root.filename());require(!alias.acquired()&&alias.windowClassName()==identity,"Lexical alias escaped ownership");
        auto uppercase=root.wstring();std::transform(uppercase.begin(),uppercase.end(),uppercase.begin(),[](wchar_t c){return wchar_t(towupper(c));});
        PlayerDataOwnership upper(uppercase);require(!upper.acquired()&&upper.windowClassName()==identity,"Case alias escaped ownership");
        PlayerDataOwnership independent(output/L"other-data");require(independent.acquired()&&independent.windowClassName()!=identity,"Independent roots contend");
        require(!DeleteFileW((root/L"player.lock").c_str()),"Active lock filename could be replaced");
        Process probe(L"--probe "+quote(root.wstring()));require(probe.finish()==23,"Second process escaped ownership");
    }
    require(fs::exists(root/L"player.lock"),"Lock release deleted the shared lock object");
    {PlayerDataOwnership again(root);require(again.acquired(),"Stale lock filename blocked a new owner");}
    {Process clean(L"--probe "+quote(root.wstring()));require(clean.finish()==0,"Clean child failed to own the root");}
    const auto eventName=L"Local\\StuntCarRacerLockTest-"+std::to_wstring(GetCurrentProcessId());
    const auto ready=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());require(ready!=nullptr,"Cannot create crash-fixture event");
    {
        Process crashing(L"--hold "+quote(root.wstring())+L" "+quote(eventName));
        require(WaitForSingleObject(ready,10000)==WAIT_OBJECT_0,"Crash fixture did not become ready");
        {PlayerDataOwnership busy(root);require(!busy.acquired(),"Parent escaped helper's lock");}
        require(TerminateProcess(crashing.info.hProcess,77)!=0&&crashing.finish()==77,"Cannot interrupt owned fixture process");
    }
    CloseHandle(ready);
    {PlayerDataOwnership afterCrash(root);require(afterCrash.acquired(),"Process termination left an active lock");}
    const auto denied=output/L"denied";fs::create_directories(denied);const auto before=dacl(denied);
    std::ofstream(denied/L"player.lock")<<"existing lock";
    {DeniedDirectory fixture(denied);rejected([&]{PlayerDataOwnership invalid(denied);},"Unwritable directory accepted an owner");fixture.restore();}
    require(before==dacl(denied)&&fs::file_size(denied/L"player.lock")==13,"Ownership changed permissions or wrote into an unwritable root");
    std::ofstream(output/L"file-instead-of-root")<<"preserve";
    rejected([&]{PlayerDataOwnership invalid(output/L"file-instead-of-root");},"Regular file accepted as data root");
    rejected([&]{PlayerDataOwnership invalid(output.root_path());},"Drive root accepted as data folder");
    const auto layout=playerDataLayout(output/L"inherited");PlayerDataOwnership owner(layout.root);
    fs::create_directories(layout.root/L"control/nested");const auto inherited=dacl(layout.root/L"control/nested"),parent=dacl(layout.root);
    SharedHallArchive original(layout.originalHall,{1,1});EnhancedHallArchive enhanced(layout.enhancedHall,{1,1,1});
    require(original.root()==layout.originalHall.root&&enhanced.root()==layout.enhancedHall.root,"Explicit category roots were redirected");
    require(original.read().state==HallArchiveState::Missing&&enhanced.read().state==HallArchiveState::Missing,"Fresh category state differs");
    original.commit({});enhanced.commit({});
    require(dacl(original.root())==inherited&&dacl(enhanced.root())==inherited&&dacl(layout.root)==parent,"Portable Hall broadened inherited permissions");
    require(!fs::exists(enhancedHallRoot(original.root())),"Enhanced created a legacy sibling despite an explicit root");
    const auto startupRoot=output/L"player-startup";
    {
        Process player(L"--data-dir "+quote(startupRoot.wstring()),argv[2]);
        require(WaitForInputIdle(player.info.hProcess,10000)==0,"Player launcher did not become idle");
        HWND window{};
        {PlayerDataOwnership busy(startupRoot);require(!busy.acquired(),"Player did not hold data-root ownership");
            window=FindWindowW(busy.windowClassName().c_str(),nullptr);require(window!=nullptr,"Player did not use the canonical root window identity");}
        require(SetWindowPos(window,nullptr,-30000,-30000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)!=0,"Cannot isolate fixture window from desktop");
        Process duplicate(L"--data-dir "+quote((startupRoot/L".."/startupRoot.filename()).wstring()),argv[2]);
        require(duplicate.finish()==0&&WaitForSingleObject(player.info.hProcess,0)==WAIT_TIMEOUT,"Duplicate player did not leave the first launcher active");
        require(PostMessageW(window,WM_CLOSE,0,0)!=0&&player.finish()==0,"Player did not close cleanly");
    }
    {PlayerDataOwnership afterClose(startupRoot);require(afterClose.acquired(),"Player close retained ownership");}
    require(!fs::exists(startupRoot/L"settings.ini")&&!fs::exists(startupRoot/L"assets"),"Closing the isolated launcher wrote settings/assets");
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"cross_process\":true,\"crash_release\":true,\"inherited_acl\":true,\"player_startup\":true,\"real_second_account_tested\":false}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
