#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "persistence/shared_hall_store.h"
#include "platform/windows/shared_hall_storage.h"
#include "hall_ghost_fixture.h"
#include <atomic>
#include <fstream>
#include <future>
#include <iostream>

namespace {
using namespace scr::runtime;using namespace scr::test::hall;namespace fs=std::filesystem;
unsigned checks{};
void require(bool v,const char* m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejected(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,"Failed Hall operation succeeded");}
std::shared_ptr<const HallSubmission> submission(unsigned n,unsigned t=4217,bool recording=true,HallBoardKey board={}){
    auto a=attempt(n,t,board);auto g=ghost(a);return std::make_shared<const HallSubmission>(makeHallSubmission(a,recording?&g:nullptr));
}
HallBytes bytes(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void write(const fs::path& path,const HallBytes& b){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));require(bool(out),"Cannot write test fixture");}
unsigned count(const HallArchiveRead& r){require(bool(r.hall),"Hall read has no table");return unsigned(r.hall->entries({}).size());}
void basic(const fs::path& root){
    SharedHallArchive archive(root,catalog);require(archive.read().state==HallArchiveState::Missing,"Fresh Hall was seeded");
    auto first=submission(1);require(archive.commit({first})==1,"First generation differs");
    const auto pinned=archive.readGhost(first->attempt);require(pinned.state==HallGhostState::Valid,"New ghost unavailable");
    require(sharedHallUsersAccess(root/"ghosts"/hallObjectName(first->attempt.id)),"Ghost did not inherit common access");
    require(archive.commit({first})==1&&count(archive.read())==1,"Retried attempt duplicated or rewrote index");
    for(unsigned n=2;n<=5;++n)archive.commit({submission(n)});
    require(count(archive.read())==5&&fs::exists(root/"index.scr-hall.bak2"),"Recovery generations not retained");
    write(root/"index.scr-hall",{1,2,3});require(archive.read().state==HallArchiveState::Recovered&&count(archive.read())==4,"Damaged head did not recover newest backup");
    archive.commit({submission(6)});require(archive.read().state==HallArchiveState::Valid&&count(archive.read())==5,"Recovered transaction did not merge");
    // Missing/corrupt objects cannot erase a valid result; original data repairs retries.
    const auto object=root/"ghosts"/hallObjectName(first->attempt.id);fs::remove(object);
    require(archive.readGhost(first->attempt).state==HallGhostState::Missing&&count(archive.read())==5,"Missing ghost erased time");
    archive.commit({first});write(object,{8,9});require(archive.readGhost(first->attempt).state==HallGhostState::Corrupt,"Damaged ghost looked usable");
    archive.commit({first});require(archive.readGhost(first->attempt).state==HallGhostState::Valid,"Retry did not repair exact ghost object");
    auto conflict=*first;conflict.attempt.name="Other";rejected([&]{archive.commit({std::make_shared<const HallSubmission>(conflict)});});
    auto bad=*first;bad.ghost.back()^=1;rejected([&]{archive.commit({std::make_shared<const HallSubmission>(bad)});});
    // Pinning is a complete independent in-memory recording.
    require(pinned.ghost&&pinned.ghost->samples.size()==3,"Pinned ghost changed with disk writes");
}
void preservation(const fs::path& root){
    SharedHallArchive archive(root,catalog);archive.commit({submission(1)});auto head=bytes(root/"index.scr-hall");
    auto future=head;future[4]=99;write(root/"index.scr-hall",future);
    require(archive.read().state==HallArchiveState::Unsupported,"Future index version accepted");rejected([&]{archive.commit({submission(2)});});
    require(bytes(root/"index.scr-hall")==future,"Unsupported archive overwritten");
    write(root/"index.scr-hall",{1});require(archive.read().state==HallArchiveState::Corrupt,"Corrupt Hall became empty");
    rejected([&]{archive.commit({submission(2)});});require(bytes(root/"index.scr-hall")==HallBytes{1},"Corrupt archive overwritten");
    write(root/"index.scr-hall",head);SharedHallArchive other(root,{catalog.trackContent,2});
    require(other.read().state==HallArchiveState::Unsupported,"Incompatible rules accepted");rejected([&]{other.commit({std::make_shared<const HallSubmission>(makeHallSubmission(attempt(2)))});});
    archive.commit({submission(2)});write(root/"index.scr-hall.bak2",future);
    require(archive.read().state==HallArchiveState::Unsupported,"Unsupported backup ignored");
    rejected([&]{archive.commit({submission(3)});});
}
void collection(const fs::path& root){
    SharedHallArchive archive(root,catalog);std::vector<std::shared_ptr<const HallSubmission>> initial;
    for(unsigned n=1;n<=20;++n)initial.push_back(submission(n,5000+n));archive.commit(initial);
    auto pinned=archive.readGhost(initial.back()->attempt);const auto old=root/"ghosts"/hallObjectName(initial.back()->attempt.id);
    archive.commit({submission(21,4000)});require(fs::exists(old),"Object still in recovery generation was collected");
    archive.commit({submission(22,3900)});require(fs::exists(old),"Second recovery generation lost its ghost");
    std::ofstream(root/"ghosts"/"unrelated.txt")<<"keep";
    archive.commit({submission(23,3800)});require(!fs::exists(old),"Unreferenced ghost was never collected");
    require(fs::exists(root/"ghosts"/"unrelated.txt")&&pinned.ghost->samples.size()==3,"Collection touched unrelated data or pinned playback");
    require(archive.commit({initial.back()})==4,"Displaced retry re-entered improving board");
}
void interrupted(const fs::path& root){
    for(unsigned point=0;point<=unsigned(HallWriteStage::Collected);++point){const auto folder=root/std::to_string(point);
        SharedHallArchive baseline(folder,catalog);for(unsigned n=1;n<=3;++n)baseline.commit({submission(n)});
        bool injected=false;SharedHallArchive writer(folder,catalog,[&](auto stage){if(unsigned(stage)==point&&!injected){injected=true;throw std::runtime_error("Injected interruption");}});
        auto pending=submission(4);rejected([&]{writer.commit({pending});});require(injected,"Transaction stage not exercised");
        const auto reopened=baseline.read();require(reopened.hall&&count(reopened)>=3,"Interrupted transaction lost last good generation");
        baseline.commit({pending});require(count(baseline.read())==4,"Interrupted retry lost or duplicated attempt");
        const auto saved=baseline.read();for(const auto& e:saved.hall->entries({}))require(baseline.readGhost(e.attempt).state==HallGhostState::Valid,"Index published before usable object");
    }
    // Recovering from bak2 must not overwrite the sole good copy first.
    const auto folder=root/"sole-bak2";SharedHallArchive baseline(folder,catalog);for(unsigned n=1;n<=3;++n)baseline.commit({submission(n)});
    write(folder/"index.scr-hall",{0});write(folder/"index.scr-hall.bak1",{0});
    require(baseline.read().state==HallArchiveState::Recovered,"Second backup could not recover");
    SharedHallArchive writer(folder,catalog,[](auto stage){if(stage==HallWriteStage::BackupPublished)throw std::runtime_error("Interrupted recovery");});
    rejected([&]{writer.commit({submission(9)});});require(count(baseline.read())>=1,"Recovery rotation destroyed sole valid generation");
    baseline.commit({submission(9)});require(count(baseline.read())==2,"Recovery retry differs");
}
void worker(const fs::path& root){
    std::atomic<bool> fail{true};SharedHallStore store(root,catalog,[&](auto stage){if(fail&&stage==HallWriteStage::HeadFlushed)throw std::runtime_error("Injected write failure");});
    require(store.submit(*submission(1))==HallSubmitResult::Queued,"Submission not queued");
    require(!store.flush()&&store.pending()&&store.failed(),"Failed write was acknowledged or dropped");
    require(store.submit(*submission(1))==HallSubmitResult::Duplicate,"Pending retry duplicated");
    auto conflict=*submission(1);conflict.attempt.name="Other";rejected([&]{store.submit(conflict);});
    for(unsigned n=2;n<=50;++n)store.submit(*submission(n,5000-n));
    fail=false;store.retry();require(store.flush()&&!store.pending()&&!store.failed(),"Retry did not drain retained work");
    const auto table=store.read();require(count(table)==20,"Pending coalescing lost qualifying attempts");
    require(table.hall->entries({}).front().attempt.id==id(1)&&table.hall->entries({}).back().attempt.id==id(32),"Coalesced top 20 differs");
    // Backpressure must be explicit and leave ownership with the caller.
    SharedHallStore limited(root/"limited",catalog,{},128);auto big=submission(100);
    require(limited.submit(*big)==HallSubmitResult::Full&&!limited.pending(),"Full queue silently took ownership");
    require(limited.submit(*submission(101,4217,false))==HallSubmitResult::Queued&&limited.flush(),"Time-only queue failed");
    // Separate instances have stale initial observations but merge under the same lock.
    SharedHallStore a(root/"two",catalog),b(root/"two",catalog);a.read();b.read();
    for(unsigned n=1;n<=10;++n){a.submit(*submission(n,4000+n,false));b.submit(*submission(n+10,4100+n,false));}
    require(a.flush()&&b.flush()&&count(a.read())==20,"Separate workers overwrote each other's attempts");
}
void arrivals(const fs::path& root){
    std::promise<void> writing,release;auto ready=writing.get_future();auto proceed=release.get_future();std::atomic<bool> first{true};
    SharedHallStore store(root,catalog,[&](auto stage){if(stage==HallWriteStage::HeadFlushed&&first.exchange(false)){
        writing.set_value();if(proceed.wait_for(std::chrono::seconds(5))!=std::future_status::ready)throw std::runtime_error("Arrival test timed out");}});
    store.submit(*submission(1,5000));require(ready.wait_for(std::chrono::seconds(2))==std::future_status::ready,"Worker did not enter publication");
    for(unsigned n=2;n<=22;++n)store.submit(*submission(n,4000-n));release.set_value();
    require(store.flush(),"Arrivals during publication were not drained");const auto saved=store.read();
    require(count(saved)==20&&saved.hall->entries({}).front().attempt.id==id(22)&&saved.hall->entries({}).back().attempt.id==id(3),"Acknowledgement removed newer queued attempts");
}
void denied(const fs::path& root){
    SharedHallArchive archive(root,catalog);const auto s=submission(1);archive.commit({s});const auto head=root/"index.scr-hall";const auto original=bytes(head);
    require(SetFileAttributesW(head.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"Cannot set fixture read-only");
    {SharedHallStore store(root,catalog);store.submit(*submission(2));require(!store.flush()&&store.failed()&&store.pending(),"Denied publication was acknowledged");
        require(bytes(head)==original,"Denied publication changed the old index");
        require(SetFileAttributesW(head.c_str(),FILE_ATTRIBUTE_NORMAL)!=0,"Cannot restore fixture access");
        store.retry();require(store.flush()&&count(store.read())==2,"Denied publication could not retry");}
    // A healthy payload that cannot be written must stay pending, not become time-only.
    const auto blocked=root/"ghosts"/hallObjectName(id(3));require(fs::create_directory(blocked),"Cannot obstruct fixture object");
    {SharedHallStore store(root,catalog);store.submit(*submission(3));require(!store.flush()&&store.pending(),"Failed ghost write lost its payload");
        require(count(store.read())==2,"Failed ghost write published an unbacked time");fs::remove(blocked);store.retry();
        require(store.flush()&&count(store.read())==3&&store.readGhost(submission(3)->attempt).state==HallGhostState::Valid,"Ghost failure retry changed its time or recording");}
    // A sharing violation is an access failure, not a corrupt or empty table.
    const auto handle=CreateFileW(head.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(handle!=INVALID_HANDLE_VALUE,"Cannot hold fixture exclusively");rejected([&]{archive.read();});CloseHandle(handle);
}
}
int main(int argc,char** argv)try{
    require(argc==2,"Usage: shared_hall_store_tests isolated-output");const auto root=fs::absolute(argv[1])/std::to_string(GetCurrentProcessId());
    basic(root/"basic");preservation(root/"preserve");collection(root/"collect");interrupted(root/"interrupt");worker(root/"worker");arrivals(root/"arrivals");denied(root/"denied");
    std::cout<<"Shared Hall store: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
