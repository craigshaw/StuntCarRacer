#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "persistence/folder_transactions.h"
#include "persistence/career_file_io.h"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace scr::runtime;namespace fs=std::filesystem;using namespace scr::state::checkpoint;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F action,const char* message){bool caught=false;try{action();}catch(const std::exception&){caught=true;}check(caught,message);}
void write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<text;check(bool(out),"Cannot write transaction fixture");}
std::string text(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
std::map<fs::path,std::string> snapshot(const fs::path& root){std::map<fs::path,std::string> result;for(const auto& e:fs::recursive_directory_iterator(root))if(e.is_regular_file())result[e.path().lexically_relative(root)]=text(e.path());return result;}
std::wstring executable(){std::wstring value(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,value.data(),DWORD(value.size()));check(n&&n<value.size(),"Cannot locate transaction helper");value.resize(n);return value;}
void interrupt(const fs::path& root,unsigned mode,unsigned at){
    auto command=L"\""+executable()+L"\" --interrupt \""+root.wstring()+L"\" "+std::to_wstring(mode)+L" "+std::to_wstring(at);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0,"Cannot start crash fixture");
    const auto wait=WaitForSingleObject(process.hProcess,10000);if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,99);WaitForSingleObject(process.hProcess,5000);}
    DWORD code{};GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    check(wait==WAIT_OBJECT_0&&code==37,"Crash fixture did not stop at the requested boundary");
}
void fixtures(const fs::path& root){write(root/L"careers/local/progress.txt","old");write(root/L"careers/incoming/progress.txt","selected");write(root/L"multiplayer/unrelated/progress.txt","untouched");}
void operation(FolderTransactions& transaction,const fs::path& root,unsigned mode){
    if(mode==2){transaction.erase(root/L"careers/local");return;}
    const auto target=root/L"careers/incoming";
    transaction.publish(mode==1?root/L"careers/new":target,mode==1?std::vector<fs::path>{}:std::vector<fs::path>{root/L"careers/local",target},[&](const auto& stage){
        FolderTransactions::copy(root/L"careers/incoming",stage);
        if(mode==0)FolderTransactions::copy(root/L"careers/local",stage/L"recovery/previous");
    });
}
fs::path journal(const fs::path& root){for(const auto& e:fs::directory_iterator(root/L".transactions"))if(fs::exists(e.path()/L"journal.scr-transaction"))return e.path()/L"journal.scr-transaction";throw std::runtime_error("No interrupted journal");}
void processInterruptions(const fs::path& root){
    for(unsigned mode=0;mode<3;++mode)for(unsigned at=0;at<4;++at){if(mode==1&&at==unsigned(FolderTransactionStage::Parked))continue;
        const auto folder=root/(std::to_string(mode)+"-"+std::to_string(at));fixtures(folder);interrupt(folder,mode,at);
        FolderTransactions recovered(folder);check(recovered.recover(),"Interrupted publication was not recovered");
        check(!recovered.recover(),"Completed transaction replayed twice");
        if(mode==0)check(!fs::exists(folder/L"careers/local")&&text(folder/L"careers/incoming/progress.txt")=="selected"&&text(folder/L"careers/incoming/recovery/previous/progress.txt")=="old","Replace recovery lost selected or prior data");
        if(mode==1)check(text(folder/L"careers/new/progress.txt")=="selected"&&fs::exists(folder/L"careers/incoming"),"Create recovery changed its source");
        if(mode==2)check(!fs::exists(folder/L"careers/local")&&fs::exists(folder/L"careers/incoming"),"Delete recovery removed another folder");
        check(text(folder/L"multiplayer/unrelated/progress.txt")=="untouched","Transaction crossed saved-game ownership");
    }
}
void guards(const fs::path& root){
    fixtures(root);FolderTransactions transactions(root);const auto external=root.parent_path()/L"external";write(external/L"keep.txt","keep");
    rejected([&]{transactions.erase(external);},"Deletion escaped root");rejected([&]{transactions.publish(external,{},[](const auto&){});},"Publication escaped root");
    rejected([&]{transactions.publish(root/L"careers/local",{},[](const auto&){});},"Unrelated existing target was overwritten");
    rejected([&]{FolderTransactions::copy(root/L"careers/local",root/L"careers/local/nested");},"Recursive self-copy accepted");
    check(text(external/L"keep.txt")=="keep"&&text(root/L"careers/local/progress.txt")=="old","Rejected path changed data");
    const auto held=CreateFileW((root/L"careers/local").c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    check(held!=INVALID_HANDLE_VALUE,"Cannot hold fixture directory");rejected([&]{operation(transactions,root,0);},"Pinned directory was retired");CloseHandle(held);
    check(text(root/L"careers/local/progress.txt")=="old","Failed retirement changed original folder");
    check(transactions.recover()&&text(root/L"careers/incoming/progress.txt")=="selected","Interrupted I/O did not retry safely");
    const auto corrupted=root.parent_path()/L"corrupted";fixtures(corrupted);interrupt(corrupted,0,unsigned(FolderTransactionStage::Prepared));
    const auto before=snapshot(corrupted/L"careers");auto path=journal(corrupted);auto data=text(path);data[4]=99;write(path,data);
    rejected([&]{FolderTransactions(corrupted).recover();},"Unsupported journal was accepted");check(before==snapshot(corrupted/L"careers"),"Unsupported journal moved or reset progress");
    const auto forged=root.parent_path()/L"forged";fixtures(forged);const auto tx=forged/L".transactions"/NativeCareerStore::newId();
    Writer w;w.u32(0x32585446);w.u32(1);w.text("careers/../../external");w.u32(0);w.u32(0);w.u64(hash(w.data.data(),w.data.size()));
    std::string error;check(career_files::atomicWrite(tx/L"journal.scr-transaction",w.data,error),"Cannot write bounded malicious journal fixture");
    rejected([&]{FolderTransactions(forged).recover();},"Forged path was accepted");check(text(external/L"keep.txt")=="keep","Journal path escaped the data root");
}
}
int wmain(int argc,wchar_t** argv)try{
    if(argc==5&&std::wstring(argv[1])==L"--interrupt"){
        const auto at=unsigned(std::stoul(argv[4]));FolderTransactions transactions(argv[2],[at](auto stage){if(unsigned(stage)==at)ExitProcess(37);});
        operation(transactions,argv[2],unsigned(std::stoul(argv[3])));return 1;
    }
    check(argc==2,"Usage: folder_transaction_tests isolated-output");const auto root=fs::absolute(argv[1]);
    processInterruptions(root/L"crashes");guards(root/L"guards");
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"process_interruption_boundaries\":11,\"path_guards\":true,\"corrupt_journal_preserved\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
