#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "session/native_game_session.h"
#include "platform/windows/shared_hall_storage.h"
#include "persistence/ghost_lap_codec.h"
#include "ghost_lap_fixture.h"
#include "state/replay_io.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>

namespace {
using namespace scr;using namespace scr::runtime;namespace fs=std::filesystem;namespace replay=scr::state::replay;
unsigned checks{},freshContexts{},capturedLaps{},capturedRaces{},failureRoutes{},viewMask{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void tap(NativeGameSession& g,unsigned controls=16,unsigned number=0,bool back=false){NativeGameInput in;in.menu={std::uint8_t(controls),std::uint8_t(number),back};g.update(0,{});g.update(0,in);g.update(0,{});}
void waitHall(NativeGameSession& g){
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(std::chrono::steady_clock::now()<until){if(g.page()==NativeMenuPage::HallOfFame&&!g.transitioning()&&g.hallBrowser()&&!g.hallBrowser()->busy())return;g.update(.02,{});Sleep(1);}
    throw std::runtime_error("Hall continuation did not settle");
}
std::unique_ptr<NativeGameSession> career(const fs::path& pack,const fs::path& root,const fs::path& shared,bool create=true){
    auto g=std::make_unique<NativeGameSession>(pack,fs::path{});g->enableCareers(root,shared);g->setNavigation(NativeMenuNavigation::Responsive);g->update(0,{});tap(*g);tap(*g);
    if(create){for(unsigned char c:std::string("CRAIG")){NativeGameInput in;in.rawKeys.push_back({*nativeAmigaRawKey(c,0,false),true});g->update(0,in);in.rawKeys[0].pressed=false;g->update(0,in);}tap(*g);tap(*g);}
    check(g->activeCareer()&&g->page()==NativeMenuPage::Main,"Enhanced career did not open");return g;
}
void practice(NativeGameSession& g){
    unsigned ordinal=0;while(ordinal<8&&g.memory()[0x648c2+ordinal]!=0)++ordinal;check(ordinal<8,"Missing track zero");
    tap(g,0,2);tap(g);if(g.page()==NativeMenuPage::Division){tap(g,0,1u<<(3-ordinal/2));tap(g);}
    tap(g,0,1u<<(ordinal%2));tap(g);tap(g);check(g.page()==NativeMenuPage::Race&&g.practiceGhostActive(),"Enhanced Practice did not start");
}
void handoff(NativeGameSession& g,const NativeMemory& captured,const std::vector<std::uint8_t>& payload,std::shared_ptr<const PinnedEnhancedHallGhost> target={}){
    auto seed=std::make_unique<NativeMemory>(captured);std::copy_n(g.memory().begin()+0x1ecab,256,seed->begin()+0x1ecab);(*seed)[0x5eb79]=11;(*seed)[0x5eb76]=0;
    auto& scene=const_cast<RaceScene&>(g.race());scene.loadNativeState(*seed,payload,{});
    const GhostLapKey key{g.activeCareer()->id,0,false};scene.recordPersonalBestLaps(const_cast<PersonalBestLaps&>(g.personalBestLaps()),{key,GhostLapSource::Practice,ghostTrackContentIdentity(payload)});
    scene.recordEnhancedPersonalBestLaps(*const_cast<EnhancedPersonalBests*>(g.enhancedPersonalBests()),{key.career,0,false},std::move(target));
    scene.recordHallAttempts(newHallRunId(),false);scene.setGhostTimingCategory(g.effectiveTimingCategory());
}
void drive(NativeGameSession& g,const std::vector<replay::ReplayInput>& plan,unsigned laps){
    for(std::size_t n=g.race().liveTicks();n<plan.size()&&!g.raceEnded()&&g.race().lapRecordingStatistics()->completions<laps;++n){NativeGameInput in;in.driveControls=plan[n].controls;in.quit=plan[n].quit;g.update(.12,in);}
    check(g.race().lapRecordingStatistics()->completions>=laps,"Authored controls did not complete requested Enhanced laps");
}
template<class Read>unsigned entries(const Read& r,HallRecordKind kind=HallRecordKind::Lap){check(bool(r.hall),"Missing category snapshot");return unsigned(r.hall->entries({0,HallLeague::Normal,kind}).size());}
void fresh(const fs::path& pack){
    for(bool super:{false,true})for(unsigned track=0;track<8;++track){auto seed=startNativeGame(pack);(*seed.memory)[0x1c9d0]=super?11:0;initializeNativeTrack(*seed.memory,track);
        if(state::drawBridge::animate(*seed.memory).geometryUpdated)rebuildNativeTrackGeometryCache(*seed.memory);initializeNativeRace(*seed.memory);
        auto scene=std::make_unique<RaceScene>(),baseline=std::make_unique<RaceScene>();scene->loadNativeState(*seed.memory,seed.payload,{});baseline->loadNativeState(*seed.memory,seed.payload,{});
        PersonalBestLaps original;EnhancedPersonalBests enhanced({ghostTrackContentIdentity(seed.payload),1,1});const GhostLapKey key{std::string(32,'1'),track,super};
        check(scene->recordPersonalBestLaps(original,{key,GhostLapSource::Practice})&&scene->recordEnhancedPersonalBestLaps(enhanced,{key.career,track,super}),"Fresh category binding failed");scene->recordHallAttempts(newHallRunId(),false);
        for(unsigned n=0;n<512;++n){scene->setGhostTimingCategory(n%2?TimingCategory::Original:TimingCategory::Enhanced);scene->stepLive(1);baseline->stepLive(1);
            check(scene->frames().back().pose.worldPosition==baseline->frames().back().pose.worldPosition,"Category selection changed native movement");
            check(scene->takeHallCompletions().empty()&&scene->takeEnhancedHallCompletions().empty(),"Partial fresh drive minted a full lap");}
        scene->quitLive();baseline->quitLive();auto a=std::make_unique<NativeMemory>(),b=std::make_unique<NativeMemory>();scene->copyLiveExit(*a);baseline->copyLiveExit(*b);check(*a==*b,"Dual capture changed full fresh memory/RNG");++freshContexts;
    }
}
void capturedModes(const NativeMemory& captured,const std::vector<std::uint8_t>& payload,const std::vector<replay::ReplayInput>& plan,const fs::path& root){
    for(unsigned mode=0;mode<3;++mode){auto seed=std::make_unique<NativeMemory>(captured);(*seed)[0x5eb79]=std::uint8_t(mode==2?10:11);
        std::fill_n(seed->begin()+0x1ecab+16*(*seed)[0x5eb79],12,' ');const std::string name=mode==2?"Second":"First";std::copy(name.begin(),name.end(),seed->begin()+0x1ecab+16*(*seed)[0x5eb79]);
        for(unsigned base:{0x1c908u,0x1c920u,0x1c938u})(*seed)[base+12+(*seed)[0x5eb79]]=0;
        if(mode){(*seed)[0x1ca22]=0x80;(*seed)[0x5eb76]=mode==2?2:0;if(mode==2)(*seed)[0x1ca35]=1;
            auto data=std::make_unique<OpponentDrivingData>(state::opponent::readData(*seed));auto opponent=state::opponent::readState(*seed);
            opponent.segment=std::uint8_t(((*seed)[0x1ca32]+1)%(*seed)[0x1ca1a]);opponent.subSegment=0;opponent.lateral=128;initializeOpponentCoordinates(*data,state::opponent::readInputs(*seed),opponent);state::opponent::writeState(*seed,opponent);}
        const auto content=ghostTrackContentIdentity(payload);const auto path=root/std::to_string(mode);const EnhancedCatalog catalog{content,1,1};
        SharedHallStore originalStore(path,{content,1},{},hallCapturePendingMaximumBytes);EnhancedHallStore enhancedStore(path,catalog,{},enhancedCapturePendingMaximumBytes);
        PersonalBestLaps original;EnhancedPersonalBests enhanced(catalog);auto scene=std::make_unique<RaceScene>(),baseline=std::make_unique<RaceScene>();scene->loadNativeState(*seed,payload,{});baseline->loadNativeState(*seed,payload,{});
        const GhostLapKey key{std::string(32,'1'),0,false};if(mode!=2){scene->recordPersonalBestLaps(original,{key,mode?GhostLapSource::League:GhostLapSource::Practice});scene->recordEnhancedPersonalBestLaps(enhanced,{key.career,0,false});}
        else check(!scene->recordEnhancedPersonalBestLaps(enhanced,{key.career,0,false}),"Multiplayer acquired a private Enhanced owner");
        scene->recordHallAttempts(newHallRunId(),mode!=0);unsigned laps=0,races=0;
        for(const auto input:plan){if(scene->liveTerminal())break;scene->setGhostTimingCategory(scene->liveTicks()%2?TimingCategory::Original:TimingCategory::Enhanced);scene->stepLive(input.controls,input.quit);baseline->stepLive(input.controls,input.quit);
            for(const auto& s:scene->takeHallCompletions())check(originalStore.submit(s)!=HallSubmitResult::Full,"Original live allocation bound failed");
            for(const auto& s:scene->takeEnhancedHallCompletions()){check(s.attempt.name==name,"Enhanced used a different native driver");if(s.attempt.board.kind==HallRecordKind::Lap)++laps;else++races;
                check(enhancedStore.submit(s)!=EnhancedSubmitResult::Full,"Enhanced live allocation bound failed");}
            check(scene->takeEnhancedHallCompletions().empty(),"Draining Enhanced delivery repeated an attempt");
        }
        check(laps==3&&races==(mode?1u:0u)&&originalStore.flush()&&enhancedStore.flush(),"Dual route eligibility or persistence differs");capturedLaps+=laps;capturedRaces+=races;
        auto old=originalStore.read();auto saved=enhancedStore.read();check(entries(old)==3&&entries(saved)==3&&entries(saved,HallRecordKind::Race)==races,"Independent archives lost completions");
        std::set<HallAttemptId> ids;for(const auto& e:old.hall->entries({}))ids.insert(e.attempt.id);
        for(const auto& e:saved.hall->entries({})){check(!ids.count(e.attempt.id)&&enhancedStore.readGhost(e.attempt).state==HallGhostState::Valid,"Enhanced identity collided or recording failed validation");}
        if(mode==2)check(original.size()==0&&enhanced.size()==0,"Multiplayer retained a private PB");else check(enhanced.best({key.career,0,false})&&original.best(key),"Eligible route lost a category PB");
        scene->quitLive();baseline->quitLive();auto a=std::make_unique<NativeMemory>(),b=std::make_unique<NativeMemory>();scene->copyLiveExit(*a);baseline->copyLiveExit(*b);check(*a==*b,"Dual completion delivery changed full native memory/RNG");
    }
}
void privateSession(const fs::path& pack,const NativeMemory& captured,const std::vector<std::uint8_t>& payload,const std::vector<replay::ReplayInput>& plan,const fs::path& root){
    const auto data=root/"private",shared=root/"shared";auto g=career(pack,data,shared);const auto id=g->activeCareer()->id;const EnhancedBestKey key{id,0,false};const EnhancedCatalog catalog{ghostTrackContentIdentity(payload),1,1};
    auto old=scr::test::ghost::lap(id);old.context.trackContentIdentity=catalog.trackContent;old.hundredths=1;old.completedTime={0,0,1};{NativeCareerStore store(data);store.saveGhost(old);check(store.flush(),"Cannot seed Original PB");}
    g->setTimingCategory(TimingCategory::Enhanced);practice(*g);handoff(*g,captured,payload);drive(*g,plan,1);check(g->flushRecords(),"Dual session did not save");
    check(g->personalBestLaps().best({id,0,false})->hundredths==1&&g->enhancedPersonalBests()->best(key),"Private category improvements were coupled");
    const auto best=g->enhancedPersonalBests()->best(key);check(best->recording&&g->race().lapGhostPlayback(1)->enhancedTarget==best->recording,"Enhanced session target differs from its PB");
    const auto before=g->race().frames().back().pose.worldPosition;const auto ticks=g->race().liveTicks();
    for(auto category:{TimingCategory::Original,TimingCategory::Enhanced,TimingCategory::Original,TimingCategory::Enhanced}){g->setTimingCategory(category);(void)g->render(true,true);}
    check(g->race().frames().back().pose.worldPosition==before&&g->race().liveTicks()==ticks&&entries(g->enhancedHall())==1,"Category/render queries changed state or minted attempts");
    check(g->clearPracticeGhost()&&!g->enhancedPersonalBests()->best(key)&&g->personalBestLaps().best({id,0,false}),"Enhanced clear removed another category");
    drive(*g,plan,2);check(g->flushRecords()&&!g->enhancedPersonalBests()->best(key)&&entries(g->enhancedHall())==2,"Cleared partial lap restored PB or stopped Hall delivery");
    drive(*g,plan,3);check(g->flushRecords()&&g->enhancedPersonalBests()->best(key)&&entries(g->sharedHall())==3&&entries(g->enhancedHall())==3,"Capture did not resume after clear");
    g->setTimingCategory(TimingCategory::Original);check(g->clearPracticeGhost()&&g->enhancedPersonalBests()->best(key),"Original clear removed Enhanced PB");
    check(g->requestClose(),"Dual session did not close");g.reset();
    g=career(pack,data,shared,false);g->setTimingCategory(TimingCategory::Enhanced);practice(*g);check(g->enhancedPersonalBests()->best(key)&&!g->personalBestLaps().best({id,0,false}),"Reload did not restore categories independently");
    check(g->flushRecords()&&entries(g->enhancedHall())==3,"Restoring saved records minted Enhanced attempts");
    auto timeOnly=*g->enhancedPersonalBests()->best(key);timeOnly.recording.reset();timeOnly.time.elapsedMicroseconds-=1000000;timeOnly.time.scoredMicroseconds-=1000000;
    check(g->requestClose(),"Reload session did not close");g.reset();{NativeCareerStore store(data);store.saveEnhancedBest(timeOnly);check(store.flush(),"Time-only PB did not save");}
    g=career(pack,data,shared,false);g->setTimingCategory(TimingCategory::Enhanced);practice(*g);handoff(*g,captured,payload);
    while(g->race().lapRecordingStatistics()->starts==0){const auto input=plan.at(g->race().liveTicks());NativeGameInput in;in.driveControls=input.controls;g->update(.12,in);}
    check(!g->enhancedPersonalBests()->best(key)->recording&&!g->race().lapGhostPlayback(1)&&g->practiceGhostTime()==formatEnhancedTime(timeOnly.time),"Time-only PB restored a slower ghost");check(g->requestClose(),"Time-only session did not close");
}
struct HeldFile {HANDLE value{INVALID_HANDLE_VALUE};void release(){if(value!=INVALID_HANDLE_VALUE){CloseHandle(value);value=INVALID_HANDLE_VALUE;}}~HeldFile(){release();}};
void failures(const fs::path& pack,const NativeMemory& captured,const std::vector<std::uint8_t>& payload,const std::vector<replay::ReplayInput>& plan,const fs::path& root){
    for(unsigned route=0;route<4;++route){const auto data=root/std::to_string(route)/"private",shared=root/std::to_string(route)/"shared";auto g=career(pack,data,shared);const auto id=g->activeCareer()->id;practice(*g);handoff(*g,captured,payload);check(g->flushRecords(),"Initial category check failed");
        fs::path blocked;if(route==0)blocked=shared/"index.scr-hall.tmp";if(route==1)blocked=enhancedHallRoot(shared)/"index.scr-ehall.tmp";
        if(route==2)blocked=data/"ghosts"/id/"0-normal.scr-ghost.tmp";if(route==3)blocked=data/"ghosts-enhanced-v1"/id/"0-normal.scr-ebest.tmp";
        fs::create_directories(blocked.parent_path());HeldFile held;held.value=CreateFileW(blocked.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);check(held.value!=INVALID_HANDLE_VALUE,"Cannot lock category publication fixture");
        drive(*g,plan,1);check(!g->flushRecords()&&g->page()==NativeMenuPage::Race,"Category write failure was hidden or interrupted driving");
        if(route==0)check(entries(g->enhancedHall())==1,"Original Hall failure blocked Enhanced publication");if(route==1)check(entries(g->sharedHall())==1,"Enhanced Hall failure blocked Original publication");
        if(route>=2){NativeCareerStore disk(data);if(route==2)check(disk.readEnhancedBest({id,0,false},{ghostTrackContentIdentity(payload),1,1}).state==CareerFileState::Valid,"Original PB failure blocked Enhanced PB");else check(disk.readGhost({id,0,false},ghostTrackContentIdentity(payload)).state==CareerFileState::Valid,"Enhanced PB failure blocked Original PB");}
        check(!g->requestClose()&&g->closePending(),"Failed category write allowed silent departure");held.release();tap(*g);check(g->closeReady(),"Category retry did not finish safe close");
        check(entries(g->sharedHall())==1&&entries(g->enhancedHall())==1,"Retry lost or duplicated a completion");++failureRoutes;
    }
}
void challenge(const fs::path& pack,const NativeMemory& captured,const std::vector<std::uint8_t>& payload,const std::vector<replay::ReplayInput>& plan,const fs::path& root){
    const auto data=root/"private",shared=root/"shared";auto g=career(pack,data,shared);const EnhancedCatalog catalog{ghostTrackContentIdentity(payload),1,1};
    // Use a freshly driven recording from the route proof, not a saved Original duration.
    EnhancedHallArchive source(root.parent_path()/"modes"/"0",catalog);const auto a=source.read().hall->entries({}).front().attempt;const auto loaded=source.readGhost(a);check(loaded.state==HallGhostState::Valid,"Driven Enhanced rival unavailable");
    const auto pinned=pinEnhancedHallGhost(a,*loaded.ghost,catalog);tap(*g,0,1);tap(*g);waitHall(*g);const auto memory=std::make_unique<NativeMemory>(g->memory());
    EnhancedHallArchive destination(shared,catalog);auto publish=a;publish.ghost.reset();destination.commit({std::make_shared<const EnhancedSubmission>(makeEnhancedSubmission(publish,loaded.ghost.get()))});
    check(g->flushRecords(),"Challenge menu checkpoint did not flush");NativeCareerStore disk(data);CareerBytes checkpoint;
    g->startEnhancedHallChallenge(pinned);g->update(0,{});check(g->page()==NativeMenuPage::Preview&&g->effectiveTimingCategory()==TimingCategory::Enhanced,"Enhanced challenge did not pin its category");
    g->setTimingCategory(TimingCategory::Original);tap(*g,0,0,true);waitHall(*g);check(g->memory()==*memory&&g->effectiveTimingCategory()==TimingCategory::Original,"Preview cancel changed career/RNG or retained challenge category");
    g->requestEnhancedHallChallenge(a);NativeGameInput cancel;cancel.menu.back=true;g->update(0,cancel);g->update(0,{});
    check(g->page()==NativeMenuPage::Main&&g->effectiveTimingCategory()==TimingCategory::Original,"Cancel lost to asynchronous Enhanced resolution");
    tap(*g,0,1);tap(*g);waitHall(*g);*memory=g->memory();
    check(g->flushRecords(),"Cancelled challenge checkpoint did not flush");decodeCareerInfo(disk.readCareer(g->activeCareer()->id).data,&checkpoint);
    auto missing=a;missing.id[15]^=1;g->requestEnhancedHallChallenge(missing);waitHall(*g);check(g->memory()==*memory&&g->effectiveTimingCategory()==TimingCategory::Original,"Missing asynchronous rival changed the continuation");
    g->requestEnhancedHallChallenge(a);const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(g->page()==NativeMenuPage::HallOfFame&&std::chrono::steady_clock::now()<until){g->update(.02,{});Sleep(1);}g->update(0,{});tap(*g);
    check(g->page()==NativeMenuPage::Race,"Asynchronous Enhanced challenge did not enter Practice");
    std::shared_ptr<const PinnedEnhancedHallGhost> resolved;
    for(unsigned n=0;n<512&&!resolved;++n){NativeGameInput input;input.driveControls=1;g->update(.12,input);const auto playback=g->race().lapGhostPlayback(1);if(playback)resolved=playback->enhancedHallTarget;}
    check(resolved&&resolved->attempt.id==pinned->attempt.id,"Off-thread Enhanced target was not bound to the live scene");handoff(*g,captured,payload,resolved);drive(*g,plan,1);
    check(g->race().lapGhostPlayback(1)->enhancedHallTarget==resolved&&g->effectiveTimingCategory()==TimingCategory::Enhanced,"Challenge target changed after PB capture");
    auto& scene=const_cast<RaceScene&>(g->race());
    for(unsigned n=0;n<300&&viewMask!=15;++n){
        for(auto view:{NativeView::Original,NativeView::Extended,NativeView::Widescreen,NativeView::Ultrawide})if(!(viewMask&(1u<<unsigned(view)))){
            const auto hidden=scene.renderLive(1,false,true,view);const auto shown=scene.renderPracticeGhost(1,true,view);if(hidden!=shown)viewMask|=1u<<unsigned(view);}
        if(viewMask!=15){const auto input=plan.at(scene.liveTicks());NativeGameInput in;in.driveControls=input.controls;in.quit=input.quit;g->update(.12,in);}
    }
    check(viewMask==15,"Driven Enhanced rival was not visible in every supported view");
    auto seed=std::make_unique<NativeMemory>(captured);std::copy_n(g->memory().begin()+0x1ecab,256,seed->begin()+0x1ecab);(*seed)[0x5eb79]=11;(*seed)[0x5eb76]=0;
    auto baseline=std::make_unique<RaceScene>();baseline->loadNativeState(*seed,payload,{});for(unsigned n=0;n<g->race().liveTicks();++n)baseline->stepLive(plan[n].controls,plan[n].quit);
    check(g->clearPracticeGhost()&&g->race().lapGhostPlayback(1)->enhancedHallTarget==resolved,"Clearing Enhanced PB removed the rival");
    check(g->requestClose(),"Enhanced challenge could not close safely");CareerBytes saved;decodeCareerInfo(disk.readCareer(g->activeCareer()->id).data,&saved);check(saved==checkpoint,"Closing challenge persisted temporary world or RNG");
    scene.quitLive();baseline->quitLive();auto actual=std::make_unique<NativeMemory>(),expected=std::make_unique<NativeMemory>();scene.copyLiveExit(*actual);baseline->copyLiveExit(*expected);check(*actual==*expected,"Enhanced ghost rendering or clearing changed simulation/RNG");
    NativeGameInput quit;quit.leaveRace=true;g->update(0,quit);waitHall(*g);check(g->memory()==*memory&&g->effectiveTimingCategory()==TimingCategory::Original,"Leaving Enhanced challenge changed career continuation or preference");
}
}
int main(int argc,char** argv)try{
    check(argc==6,"Usage: native_enhanced_session_tests pack output chip payload inputs");for(unsigned n:{1u,3u,4u,5u})if(!fs::exists(argv[n])){std::cout<<"SKIP: missing local test input\n";return 0;}
    const fs::path root=argv[2];fs::create_directories(root);const auto captured=replay::loadCapture(argv[3]);std::ifstream file(argv[4],std::ios::binary);const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),{}};const auto plan=replay::loadInputs(argv[5]);
    fresh(argv[1]);capturedModes(*captured,payload,plan,root/"modes");privateSession(argv[1],*captured,payload,plan,root/"session");failures(argv[1],*captured,payload,plan,root/"failures");challenge(argv[1],*captured,payload,plan,root/"challenge");
    std::ofstream report(root/"results.json");report<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"fresh_contexts\":"<<freshContexts<<",\"captured_laps\":"<<capturedLaps<<",\"competitive_races\":"<<capturedRaces<<",\"failure_routes\":"<<failureRoutes<<",\"view_mask\":"<<viewMask<<",\"independent_categories\":true,\"full_memory_and_rng_isolation\":true,\"pinned_challenge_and_continuation\":true,\"time_only_restore\":true}\n";
    std::cout<<"Enhanced sessions: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
