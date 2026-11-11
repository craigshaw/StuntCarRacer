#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "persistence/enhanced_hall_store.h"
#include "platform/windows/shared_hall_storage.h"
#include <fstream>
#include <iostream>

namespace {
using namespace scr::runtime;namespace fs=std::filesystem;
constexpr EnhancedCatalog catalog{0x123456789abcdef0ULL,1};
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void text(const fs::path& path,const std::string& value){std::ofstream out(path);out<<value;require(bool(out),"Cannot write proof marker");}
void checkRoot(const fs::path& root){std::ifstream in(root/"proof-marker.txt");std::string marker;std::getline(in,marker);require(marker=="SCR Enhanced Hall proof 1","Not an isolated durable Hall proof root");}
std::shared_ptr<const EnhancedSubmission> submission(unsigned identity){
    EnhancedHallAttempt a;for(unsigned n=0;n<4;++n)a.id[n]=std::uint8_t(identity>>(n*8));a.name=identity%2?"Writer A":"Writer B";
    a.time.elapsedMicroseconds=240000+std::int64_t(identity)*2000;a.time.scoredMicroseconds=a.time.elapsedMicroseconds;
    a.time.confidence=scr::timing::Reason::reliable;a.time.hasEstimate=true;
    EnhancedHallGhost g;g.catalog=catalog;g.attempt=a.id;g.board=a.board;auto& data=g.recording;data.time=a.time;
    data.start.crossingMicroseconds=60000;data.start.observedMicroseconds=120000;
    data.start.confidence=data.finish.confidence=scr::timing::Reason::reliable;data.start.player=data.finish.player=11;
    data.finish.crossingMicroseconds=60000+a.time.elapsedMicroseconds;data.finish.observedMicroseconds=((data.finish.crossingMicroseconds+119999)/120000)*120000;
    for(std::int64_t at=-60000;;at+=120000){GhostLapSample sample;sample.microseconds=at;sample.pose.worldPosition={100,200,std::uint32_t(at+60000)};
        data.samples.push_back(sample);if(at>=a.time.elapsedMicroseconds)break;}
    return std::make_shared<const EnhancedSubmission>(makeEnhancedSubmission(a,&g));
}
std::wstring executable(){std::wstring path(32768,L'\0');const auto used=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));require(used&&used<path.size(),"Cannot locate proof executable");path.resize(used);return path;}
std::wstring quoted(const fs::path& path){return L"\""+fs::absolute(path).wstring()+L"\"";}
struct Process {
    HANDLE handle{};
    ~Process(){if(handle){DWORD code{};if(GetExitCodeProcess(handle,&code)&&code==STILL_ACTIVE){TerminateProcess(handle,3);WaitForSingleObject(handle,5000);}CloseHandle(handle);}}
    void start(std::wstring args){const auto exe=executable();auto command=L"\""+exe+L"\" "+args;
        STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
        require(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=0,"Cannot launch owned proof child");handle=pi.hProcess;CloseHandle(pi.hThread);}
    void wait(){require(WaitForSingleObject(handle,15000)==WAIT_OBJECT_0,"Proof child timed out");DWORD code{};require(GetExitCodeProcess(handle,&code)&&code==0,"Proof child failed");}
    void kill(){require(TerminateProcess(handle,77)!=0&&WaitForSingleObject(handle,5000)==WAIT_OBJECT_0,"Cannot terminate owned proof child");}
};
void waitFor(const fs::path& path){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!fs::exists(path)&&std::chrono::steady_clock::now()<until)Sleep(5);require(fs::exists(path),"Proof child did not reach transaction boundary");}
void verify(EnhancedHallArchive& archive,unsigned expected){
    const auto read=archive.read();require(read.hall&&read.hall->entries({}).size()==expected,"Durable proof lost or duplicated an attempt");
    for(const auto& e:read.hall->entries({}))require(archive.readGhost(e.attempt).state==HallGhostState::Valid,"Published entry has no matching usable ghost");
}
void initialise(const fs::path& root){require(!fs::exists(root),"Use a new disposable durable proof root");prepareSharedHallDirectory(root);text(root/"proof-marker.txt","SCR Enhanced Hall proof 1");}
void run(const fs::path& root){
    initialise(root);const auto competing=root/"competing";initialise(competing);EnhancedHallArchive shared(competing,catalog);
    Process a,b;a.start(L"--writer "+quoted(competing)+L" 1");b.start(L"--writer "+quoted(competing)+L" 2");
    waitFor(competing/"ready-1");waitFor(competing/"ready-2");text(competing/"go","ready");a.wait();b.wait();verify(shared,20);
    const auto snapshot=shared.read();for(unsigned n=0;n<20;++n)require(snapshot.hall->entries({})[n].attempt.id==submission(n+1)->attempt.id,"Merged writer order differs");
    for(unsigned point=0;point<=unsigned(HallWriteStage::Collected);++point){const auto folder=root/("crash-"+std::to_string(point));initialise(folder);
        EnhancedHallArchive archive(folder,catalog);for(unsigned n=1;n<=3;++n)archive.commit({submission(n)});
        Process child;child.start(L"--crash-writer "+quoted(folder)+L" "+std::to_wstring(point));waitFor(folder/"at-boundary");child.kill();
        const auto saved=archive.read();require(saved.hall&&saved.hall->entries({}).size()>=3,"Killed writer lost last good index");
        archive.commit({submission(4)});verify(archive,4);
    }
    text(root/"results.json","{\"status\":\"passed\",\"writers\":2,\"entries\":20,\"lost_updates\":0,\"retry_duplicates\":0,\"process_crash_points\":7,\"all_references_usable\":true,\"real_second_account_tested\":false}\n");
}
}
int wmain(int argc,wchar_t** argv)try{
    require(argc>=3,"Usage: native_enhanced_store_proof --proof|--writer|--crash-writer disposable-root [number]");
    const std::wstring mode=argv[1];const auto root=fs::absolute(argv[2]);
    if(mode==L"--proof"){require(argc==3,"Invalid proof arguments");run(root);}
    else if(mode==L"--writer"){
        require(argc==4,"Invalid writer arguments");checkRoot(root);const auto writer=std::stoul(argv[3]);require(writer==1||writer==2,"Invalid writer ID");
        EnhancedHallStore store(root,catalog);require(store.read().state==HallArchiveState::Missing,"Writer did not start with stale empty observation");
        text(root/("ready-"+std::to_string(writer)),"ready");waitFor(root/"go");
        for(unsigned n=0;n<10;++n){const auto s=submission(n*2+unsigned(writer));require(store.submit(*s)==EnhancedSubmitResult::Queued,"Writer queue rejected attempt");
            require(store.submit(*s)==EnhancedSubmitResult::Duplicate||store.flush(),"Writer retry failed");}
        require(store.flush(),"Durable writer failed to flush");
    }else if(mode==L"--crash-writer"){
        require(argc==4,"Invalid crash writer arguments");checkRoot(root);const auto point=std::stoul(argv[3]);require(point<=unsigned(HallWriteStage::Collected),"Invalid crash point");
        EnhancedHallArchive archive(root,catalog,[&](auto stage){if(unsigned(stage)==point){text(root/"at-boundary","ready");Sleep(30000);}});archive.commit({submission(4)});
    }else throw std::runtime_error("Unknown durable Hall proof mode");
    std::cout<<"Durable Hall proof passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
