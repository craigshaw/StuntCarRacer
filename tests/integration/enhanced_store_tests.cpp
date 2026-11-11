#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "persistence/enhanced_hall_store.h"
#include "persistence/native_career_store.h"
#include "persistence/shared_hall_store.h"
#include "platform/windows/shared_hall_storage.h"
#include "enhanced_record_fixture.h"
#include "hall_ghost_fixture.h"
#include "ghost_lap_fixture.h"
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <atomic>

namespace {
using namespace scr::runtime;namespace fixture=scr::test::enhanced;namespace old=scr::test::hall;namespace fs=std::filesystem;
std::atomic<unsigned> checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,"Invalid storage operation succeeded");}
HallBytes bytes(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void write(const fs::path& path,const HallBytes& data){fs::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));check(bool(output),"Cannot write synthetic fixture");}
std::map<fs::path,HallBytes> files(const fs::path& root){std::map<fs::path,HallBytes> result;if(fs::exists(root))for(const auto& e:fs::recursive_directory_iterator(root))if(e.is_regular_file())result.emplace(e.path(),bytes(e.path()));return result;}
std::shared_ptr<const EnhancedSubmission> submission(unsigned n,std::int64_t elapsed=120000,bool ghost=true){
    auto a=fixture::attempt(n,elapsed);auto g=fixture::ghost(a);return std::make_shared<const EnhancedSubmission>(makeEnhancedSubmission(a,ghost?&g:nullptr));
}
void privateBests(const fs::path& root){
    NativeCareerStore store(root);auto pb=fixture::best(120499);auto original=scr::test::ghost::lap(pb.key.career);
    store.saveGhost(original);NativeCareerInfo info;info.id=pb.key.career;info.name="Craig";store.saveCareer(info,{1,2,3});check(store.flush(),"Original fixture did not save");
    const auto originalFiles=files(root/"ghosts"),careerFiles=files(root/"careers");
    check(store.readEnhancedBest(pb.key,fixture::catalog).state==CareerFileState::Missing,"Original PB imported as Enhanced");
    store.saveEnhancedBest(pb);check(store.flush(),"Enhanced PB did not save");
    const auto path=root/"ghosts-enhanced-v1"/pb.key.career/"0-normal.scr-ebest";
    const auto held=bytes(path);auto tied=fixture::best(120001,false);store.saveEnhancedBest(tied);check(store.flush()&&bytes(path)==held,"PB tie changed the saved ghost or generation");
    store.saveEnhancedBest(fixture::best(119499,false));check(store.flush(),"Time-only PB failed");
    auto read=store.readEnhancedBest(pb.key,fixture::catalog);check(!decodeEnhancedBest(read.data).recording,"Time-only PB restored a slower ghost");
    const auto beforeFailure=bytes(path);fs::create_directory(path.wstring()+L".tmp");store.saveEnhancedBest(fixture::best(118000,false));
    check(!store.flush()&&store.failed()&&store.enhancedBestFailed()&&bytes(path)==beforeFailure,"Failed PB write damaged the last good file");
    store.saveEnhancedBest(fixture::best(119000,false));check(!store.flush(),"Blocked PB candidate unexpectedly saved");
    fs::remove(path.wstring()+L".tmp");store.retry();check(store.flush(),"PB retry failed");
    check(decodeEnhancedBest(store.readEnhancedBest(pb.key,fixture::catalog).data).time.elapsedMicroseconds==118000,"Slower submission discarded the fastest unsaved PB");
    store.saveEnhancedBest(fixture::best(117000,false));check(store.flush(),"Second PB generation failed");
    write(path,{1});read=store.readEnhancedBest(pb.key,fixture::catalog);check(read.state==CareerFileState::Recovered,"PB head did not recover");
    const auto recovered=decodeEnhancedBest(read.data);store.saveEnhancedBest(fixture::best(999000,false));check(store.flush(),"Recovery repair failed");
    check(decodeEnhancedBest(store.readEnhancedBest(pb.key,fixture::catalog).data).time.milliseconds()==recovered.time.milliseconds(),"Slower candidate replaced a recovered PB");
    bool damaged=false;for(const auto& entry:fs::directory_iterator(path.parent_path()))damaged|=entry.path().filename().wstring().find(L".damaged-")!=std::wstring::npos;check(damaged,"Unreadable PB was not preserved");
    const auto valid=bytes(path);auto future=valid;future[4]=99;write(path,future);store.saveEnhancedBest(fixture::best(110000,false));
    check(store.readEnhancedBest(pb.key,fixture::catalog).state==CareerFileState::Unsupported&&!store.flush()&&bytes(path)==future,"Unsupported PB was overwritten");
    write(path,valid);store.retry();check(store.flush(),"Supported PB did not resume after external repair");
    auto other=fixture::catalog;++other.rules;check(store.readEnhancedBest(pb.key,other).state==CareerFileState::Unsupported,"PB rules mismatch accepted");
    const auto current=bytes(path);auto exhausted=current;for(unsigned i=16;i<24;++i)exhausted[i]=255;write(path,exhausted);store.saveEnhancedBest(fixture::best(109000,false));
    check(!store.flush()&&bytes(path)==exhausted,"Generation overflow overwrote PB");write(path,current);store.retry();check(store.flush(),"PB did not recover from generation test");
    const auto fastest=bytes(path);write(path,held);write(path.wstring()+L".bak1",held);write(path.wstring()+L".bak2",fastest);
    fs::create_directory(path.wstring()+L".tmp");store.saveEnhancedBest(fixture::best(999000,false));check(!store.flush(),"PB interruption fixture did not fail publication");
    auto latest=store.readEnhancedBest(pb.key,fixture::catalog);check(latest.state==CareerFileState::Recovered&&decodeEnhancedBest(latest.data).time.milliseconds()==109,"Sole fastest backup was lost before publication");
    fs::remove(path.wstring()+L".tmp");store.retry();check(store.flush(),"Sole-backup recovery did not retry");
    auto foreign=fixture::best();foreign.key.career=std::string(32,'2');store.saveEnhancedBest(foreign);check(store.flush(),"Foreign-owner fixture did not save");
    const auto foreignFile=root/"ghosts-enhanced-v1"/foreign.key.career/"0-normal.scr-ebest";const auto foreignBytes=bytes(foreignFile);write(path,foreignBytes);
    store.saveEnhancedBest(fixture::best(108000,false));check(!store.flush()&&store.readEnhancedBest(pb.key,fixture::catalog).state==CareerFileState::Unsupported&&bytes(path)==foreignBytes,"Foreign-owner PB was replaced");
    check(files(root/"ghosts")==originalFiles&&files(root/"careers")==careerFiles,"Enhanced writes changed Original PB or career checkpoint");
    check(store.clearEnhancedBest(pb.key)&&store.readEnhancedBest(pb.key,fixture::catalog).state==CareerFileState::Missing,"Enhanced clear did not remove its generations");
    check(files(root/"ghosts")==originalFiles,"Enhanced clear removed Original PB");
    store.saveEnhancedBest(pb);check(store.flush(),"PB resave failed");check(store.removeCareer(pb.key.career),"Career deletion failed");
    check(store.readEnhancedBest(pb.key,fixture::catalog).state==CareerFileState::Missing&&store.readGhost(original.context.key,original.context.trackContentIdentity).state==CareerFileState::Missing,"Career deletion left a private timing category");
    check(store.readEnhancedBest(foreign.key,fixture::catalog).state==CareerFileState::Valid,"Career deletion removed another Enhanced owner");
}
void collection(const fs::path& base){
    EnhancedHallArchive archive(base,fixture::catalog);std::vector<std::shared_ptr<const EnhancedSubmission>> initial;
    for(unsigned n=1;n<=20;++n)initial.push_back(submission(n,120000+n*2000));archive.commit(initial);
    const auto path=archive.root()/"ghosts"/enhancedObjectName(initial.back()->attempt.id);const auto pinned=archive.readGhost(initial.back()->attempt);
    archive.commit({submission(21,118000)});check(fs::exists(path),"Enhanced backup lost referenced ghost");
    archive.commit({submission(22,116000)});check(fs::exists(path),"Second Enhanced backup lost referenced ghost");
    const auto unrelated=archive.root()/"ghosts"/(std::string(32,'f')+".scr-hghost");write(unrelated,{1,2,3});
    archive.commit({submission(23,114000)});check(!fs::exists(path)&&fs::exists(unrelated)&&pinned.ghost->recording.samples.size()>=2,"Enhanced GC lost isolation or pinned data");
}
void shared(const fs::path& originalRoot){
    SharedHallArchive original(originalRoot,old::catalog);auto a=old::attempt(1);auto g=old::ghost(a);original.commit({std::make_shared<const HallSubmission>(makeHallSubmission(a,&g))});
    const auto originalFiles=files(originalRoot);EnhancedHallArchive enhanced(originalRoot,fixture::catalog);
    check(enhanced.root()==enhancedHallRoot(originalRoot)&&enhanced.root().parent_path()==originalRoot.parent_path(),"Enhanced root is not a sibling");
    check(enhancedHallRoot(originalRoot/"")==enhanced.root(),"Trailing path separator changed the namespace");
    check(enhanced.read().state==HallArchiveState::Missing&&enhanced.read().hall->entries({}).empty(),"Original Hall imported as Enhanced");
    auto first=submission(1);check(enhanced.commit({first})==1&&enhanced.readGhost(first->attempt).state==HallGhostState::Valid,"Enhanced ghost publication failed");
    check(enhanced.commit({first})==1,"Retry rewrote a generation");
    check(sharedHallUsersAccess(enhanced.root()/"ghosts"/enhancedObjectName(first->attempt.id)),"Enhanced ghost did not inherit shared access");
    for(unsigned n=2;n<=4;++n)enhanced.commit({submission(n,120000+n*2000)});
    const auto head=enhanced.root()/"index.scr-ehall";write(head,{1});check(enhanced.read().state==HallArchiveState::Recovered,"Enhanced head did not recover");
    enhanced.commit({submission(5,130000)});check(enhanced.read().state==HallArchiveState::Valid,"Recovered Hall did not publish a new head");
    const auto object=enhanced.root()/"ghosts"/enhancedObjectName(first->attempt.id);fs::remove(object);
    check(enhanced.readGhost(first->attempt).state==HallGhostState::Missing,"Missing Enhanced ghost looked available");enhanced.commit({first});check(enhanced.readGhost(first->attempt).state==HallGhostState::Valid,"Retry did not repair Enhanced ghost");
    const auto valid=bytes(head);auto future=valid;future[4]=99;write(head,future);rejected([&]{enhanced.commit({submission(6)});});check(bytes(head)==future&&enhanced.read().state==HallArchiveState::Unsupported,"Unsupported Enhanced Hall overwritten");write(head,valid);
    auto mismatch=fixture::catalog;++mismatch.rules;EnhancedHallArchive other(originalRoot,mismatch);check(other.read().state==HallArchiveState::Unsupported,"Enhanced Hall rules mismatch accepted");
    check(files(originalRoot)==originalFiles,"Enhanced operations changed Original archive, objects or backups");
    const auto run=fixture::id(51);check(enhancedCompletionId(run,1,HallRecordKind::Lap)!=hallCompletionId(run,1,HallRecordKind::Lap)&&enhancedCompletionId(run,1,HallRecordKind::Lap)!=enhancedCompletionId(run,1,HallRecordKind::Lap,2),"Attempt identity does not distinguish timing methods");
}
void interrupted(const fs::path& root){
    for(unsigned point=0;point<=unsigned(HallWriteStage::Collected);++point){const auto base=root/std::to_string(point);EnhancedHallArchive existing(base,fixture::catalog);
        for(unsigned n=1;n<=3;++n)existing.commit({submission(n,120000+n*2000)});
        bool hit=false;EnhancedHallArchive writer(base,fixture::catalog,[&](auto stage){if(unsigned(stage)==point&&!hit){hit=true;throw std::runtime_error("Injected interruption");}});
        rejected([&]{writer.commit({submission(4,128000)});});check(hit,"Missing transaction interruption point");
        check(existing.read().hall&&existing.read().hall->entries({}).size()>=3,"Interruption lost committed records");existing.commit({submission(4,128000)});
        check(existing.read().hall->entries({}).size()==4,"Interrupted retry lost or duplicated record");
        for(const auto& entry:existing.read().hall->entries({}))check(existing.readGhost(entry.attempt).state==HallGhostState::Valid,"Interrupted publication left unusable ghost");
    }
}
void workers(const fs::path& root){
    EnhancedHallStore a(root,fixture::catalog),b(root,fixture::catalog);
    auto task=[&](EnhancedHallStore& store,unsigned parity){for(unsigned n=parity;n<=20;n+=2)check(store.submit(*submission(n,120000+n*2000))==EnhancedSubmitResult::Queued,"Worker rejected a candidate");check(store.flush(),"Concurrent worker failed");};
    auto worker=std::async(std::launch::async,[&]{task(a,1);});task(b,2);worker.get();
    check(a.read().hall->entries({}).size()==20,"Concurrent writers lost updates");
    EnhancedHallStore tiny(root/"limited",fixture::catalog,{},1);check(tiny.submit(*submission(33))==EnhancedSubmitResult::Full,"Pending backpressure missing");
    EnhancedHallStore allocations(root/"allocations",fixture::catalog,{},1024*1024);auto reserved=*submission(35);reserved.ghost.reserve(2*1024*1024);
    check(allocations.submit(std::move(reserved))==EnhancedSubmitResult::Full,"Encoded allocation was charged only by payload size");
    auto invalid=*submission(34);invalid.attempt.time.confidence=scr::timing::Reason::missingPrevious;check(a.submit(invalid)==EnhancedSubmitResult::Ineligible,"Uncertain candidate reached the writer");
    const auto failedRoot=root/"failed";EnhancedHallArchive baseline(failedRoot,fixture::catalog);baseline.commit({submission(41)});const auto head=baseline.root()/"index.scr-ehall";
    const auto valid=bytes(head);auto future=valid;future[4]=99;write(head,future);EnhancedHallStore failed(failedRoot,fixture::catalog);
    check(failed.submit(*submission(42,118000))==EnhancedSubmitResult::Queued&&!failed.flush()&&failed.failed(),"Failed writer discarded its pending attempt");
    check(bytes(head)==future,"Failed writer overwrote unsupported data");write(head,valid);failed.retry();check(failed.flush()&&failed.read().hall->entries({}).size()==2,"Worker retry lost its attempt");
}
}
int main(int argc,char** argv)try{check(argc==2,"Usage: enhanced_store_tests output");const fs::path root=argv[1];fs::create_directories(root);
    privateBests(root/"private");shared(root/"shared");collection(root/"collection");interrupted(root/"interruptions");workers(root/"workers");
    std::ofstream report(root/"results.json");report<<"{\"status\":\"passed\",\"checks\":"<<checks.load()<<",\"timing_namespaces_isolated\":true,\"private_recovery_and_ties\":true,\"shared_recovery_points\":7,\"concurrent_writers\":2}\n";
    std::cout<<"Enhanced storage: "<<checks.load()<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
