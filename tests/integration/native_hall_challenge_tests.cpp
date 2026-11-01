#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "session/native_game_session.h"
#include "platform/windows/game_display.h"
#include "platform/windows/shared_hall_storage.h"
#include "hall_ghost_fixture.h"
#include "ghost_lap_fixture.h"
#include "state/replay_io.h"
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
using namespace scr;using namespace scr::runtime;namespace fs=std::filesystem;namespace fixture=scr::test::hall;namespace replay=scr::state::replay;
unsigned checks{},routes{},visible{},viewMask{};
void require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
void update(NativeGameSession& g,double elapsed=0,unsigned controls=0,unsigned numbers=0,bool back=false){NativeGameInput in;in.menu={std::uint8_t(controls),std::uint8_t(numbers),back};g.update(elapsed,in);}
void tap(NativeGameSession& g,unsigned controls=16,unsigned numbers=0,bool back=false){update(g);update(g,0,controls,numbers,back);update(g);}
void settle(NativeGameSession& g){for(unsigned n=0;n<12;++n)update(g,1);}
void wait(NativeGameSession& g,NativeMenuPage page){
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    do{if(g.page()==page&&!g.transitioning()&&(!g.hallBrowser()||!g.hallBrowser()->busy()))return;update(g,.02);Sleep(1);}while(std::chrono::steady_clock::now()<until);
    throw std::runtime_error("Hall challenge destination "+std::to_string(unsigned(page))+"; actual "+std::to_string(unsigned(g.page()))+"; track "+std::to_string(g.memory()[0x1ca33])+"; league "+std::to_string(g.memory()[0x1c9d0])+"; transition "+std::to_string(g.transitioning()));
}
std::unique_ptr<NativeGameSession> career(const fs::path& pack,const fs::path& root,const fs::path& shared,bool create=true){
    auto g=std::make_unique<NativeGameSession>(pack,fs::path{});g->enableCareers(root,shared);g->setNavigation(NativeMenuNavigation::Responsive);update(*g);tap(*g);tap(*g);
    if(create){for(unsigned char c:std::string("CHALLENGER")){NativeGameInput in;in.rawKeys.push_back({*nativeAmigaRawKey(c,0,false),true});g->update(0,in);in.rawKeys[0].pressed=false;g->update(0,in);}tap(*g);tap(*g);}
    require(g->activeCareer()&&g->page()==NativeMenuPage::Main,"Challenge career did not open");return g;
}
void open(NativeGameSession& g){settle(g);tap(g,0,1);settle(g);tap(g);wait(g,NativeMenuPage::HallOfFame);}
void board(NativeGameSession& g,unsigned track,unsigned league){
    tap(g,0,1);while(g.hallBrowser()->board().track!=track){tap(g,8);wait(g,NativeMenuPage::HallOfFame);}
    tap(g,0,2);if(unsigned(g.hallBrowser()->board().league)!=league){tap(g,8);wait(g,NativeMenuPage::HallOfFame);}
    tap(g,0,4);tap(g,2);require(g.hallBrowser()->canChallenge(),"Retained lap is not challengeable");
}
void image(NativeGameSession& g,const fs::path& path){std::ofstream out(path,std::ios::binary);out<<"P6\n320 200\n255\n";for(auto pixel:g.render())for(unsigned shift:{16u,8u,0u})out.put(char(pixel>>shift));require(bool(out),"Cannot save challenge preview");}
void pauseImage(NativeGameSession& g,const fs::path& path){
    const auto dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=1280;info.bmiHeader.biHeight=-720;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    std::uint32_t* pixels{};const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);require(bitmap&&pixels,"Cannot allocate Hall pause preview");const auto old=SelectObject(dc,bitmap);
    NativeDisplaySettings settings;const auto time=g.practiceGhostTime(),target=g.practiceGhostTarget();settings.setPracticeGhostContext(true,std::wstring(time.begin(),time.end()),g.canClearPracticeGhost(),std::wstring(target.begin(),target.end()));settings.openGameMenu(true);
    require(settings.practiceTarget().find(L"Hall rival:")==0&&settings.bestTime()==L"0:01.23","Pause merged rival identity/time with the career PB");
    WindowsGameDisplay display;auto prefs=settings.preferences();prefs.scaling=NativeScaling::Fit;display.paint(dc,1280,720,g.render(true,true),prefs);display.overlay(dc,1280,720,settings,L"");GdiFlush();
    std::ofstream out(path,std::ios::binary);out<<"P6\n1280 720\n255\n";for(unsigned n=0;n<1280*720;++n)for(unsigned shift:{16u,8u,0u})out.put(char(pixels[n]>>shift));SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);require(bool(out),"Cannot save Hall pause preview");
}
void seed(SharedHallArchive& archive,HallCatalog catalog){
    std::vector<std::shared_ptr<const HallSubmission>> entries;
    for(unsigned track=0;track<8;++track)for(unsigned league=0;league<2;++league){auto a=fixture::attempt(1+track*2+league,4000,{track,HallLeague(league),HallRecordKind::Lap},"RIVAL1234567");auto ghost=fixture::ghost(a);ghost.catalog=catalog;entries.push_back(std::make_shared<const HallSubmission>(makeHallSubmission(a,&ghost)));}
    archive.commit(entries);
}
void journeys(const fs::path& pack,const fs::path& root,NativeMenuNavigation policy){
    const auto shared=root/"shared",privateRoot=root/"private";auto g=career(pack,privateRoot,shared);const auto startup=startNativeGame(pack);HallCatalog catalog{ghostTrackContentIdentity(startup.payload),1};SharedHallArchive archive(shared,catalog);seed(archive,catalog);
    tap(*g,0,4);tap(*g);require(g->page()==NativeMenuPage::Matchup,"Missing pending season fixture");tap(*g,0,0,true);require(g->hasSuspendedSeason(),"Season did not suspend");
    const auto earned=*g->activeCareer();
    {NativeCareerStore store(privateRoot);for(bool super:{false,true}){auto pb=scr::test::ghost::lap(earned.id,0,super);pb.context.trackContentIdentity=catalog.trackContent;store.saveGhost(pb);}require(store.flush(),"Cannot seed independent challenge PBs");}
    g->setNavigation(policy);open(*g);
    for(unsigned track=0;track<8;++track)for(unsigned league=0;league<2;++league){
        board(*g,track,league);const auto selected=g->hallBrowser()->entries()[g->hallBrowser()->selected()].attempt.id;auto before=std::make_unique<NativeMemory>(g->memory());
        tap(*g);wait(*g,NativeMenuPage::Preview);
        require(g->memory()[0x1ca33]==track&&bool(g->memory()[0x1c9d0])==bool(league)&&!g->competitive(),"Challenge preview uses the wrong track/league/mode");
        require(g->activeCareer()->league==earned.league&&g->activeCareer()->division==earned.division,"Preview promoted/relegated the career");
        if(track==0&&league==1)image(*g,root/"super-preview.ppm");
        // Preview cancel restores the entire career world, not selected fields.
        tap(*g,0,0,true);wait(*g,NativeMenuPage::HallOfFame);require(g->memory()==*before,"Preview cancel changed native career memory/RNG");
        require(g->hallBrowser()->entries()[g->hallBrowser()->selected()].attempt.id==selected,"Preview cancel selected a different rival");
        tap(*g);wait(*g,NativeMenuPage::Preview);tap(*g);wait(*g,NativeMenuPage::Race);
        require(g->practiceGhostActive()&&!g->competitive()&&g->hasSuspendedSeason(),"Challenge did not enter isolated Practice");
        require(g->practiceGhostTarget().find("RIVAL1234567")!=std::string::npos,"Pause target lost the Hall rival name");
        if(track==0){
            require(g->canClearPracticeGhost()&&g->personalBestLaps().size()==1,"Challenge did not load its actual league's private PB");
            if(league==1)pauseImage(*g,root/"hall-pause.ppm");
            require(g->clearPracticeGhost()&&g->personalBestLaps().size()==0&&g->practiceGhostTarget().find("RIVAL1234567")!=std::string::npos,"Clearing a career PB removed the selected Hall rival");
            require(archive.readGhost(archive.read().hall->entries({track,HallLeague(league),HallRecordKind::Lap}).front().attempt).state==HallGhostState::Valid,"Challenge PB clear removed Hall storage");
        }else require(g->personalBestLaps().size()==0,"Starting a challenge imported its rival as a PB");
        auto baseline=std::make_unique<RaceScene>();baseline->loadNativeState(g->memory(),startup.payload,{});
        // Check the complete live world after recording/rendering/toggles, in
        // both leagues and every track, including the dynamic bridge.
        for(unsigned n=0;n<48;++n){NativeGameInput input;input.driveControls=1;g->update(.12,input);baseline->stepLive(1);
            if(n%12==0)for(auto view:{NativeView::Original,NativeView::Extended,NativeView::Widescreen,NativeView::Ultrawide}){
                g->setPracticeGhostVisible(false);g->render(true,false,view);g->setPracticeGhostVisible(true);g->render(true,true,view);
            }
        }
        const_cast<RaceScene&>(g->race()).quitLive();baseline->quitLive();
        auto actual=std::make_unique<NativeMemory>(),expected=std::make_unique<NativeMemory>();g->race().copyLiveExit(*actual);baseline->copyLiveExit(*expected);require(*actual==*expected,"Hall challenge recording/playback changed full native memory or RNG");
        require(g->flushRecords(),"Challenge store did not flush");
        require(g->sharedHall().hall->entries({track,HallLeague(league),HallRecordKind::Race}).empty(),"Challenge Practice created a race total");
        NativeGameInput leave;leave.leaveRace=true;g->update(0,leave);wait(*g,NativeMenuPage::HallOfFame);
        require(g->memory()==*before&&g->activeCareer()->league==earned.league,"Leaving a challenge changed earned career state");
        require(g->hallBrowser()->entries()[g->hallBrowser()->selected()].attempt.id==selected,"Challenge return changed selected rival");++routes;
    }
    // Closing either during preview or during driving retains a normal career
    // continuation, with its earned league and suspended season.
    board(*g,0,1);auto saved=std::make_unique<NativeMemory>(g->memory());tap(*g);wait(*g,NativeMenuPage::Preview);require(g->requestClose(),"Preview close did not flush");g.reset();
    g=career(pack,privateRoot,shared,false);require(g->activeCareer()->league==earned.league&&g->hasSuspendedSeason(),"Preview close saved the temporary Super league");
    open(*g);board(*g,0,1);tap(*g);wait(*g,NativeMenuPage::Preview);tap(*g);wait(*g,NativeMenuPage::Race);require(g->requestClose(),"Challenge close did not flush");g.reset();
    g=career(pack,privateRoot,shared,false);require(g->activeCareer()->league==earned.league&&g->hasSuspendedSeason(),"Driving close lost the pending season or saved the temporary league");
    tap(*g,0,4);tap(*g);require(g->page()==NativeMenuPage::Matchup,"Pending season no longer resumes");
    require(std::equal(saved->begin()+0x625a2,saved->begin()+0x625a7,g->memory().begin()+0x625a2),"Challenge close changed the suspended season RNG");require(g->requestClose(),"Resumed career did not close");
}
void unavailable(const fs::path& pack,const fs::path& root){
    auto g=career(pack,root/"private",root/"shared");const auto startup=startNativeGame(pack);HallCatalog catalog{ghostTrackContentIdentity(startup.payload),1};SharedHallArchive archive(root/"shared",catalog);seed(archive,catalog);open(*g);board(*g,0,0);
    auto a=g->hallBrowser()->entries().front().attempt;fs::remove(archive.root()/"ghosts"/hallObjectName(a.id));tap(*g);wait(*g,NativeMenuPage::HallOfFame);
    require(!g->hallBrowser()->ghost(0)&&g->page()==NativeMenuPage::HallOfFame,"Missing ghost launched a challenge or lost the time");
    // Resolve again with a valid object, then cancel while the worker is locked.
    auto ghost=fixture::ghost(a);ghost.catalog=catalog;archive.commit({std::make_shared<const HallSubmission>(makeHallSubmission(HallAttempt{a.id,a.board,a.name,a.time,{}},&ghost))});
    tap(*g,0,0,true);open(*g);board(*g,0,0);
    {SharedHallLock lock(archive.root());tap(*g);const auto start=std::chrono::steady_clock::now();g->render();tap(*g,0,0,true);require(std::chrono::steady_clock::now()-start<std::chrono::seconds(1),"Resolving/canceling a rival blocked the menu thread");}
    wait(*g,NativeMenuPage::Main);for(unsigned n=0;n<20;++n){update(*g,.01);Sleep(1);}require(g->page()==NativeMenuPage::Main,"Canceled async resolution later launched a challenge");
    open(*g);board(*g,0,0);std::vector<std::shared_ptr<const HallSubmission>> faster;
    for(unsigned n=0;n<20;++n)faster.push_back(std::make_shared<const HallSubmission>(makeHallSubmission(fixture::attempt(200+n,1+n))));archive.commit(faster);
    tap(*g);wait(*g,NativeMenuPage::HallOfFame);require(g->hallBrowser()->entries().front().attempt.id!=a.id&&!g->hallBrowser()->canChallenge(),"Displaced stale row launched a different ghost");require(g->requestClose(),"Unavailable career did not close");
}
void capturedTarget(const fs::path& chip,const fs::path& payloadPath,const fs::path& inputs,const fs::path& root){
    const auto captured=replay::loadCapture(chip.string().c_str());std::ifstream file(payloadPath,std::ios::binary);const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),{}};const auto plan=replay::loadInputs(inputs.string().c_str());
    auto source=std::make_unique<RaceScene>();source->loadNativeState(*captured,payload,{});source->recordHallAttempts(newHallRunId(),false);std::optional<HallSubmission> recorded;
    for(auto input:plan){if(source->liveTerminal())break;source->stepLive(input.controls,input.quit);auto completions=source->takeHallCompletions();if(!completions.empty())recorded=std::move(completions.back());}
    require(recorded&&recorded->recording,"Authored captured trajectory did not earn a Hall lap");
    const auto pinned=pinHallGhost(recorded->attempt,*recorded->recording,{ghostTrackContentIdentity(payload),1});
    SharedHallArchive archive(root/"shared",pinned->catalog);archive.commit({std::make_shared<const HallSubmission>(*recorded)});
    auto scene=std::make_unique<RaceScene>(),baseline=std::make_unique<RaceScene>();scene->loadNativeState(*captured,payload,{});baseline->loadNativeState(*captured,payload,{});PersonalBestLaps bests;const GhostLapKey key{"challenge",0,false};scene->recordPersonalBestLaps(bests,{key,GhostLapSource::Practice,pinned->catalog.trackContent},pinned);scene->recordHallAttempts(newHallRunId(),false);
    bool cleared=false;unsigned delivered=0;
    for(unsigned tick=0;tick<plan.size()&&!scene->liveTerminal();++tick){auto input=plan[tick];scene->stepLive(input.controls,input.quit);baseline->stepLive(input.controls,input.quit);
        const auto playback=scene->lapGhostPlayback(1);if(playback)require(playback->hallTarget==pinned,"A completed/faster/cleared PB replaced the Hall target");
        if(tick%24==0)for(auto view:{NativeView::Original,NativeView::Extended,NativeView::Widescreen,NativeView::Ultrawide})for(bool smooth:{false,true}){
            const auto shown=scene->renderPracticeGhost(.5,smooth,view),hidden=scene->renderLive(.5,false,smooth,view);
            if(shown!=hidden){++visible;viewMask|=1u<<unsigned(view);}
            require(scene->renderPracticeGhost(.5,smooth,view)==shown,"Repeated/paused rendering changed Hall playback");
        }
        if(!cleared&&bests.best(key)){scene->clearPersonalBestLap();cleared=true;}
        for(const auto& completion:scene->takeHallCompletions()){require(completion.attempt.board.kind==HallRecordKind::Lap,"Challenge capture delivered a race total");++delivered;}
    }
    require(cleared&&bests.best(key),"Pinned trajectory omitted PB clear and fresh-lap re-record");
    require(delivered==3,"Pinned trajectory did not retain all three Hall completions");
    require(visible&&viewMask==15,"Pinned trajectory was not visible through all four apertures");
    // Evict the selected time, rotate recovery generations, collect its object.
    for(unsigned round=0;round<4;++round){std::vector<std::shared_ptr<const HallSubmission>> faster;
        for(unsigned n=0;n<20;++n)faster.push_back(std::make_shared<const HallSubmission>(makeHallSubmission(fixture::attempt(1000+round*20+n,1+n))));archive.commit(faster);}
    require(!fs::exists(archive.root()/"ghosts"/hallObjectName(pinned->attempt.id))&&pinned->recording.samples.size()>2,"Pinned rival was not independent of archive collection");
    scene->quitLive();baseline->quitLive();auto a=std::make_unique<NativeMemory>(),b=std::make_unique<NativeMemory>();scene->copyLiveExit(*a);baseline->copyLiveExit(*b);require(*a==*b,"Pinned trajectory/render/clear changed simulation or RNG");
}
}
int main(int argc,char** argv)try{
    require(argc==6,"Usage: native_hall_challenge_tests pack output chip payload inputs");const fs::path root=argv[2];fs::create_directories(root);
    journeys(argv[1],root/"responsive",NativeMenuNavigation::Responsive);journeys(argv[1],root/"original",NativeMenuNavigation::Original);unavailable(argv[1],root/"unavailable");capturedTarget(argv[3],argv[4],argv[5],root/"captured");
    std::ofstream report(root/"results.json");report<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"routes\":"<<routes<<",\"view_mask\":"<<viewMask<<",\"visible_queries\":"<<visible<<",\"pinned_target\":true,\"career_and_rng_isolation\":true,\"safe_close\":true,\"missing_and_cancel\":true,\"original_cpu_executed\":false}\n";
    std::cout<<"Hall challenge acceptance passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
