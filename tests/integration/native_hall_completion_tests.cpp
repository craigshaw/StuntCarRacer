#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "session/native_game_session.h"
#include "platform/windows/shared_hall_storage.h"
#include "persistence/ghost_lap_codec.h"
#include "ghost_lap_fixture.h"
#include "state/replay_io.h"
#include <fstream>
#include <iostream>
#include <set>

namespace {
using namespace scr;using namespace scr::runtime;namespace fs=std::filesystem;namespace replay=scr::state::replay;
unsigned checks{};
void require(bool v,const char* m){++checks;if(!v)throw std::runtime_error(m);}
void tap(NativeGameSession& g,unsigned controls=16,unsigned number=0){NativeGameInput in;in.menu={std::uint8_t(controls),std::uint8_t(number)};g.update(0,{});g.update(0,in);g.update(0,{});}
void text(NativeGameSession& g,const std::string& name){for(unsigned char c:name){NativeGameInput in;in.rawKeys.push_back({*nativeAmigaRawKey(c,0,false),true});g.update(0,in);in.rawKeys[0].pressed=false;g.update(0,in);}}
std::unique_ptr<NativeGameSession> career(const fs::path& pack,const fs::path& root,const fs::path& shared,bool create){
    auto g=std::make_unique<NativeGameSession>(pack,fs::path{});g->enableCareers(root,shared);g->setNavigation(NativeMenuNavigation::Responsive);g->update(0,{});
    tap(*g);tap(*g);if(create){text(*g,"CRAIG");tap(*g);tap(*g);}require(g->activeCareer()&&g->page()==NativeMenuPage::Main,"Hall career did not open");return g;
}
void practice(NativeGameSession& g){
    unsigned ordinal=0;while(ordinal<8&&g.memory()[0x648c2+ordinal]!=0)++ordinal;require(ordinal<8,"Missing track zero");
    tap(g,0,2);tap(g);if(g.page()==NativeMenuPage::Division){tap(g,0,1u<<(3-ordinal/2));tap(g);}
    tap(g,0,1u<<(ordinal%2));tap(g);tap(g);require(g.page()==NativeMenuPage::Race&&g.practiceGhostActive(),"Hall Practice did not start");
}
void handoff(NativeGameSession& g,const NativeMemory& captured,const std::vector<std::uint8_t>& payload,bool pb=true){
    auto seed=std::make_unique<NativeMemory>(captured);std::copy_n(g.memory().begin()+0x1ecab,256,seed->begin()+0x1ecab);
    (*seed)[0x5eb79]=11;(*seed)[0x5eb76]=0;
    auto& scene=const_cast<RaceScene&>(g.race());scene.loadNativeState(*seed,payload,{});
    if(pb)scene.recordPersonalBestLaps(const_cast<PersonalBestLaps&>(g.personalBestLaps()),{{g.activeCareer()->id,0,false},GhostLapSource::Practice});
    scene.recordHallAttempts(newHallRunId(),false);
}
void drive(NativeGameSession& g,const std::vector<replay::ReplayInput>& plan,unsigned completedLaps=3){
    for(const auto i:plan){if(g.raceEnded())break;NativeGameInput in;in.driveControls=i.controls;in.quit=i.quit;g.update(.12,in);
        if(g.race().lapRecordingStatistics()->completions>=completedLaps)break;}
    require(g.race().lapRecordingStatistics()->completions>=completedLaps,"Authored controls did not complete requested laps");
}
unsigned entries(const HallArchiveRead& r,HallRecordKind kind=HallRecordKind::Lap){require(bool(r.hall),"Missing shared Hall snapshot");return unsigned(r.hall->entries({0,HallLeague::Normal,kind}).size());}
void legacy(const fs::path& pack,const fs::path& output){
    for(unsigned variant=0;variant<3;++variant){const auto root=output/std::to_string(variant);fs::create_directories(root);
        {NativeCareerStore old(root);NativeHallBanks banks{};for(unsigned n=0;n<512;n+=16){std::fill_n(banks.begin()+n,12,'A');banks[n+13]=1;}old.saveHall(banks);require(old.flush(),"Cannot seed legacy Hall");}
        if(variant==1){std::ofstream(root/"hall.scr-hall",std::ios::trunc)<<"corrupt";std::ofstream(root/"hall.scr-hall.bak1")<<"corrupt";}
        if(variant==2){std::fstream file(root/"hall.scr-hall",std::ios::binary|std::ios::in|std::ios::out);file.seekp(4);file.put(99);}
        const auto before=fs::file_size(root/"hall.scr-hall");auto g=career(pack,root,root/"shared-hall",true);
        require(g->flushRecords()&&entries(g->sharedHall())==0,"Legacy Hall seeded new archive or blocked careers");require(g->requestClose(),"Legacy files blocked closing");
        require(fs::file_size(root/"hall.scr-hall")==before,"Player rewrote legacy Hall");
    }
}
void sessions(const fs::path& pack,const fs::path& output,const NativeMemory& captured,const std::vector<std::uint8_t>& payload,const std::vector<replay::ReplayInput>& plan){
    const auto shared=output/"shared",root=output/"private-a";auto g=career(pack,root,shared,true);const auto id=g->activeCareer()->id;
    auto old=scr::test::ghost::lap(id);old.context.trackContentIdentity=ghostTrackContentIdentity(payload);old.completedTime={0,0,1};old.hundredths=1;
    {NativeCareerStore store(root);store.saveGhost(old);require(store.flush(),"Cannot seed existing private PB");}
    practice(*g);require(g->flushRecords()&&entries(g->sharedHall())==0,"Loaded PB or preview created a Hall row");
    handoff(*g,captured,payload);drive(*g,plan);require(g->flushRecords(),"Session completion stream did not flush");
    const auto saved=g->sharedHall();require(entries(saved)==3&&entries(saved,HallRecordKind::Race)==0,"Practice failed to save every lap or saved a race total");
    require(g->personalBestLaps().best({id,0,false})->hundredths==1,"Hall completion replaced faster private PB");
    std::set<HallAttemptId> originalIds;for(const auto& e:saved.hall->entries({})){originalIds.insert(e.attempt.id);require(e.attempt.name=="craig"&&e.attempt.ghost.has_value(),"Session used the wrong driver or lost a recording");}
    const auto original=saved.hall->entries({}).front().attempt;SharedHallArchive archive(shared,{ghostTrackContentIdentity(payload),1});
    const auto pinned=archive.readGhost(original);require(pinned.state==HallGhostState::Valid,"Session ghost object unusable");require(g->requestClose(),"Session close lost pending completions");g.reset();
    g=career(pack,root,shared,false);require(g->flushRecords()&&entries(g->sharedHall())==3,"Reload/import replayed saved attempts");practice(*g);handoff(*g,captured,payload);drive(*g,plan,1);
    require(g->flushRecords()&&entries(g->sharedHall())==4,"A genuinely re-driven attempt reused an old ID");
    const auto rerun=g->sharedHall();unsigned newIds=0;for(const auto& e:rerun.hall->entries({}))newIds+=!originalIds.count(e.attempt.id);require(newIds==1,"Re-drive identity count differs");
    require(g->clearPracticeGhost(),"Private PB clear failed");require(entries(g->sharedHall())==4&&archive.readGhost(original).state==HallGhostState::Valid,"PB clear erased Hall ownership");
    // Clear occurs in the newly started second lap. Its Hall capture continues,
    // while that partial lap remains ineligible for private PB replacement.
    for(std::size_t n=g->race().liveTicks();n<plan.size()&&g->race().lapRecordingStatistics()->completions<2;++n){NativeGameInput in;in.driveControls=plan[n].controls;in.quit=plan[n].quit;g->update(.12,in);}
    require(g->flushRecords()&&entries(g->sharedHall())==5&&!g->personalBestLaps().best({id,0,false}),"PB clear interrupted Hall capture or saved cleared partial PB");
    require(g->requestClose(),"Cleared session did not flush");g.reset();
    {NativeCareerStore store(root);require(store.removeCareer(id),"Career deletion failed");}
    require(entries(archive.read())==5&&archive.readGhost(original).state==HallGhostState::Valid&&pinned.ghost->samples.size()>2,"Career deletion removed independently retained Hall ghosts");
    auto other=career(pack,output/"private-b",shared,true);require(other->flushRecords()&&entries(other->sharedHall())==5,"Separate private data root did not share installation Hall");require(other->requestClose(),"Second private root did not close");
}
void capturedRaces(const NativeMemory& captured,const std::vector<std::uint8_t>& payload,const std::vector<replay::ReplayInput>& plan,const fs::path& output){
    for(unsigned mode=0;mode<3;++mode){auto seed=std::make_unique<NativeMemory>(captured);(*seed)[0x5eb79]=std::uint8_t(mode==2?10:11);
        std::fill_n(seed->begin()+0x1ecab+16*(*seed)[0x5eb79],12,' ');const std::string name=mode==2?"Second":"First";std::copy(name.begin(),name.end(),seed->begin()+0x1ecab+16*(*seed)[0x5eb79]);
        for(unsigned base:{0x1c908u,0x1c920u,0x1c938u})(*seed)[base+12+(*seed)[0x5eb79]]=0;
        if(mode){(*seed)[0x1ca22]=0x80;(*seed)[0x5eb76]=mode==2?2:0;if(mode==2)(*seed)[0x1ca35]=1;
            auto data=std::make_unique<OpponentDrivingData>(state::opponent::readData(*seed));auto opponent=state::opponent::readState(*seed);
            opponent.segment=std::uint8_t(((*seed)[0x1ca32]+1)%(*seed)[0x1ca1a]);opponent.subSegment=0;opponent.lateral=128;
            initializeOpponentCoordinates(*data,state::opponent::readInputs(*seed),opponent);state::opponent::writeState(*seed,opponent);}
        auto scene=std::make_unique<RaceScene>(),baseline=std::make_unique<RaceScene>();scene->loadNativeState(*seed,payload,{});baseline->loadNativeState(*seed,payload,{});
        scene->recordHallAttempts(newHallRunId(),mode!=0);SharedHallStore store(output/std::to_string(mode),{ghostTrackContentIdentity(payload),1}, {},hallCapturePendingMaximumBytes);
        unsigned laps=0,races=0;
        for(const auto input:plan){if(scene->liveTerminal())break;scene->stepLive(input.controls,input.quit);baseline->stepLive(input.controls,input.quit);
            require(scene->frames().back().pose.worldPosition==baseline->frames().back().pose.worldPosition,"Capture changed driving position");
            for(const auto& s:scene->takeHallCompletions()){require(s.attempt.name==name,"Actual local driver name was replaced by active career");if(s.attempt.board.kind==HallRecordKind::Lap)++laps;else++races;require(store.submit(s)!=HallSubmitResult::Full,"Live bound rejected a capture");}
        }
        require(laps==3&&races==(mode?1u:0u)&&store.flush(),"Captured completion/race eligibility or persistence differs");
        const auto saved=store.read();require(entries(saved)==3&&entries(saved,HallRecordKind::Race)==races,"Store did not retain captured completions");
        for(const auto& e:saved.hall->entries({}))require(store.readGhost(e.attempt).state==HallGhostState::Valid,"Captured Hall ghost failed validation");
        scene->quitLive();baseline->quitLive();
        auto a=std::make_unique<NativeMemory>(),b=std::make_unique<NativeMemory>();scene->copyLiveExit(*a);baseline->copyLiveExit(*b);require(*a==*b,"Hall ingestion changed full native memory or RNG");
    }
}
void writeFailure(const fs::path& pack,const fs::path& root,const NativeMemory& captured,const std::vector<std::uint8_t>& payload,const std::vector<replay::ReplayInput>& plan){
    const auto shared=root/"shared";auto game=career(pack,root/"private",shared,true);practice(*game);handoff(*game,captured,payload);
    require(game->flushRecords(),"Initial store check failed");
    const auto handle=CreateFileW((shared/"index.scr-hall.tmp").c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(handle!=INVALID_HANDLE_VALUE,"Cannot lock Hall publication fixture");drive(*game,plan,1);
    require(!game->flushRecords()&&game->page()==NativeMenuPage::Race,"Hall failure was hidden or interrupted active driving");
    require(!game->requestClose()&&game->closePending(),"Failed Hall write allowed silent close");CloseHandle(handle);
    tap(*game);require(game->closeReady(),"Safe-boundary Hall retry did not complete close");game.reset();
    SharedHallArchive archive(shared,{ghostTrackContentIdentity(payload),1});const auto saved=archive.read();
    require(entries(saved)==1&&archive.readGhost(saved.hall->entries({}).front().attempt).state==HallGhostState::Valid,"Retry lost the original recording or duplicated its time");
}
void multiplayerBinding(const fs::path& pack,const fs::path& root){
    NativeGameSession g(pack,{});g.enableCareers(root);g.setNavigation(NativeMenuNavigation::Responsive);g.update(0,{});
    tap(g,0,2);tap(g);text(g,"FIRST");tap(g);tap(g);text(g,"SECOND");tap(g);tap(g,0,2);tap(g);tap(g);
    require(g.multiplayer()&&g.page()==NativeMenuPage::Main,"Local championship fixture did not reach main menu");
    tap(g,0,4);tap(g);tap(g);tap(g);
    require(g.page()==NativeMenuPage::Race&&g.race().lapRecordingStatistics()&&g.personalBestLaps().size()==0,"Local championship did not attach Hall-only capture");
    require(g.requestClose(),"Local championship capture did not close cleanly");
}
}
int main(int argc,char** argv)try{
    require(argc==6,"Usage: native_hall_completion_tests pack output chip payload inputs");
    for(unsigned n:{1u,3u,4u,5u})if(!fs::exists(argv[n])){std::cout<<"SKIP: missing local test input\n";return 0;}
    const fs::path output=argv[2];const auto seed=replay::loadCapture(argv[3]);std::ifstream file(argv[4],std::ios::binary);
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),{}};const auto plan=replay::loadInputs(argv[5]);
    legacy(argv[1],output/"legacy");sessions(argv[1],output/"sessions",*seed,payload,plan);capturedRaces(*seed,payload,plan,output/"replay");writeFailure(argv[1],output/"failure",*seed,payload,plan);multiplayerBinding(argv[1],output/"multiplayer-binding");
    std::ofstream report(output/"results.json");report<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"practice_laps\":3,\"competitive_laps\":3,\"multiplayer_laps\":3,\"competitive_races\":2,\"legacy_not_imported\":true,\"private_pb_independent\":true,\"full_memory_and_rng_isolation\":true,\"original_cpu_executed\":false}\n";
    std::cout<<"Native Hall completion integration passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
