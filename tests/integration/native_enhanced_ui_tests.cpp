#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "session/native_game_session.h"
#include "platform/windows/game_display.h"
#include "platform/windows/shared_hall_storage.h"
#include "presentation/native_competition_render.h"
#include "state/opponent_driving_adapter.h"
#include "enhanced_record_fixture.h"
#include "hall_ghost_fixture.h"
#include "state/replay_io.h"
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
using namespace scr;using namespace scr::runtime;namespace fs=std::filesystem;namespace replay=scr::state::replay;namespace fixture=scr::test::enhanced;
unsigned checks{},recoveryChecks{},crossings{},viewMask{},hallSwitches{},recordNoticeRoutes{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void image(const fs::path& path,const std::vector<std::uint32_t>& pixels,unsigned width=320){std::ofstream out(path,std::ios::binary);out<<"P6\n"<<width<<' '<<pixels.size()/width<<"\n255\n";for(auto pixel:pixels)for(unsigned shift:{16u,8u,0u})out.put(char(pixel>>shift));check(bool(out),"Cannot save Enhanced UI preview");}
void tap(NativeDisplaySettings& settings,unsigned controls){settings.navigate(0);settings.navigate(controls);settings.navigate(0);}
void tap(NativeGameSession& g,unsigned controls=16,unsigned number=0,bool back=false){NativeGameInput in;in.menu={std::uint8_t(controls),std::uint8_t(number),back};g.update(0,{});g.update(0,in);g.update(0,{});}
void waitHall(NativeGameSession& g){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(std::chrono::steady_clock::now()<until){if(g.hallBrowser()&&!g.transitioning()&&!g.hallBrowser()->busy())return;g.update(.02,{});Sleep(1);}throw std::runtime_error("Enhanced browser did not settle");}
void preferences(const fs::path& root){
    fs::create_directories(root);const auto path=root/"display.ini";
    check(loadDisplayPreferences(path).timing==TimingCategory::Enhanced,"Fresh preferences did not select Enhanced");
    for(unsigned version=1;version<=9;++version){std::ofstream out(path);out<<"SCR display "<<version<<"\nfullscreen=1\nscaling="<<(version<4?"smooth":"fit")<<"\ninterpolate=1\n";
        if(version>=2)out<<"controls=modern\n";if(version>=3)out<<"navigation=original\n";
        if(version>=4)out<<"filtering=bilinear\ninteger-scale=2\n";
        if(version==5)out<<"view=widescreen\n";else if(version>=6)out<<"aspect="<<(version==6?"extended":version==7?"widescreen":"ultrawide")<<'\n';
        if(version==9)out<<"practice-ghost=0\n";out.close();const auto p=loadDisplayPreferences(path);
        check(p.timing==TimingCategory::Enhanced&&p.fullscreen&&p.interpolate&&p.scaling==NativeScaling::Fit&&p.filtering==NativeFiltering::Bilinear,"Timing migration lost display choices");
        check(p.controls==(version>=2?DriveControlPreset::Modern:DriveControlPreset::Original)&&p.navigation==(version>=3?NativeMenuNavigation::Original:NativeMenuNavigation::Responsive),"Timing migration lost controls/navigation");
        check(p.practiceGhost==(version!=9)&&p.integerScale==(version>=4?2u:0u),"Timing migration lost ghost/scale choices");
        const auto view=version<5?NativeView::Original:version<=6?NativeView::Extended:version==7?NativeView::Widescreen:NativeView::Ultrawide;check(p.view==view,"Timing migration lost aspect ratio");
    }
    auto p=loadDisplayPreferences(path);p.timing=TimingCategory::Original;check(saveDisplayPreferences(path,p)&&loadDisplayPreferences(path)==p,"Explicit Original choice did not round-trip");
    p.timing=TimingCategory::Enhanced;check(saveDisplayPreferences(path,p)&&loadDisplayPreferences(path)==p,"Explicit Enhanced choice did not round-trip");
    NativeDisplaySettings settings(p);settings.openSettings();for(unsigned n=1;n<displayPreferenceCount;++n)tap(settings,2);tap(settings,8);check(settings.preferences().timing==TimingCategory::Original,"Settings did not preview timing");
    settings.back();check(settings.preferences()==p&&!settings.blocksGame(),"Cancel did not restore timing with all other preferences");
    settings.openSettings();for(unsigned n=1;n<displayPreferenceCount;++n)tap(settings,2);tap(settings,8);tap(settings,2);settings.navigate(0);check(settings.navigate(16)&&settings.blocksGame(),"Apply did not retain the settings transaction");
    const auto blocked=fs::path(path.wstring()+L".tmp");fs::create_directory(blocked);check(!saveDisplayPreferences(path,settings.preferences())&&loadDisplayPreferences(path)==p,"Failed Apply replaced saved preferences");fs::remove(blocked);
    check(saveDisplayPreferences(path,settings.preferences()),"Timing Apply retry failed");settings.applied();check(!settings.blocksGame()&&loadDisplayPreferences(path).timing==TimingCategory::Original,"Timing Apply did not persist Original");
}
void overlay(const fs::path& path,const std::vector<std::uint32_t>& pixels,NativeDisplaySettings& settings){
    const int width=960,height=800;const auto dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    std::uint32_t* bits{};const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&bits),nullptr,0);check(bitmap&&bits,"Cannot allocate settings preview");const auto old=SelectObject(dc,bitmap);
    WindowsGameDisplay display;display.paint(dc,width,height,pixels,settings.preferences());display.overlay(dc,width,height,settings,L"");GdiFlush();image(path,{bits,bits+width*height},width);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
void freshTimer(const fs::path& pack,const fs::path& root){
    auto seed=startNativeRace(pack,0);auto shown=std::make_unique<RaceScene>(),original=std::make_unique<RaceScene>();shown->loadNativeState(*seed.memory,seed.payload,{});original->loadNativeState(*seed.memory,seed.payload,{});
    shown->setRunningLapHundredths(true);original->setRunningLapHundredths(true);shown->setGhostTimingCategory(TimingCategory::Enhanced);
    EnhancedClockValue previous;unsigned restarts=0;bool captured=false;
    for(unsigned n=0;n<1024&&(n<512||!captured||!recoveryChecks);++n){shown->stepLive(1);original->stepLive(1);const auto& frame=shown->frames().back();const auto value=shown->enhancedClock(1).lap;
        if(frame.restarts>restarts&&value.available&&previous.available&&frame.lapCounter==1){check(value.microseconds-previous.microseconds==120000,"Native future recovery penalty appeared in its earlier car frame");++recoveryChecks;}
        previous=value;restarts=frame.restarts;
        for(double position:{0.0,.25,.75,.125,1.0,.25}){const auto a=shown->enhancedClock(position),b=shown->enhancedClock(position);check(a.lap.microseconds==b.lap.microseconds&&a.lap.available==b.lap.available,"Clock query changed its retained history");}
        if(!captured&&value.available&&value.microseconds>7000000){
            for(auto view:{NativeView::Original,NativeView::Extended,NativeView::Widescreen,NativeView::Ultrawide}){
                const auto baseline=original->renderLive(.5,false,true,view);const auto enhanced=shown->renderLive(.5,false,true,view);check(enhanced!=baseline,"Enhanced HUD did not change the clock");
                const auto width=unsigned(enhanced.size()/200),offset=(width-320)/2;
                for(unsigned p=0;p<enhanced.size();++p)if(enhanced[p]!=baseline[p]){const auto x=p%width,y=p/width;const bool corner=y==178||y==185||y==188||y==195;
                    check(x>=offset+(corner?235u:234u)&&x<offset+(corner?283u:284u)&&((y>=178&&y<186)||(y>=188&&y<196)),"Enhanced HUD painted outside the artwork's clock slots");}
                image(root/("timer-"+std::to_string(unsigned(view))+".ppm"),enhanced,width);viewMask|=1u<<unsigned(view);
                if(view==NativeView::Original){
                    const auto assets=decodeRaceRenderAssets(seed.payload.data(),seed.payload.size(),seed.memory->data(),seed.memory->size());
                    auto finish=baseline;composeEnhancedFinish(finish,assets.font,assets.palette,shown->enhancedClock(.5));check(finish==enhanced,"Saved finish and live clock layouts differ");
                    image(root/"timer-original.ppm",baseline);
                    for(const auto& example:std::vector<std::pair<const char*,EnhancedClockDisplay>>{
                        {"timer-layout",{{6416000,true,false},{47125000,true,false}}},
                        {"timer-layout-approximate",{{6416000,true,true},{47125000,true,true}}},
                        {"timer-layout-long",{{7200000000LL,true,false},{7200000000LL,true,true}}},
                        {"timer-layout-unavailable",{}}}){
                        auto preview=baseline;composeEnhancedFinish(preview,assets.font,assets.palette,example.second);image(root/(std::string(example.first)+".ppm"),preview);
                    }
                }
                shown->setGhostTimingCategory(TimingCategory::Original);check(shown->renderLive(.5,false,true,view)==baseline,"Original mode reused Enhanced timer pixels");shown->setGhostTimingCategory(TimingCategory::Enhanced);
                const auto paused=shown->renderLiveMessage(0x4c,2,.5,true,view);check(paused==shown->renderLiveMessage(0x4c,2,.5,true,view),"Paused Enhanced time changed");
            }
            NativeDisplaySettings settings;settings.openSettings();overlay(root/"settings.ppm",shown->renderLive(.5),settings);settings.back();settings.setPracticeGhostContext(true,L"0:07.123",true,L"Enhanced career PB",TimingCategory::Enhanced);settings.openGameMenu(true);overlay(root/"game-menu.ppm",shown->renderLive(.5),settings);
            tap(settings,2);tap(settings,2);tap(settings,2);tap(settings,16);overlay(root/"clear-enhanced.ppm",shown->renderLive(.5),settings);
            auto& synthetic=const_cast<RaceFrame&>(shown->frames().back());const auto retained=synthetic.enhancedClock;synthetic.enhancedClock.changes.clear();synthetic.enhancedClock.before.start=LapTimingBoundary{};
            auto& start=*synthetic.enhancedClock.before.start;start.crossingMicroseconds=0;start.player=11;start.confidence=timing::Reason::missingPrevious;synthetic.enhancedClock.before.movement.player=11;synthetic.enhancedClock.before.movement.confidence=timing::Reason::reliable;
            check(shown->enhancedClock(1).lap.approximate,"Constructed uncertain presentation lost its approximation marker");image(root/"approximate.ppm",shown->renderLive(1,false,false));synthetic.enhancedClock=retained;captured=true;
        }
    }
    check(captured&&viewMask==15&&recoveryChecks,"Fresh HUD proof omitted views or native recovery");shown->quitLive();original->quitLive();auto a=std::make_unique<NativeMemory>(),b=std::make_unique<NativeMemory>();shown->copyLiveExit(*a);original->copyLiveExit(*b);check(*a==*b,"HUD queries/settings changed full native memory/RNG");
}
void completed(const NativeMemory& captured,const std::vector<std::uint8_t>& payload,const std::vector<replay::ReplayInput>& plan,const fs::path& root){
    auto shown=std::make_unique<RaceScene>(),baseline=std::make_unique<RaceScene>();shown->loadNativeState(captured,payload,{});baseline->loadNativeState(captured,payload,{});shown->setGhostTimingCategory(TimingCategory::Enhanced);
    for(const auto input:plan){if(shown->liveTerminal())break;shown->stepLive(input.controls,input.quit);baseline->stepLive(input.controls,input.quit);
        for(const auto& lap:shown->takeTimingCompletions()){check(lap.enhanced.lap.rankable(),"Authored crossing became approximate");
            const auto& from=shown->frames().front();const auto& to=shown->frames().back();const auto span=to.presentationMicroseconds-from.presentationMicroseconds;check(span==120000,"Authored crossing lost its movement bracket");
            const auto position=double(lap.enhanced.finish.crossingMicroseconds-from.presentationMicroseconds)/span;const auto at=shown->enhancedClock(position).lap;
            check(at.available&&at.microseconds==lap.enhanced.lap.scoredMicroseconds,"Completed HUD result did not meet the presented car at its crossing");
            const auto before=shown->enhancedClock(position-1.0/span).lap;check(before.available&&before.microseconds+1==at.microseconds,"Enhanced hold appeared ahead of the crossing");
            check(formatEnhancedClock(at)==formatEnhancedTime(lap.enhanced.lap),"HUD rounding differed from Enhanced rankings");++crossings;
            image(root/("crossing-"+std::to_string(crossings)+".ppm"),shown->renderLive(position));
        }
    }
    check(crossings==3,"Authored display proof omitted complete laps");shown->quitLive();baseline->quitLive();auto a=std::make_unique<NativeMemory>(),b=std::make_unique<NativeMemory>();shown->copyLiveExit(*a);baseline->copyLiveExit(*b);check(*a==*b,"Sub-tick rendering changed native simulation/RNG");
}
std::unique_ptr<NativeGameSession> career(const fs::path& pack,const fs::path& root){auto g=std::make_unique<NativeGameSession>(pack,fs::path{});g->enableCareers(root);g->setNavigation(NativeMenuNavigation::Responsive);g->setTimingCategory(TimingCategory::Enhanced);g->update(0,{});tap(*g);tap(*g);
    for(unsigned char c:std::string("CLOCK")){NativeGameInput in;in.rawKeys.push_back({*nativeAmigaRawKey(c,0,false),true});g->update(0,in);in.rawKeys[0].pressed=false;g->update(0,in);}tap(*g);tap(*g);check(g->activeCareer()&&g->page()==NativeMenuPage::Main,"UI career did not open");return g;}
void browser(const fs::path& pack,const fs::path& root){
    auto g=career(pack,root);const auto game=startNativeGame(pack);const auto content=ghostTrackContentIdentity(game.payload);const EnhancedCatalog catalog{content,1,1};const auto shared=root/"shared-hall";
    EnhancedHallArchive enhanced(shared,catalog);SharedHallArchive original(shared,{content,1});
    std::vector<std::shared_ptr<const EnhancedSubmission>> rows;for(unsigned n=1;n<=20;++n){auto a=fixture::attempt(n,59999500+n);auto ghost=fixture::ghost(a);ghost.catalog=catalog;rows.push_back(std::make_shared<const EnhancedSubmission>(makeEnhancedSubmission(a,&ghost)));}
    auto race=fixture::attempt(101,180123500,{0,HallLeague::Normal,HallRecordKind::Race});rows.push_back(std::make_shared<const EnhancedSubmission>(makeEnhancedSubmission(race)));enhanced.commit(rows);
    auto old=scr::test::hall::attempt(1,123);auto ghost=scr::test::hall::ghost(old);ghost.catalog={content,1};original.commit({std::make_shared<const HallSubmission>(makeHallSubmission(old,&ghost))});
    tap(*g,0,1);tap(*g);waitHall(*g);check(g->hallBrowser()->timingCategory()==TimingCategory::Enhanced&&g->hallBrowser()->rowCount()==20,"Hall default ignored Enhanced preference");image(root/"hall-enhanced.ppm",g->render());
    tap(*g,0,8);tap(*g,8);waitHall(*g);++hallSwitches;check(g->hallBrowser()->timingCategory()==TimingCategory::Original&&g->hallBrowser()->rowTime(0)=="0:01.23","Original filter reused Enhanced milliseconds");image(root/"hall-original.ppm",g->render());
    tap(*g,8);waitHall(*g);++hallSwitches;check(g->hallBrowser()->rowTime(0)=="1:00.000"&&g->hallBrowser()->rank(19)==1,"Enhanced filter lost tie/rounding rules");
    tap(*g,0,4);tap(*g,8);waitHall(*g);check(g->hallBrowser()->rowTime(0)=="3:00.124"&&!g->hallBrowser()->canChallenge(),"Three-lap Enhanced display or eligibility differs");image(root/"hall-race.ppm",g->render());
    tap(*g,8);waitHall(*g);tap(*g,2);check(g->hallBrowser()->canChallenge(),"Enhanced Ready row was not challengeable");tap(*g);const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(g->page()==NativeMenuPage::HallOfFame&&std::chrono::steady_clock::now()<until){g->update(.02,{});Sleep(1);}g->update(0,{});
    check(g->page()==NativeMenuPage::Preview&&g->effectiveTimingCategory()==TimingCategory::Enhanced,"Enhanced browser did not launch its pinned rival");image(root/"rival-preview.ppm",g->render());
    g->setTimingCategory(TimingCategory::Original);check(g->effectiveTimingCategory()==TimingCategory::Enhanced,"Changing preference overrode a pinned rival");tap(*g,0,0,true);waitHall(*g);check(g->effectiveTimingCategory()==TimingCategory::Original,"Challenge return did not restore the global timing preference");check(g->requestClose(),"UI career did not close safely");
}
void finishCheckpoint(const fs::path& pack,const fs::path& root){
    auto seed=startNativeGame(pack);NativeCareerCheckpoint c;c.memory=std::make_unique<NativeMemory>(*seed.memory);c.menu.page=NativeMenuPage::Results;c.menu.competitive=true;c.menu.division=3;c.finishPending=true;c.finishPixels.assign(64000,0x102030);
    c.finishTiming=EnhancedClockDisplay{{1234567,true,false},{3456789,true,true}};const auto encoded=encodeNativeCheckpoint(*seed.memory,c);const auto decoded=decodeNativeCheckpoint(*seed.memory,encoded);
    check(encoded.front()==3&&decoded.finishTiming&&decoded.finishTiming->lap.microseconds==1234567&&decoded.finishTiming->record.approximate&&decoded.finishPixels==c.finishPixels,"Finish metadata did not round-trip separately from Original pixels");
    c.finishTiming.reset();const auto old=encodeNativeCheckpoint(*seed.memory,c);check(old.front()==2&&!decodeNativeCheckpoint(*seed.memory,old).finishTiming,"Legacy finish was promoted to Enhanced precision");
    auto future=encoded;const auto offset=future.size()-36;future[offset]=99;bool rejected=false;try{(void)decodeNativeCheckpoint(*seed.memory,future);}catch(const UnsupportedNativeCheckpoint&){rejected=true;}check(rejected,"Unknown finish method was interpreted as current timing");
    std::ofstream(root/"finish-checkpoint-size.txt")<<encoded.size()<<'\n';
}
void recordNotices(const fs::path& pack,const NativeMemory& captured,const std::vector<std::uint8_t>& payload,const std::vector<replay::ReplayInput>& plan,const fs::path& root){
    const auto run=root/"record-data"/std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    const EnhancedCatalog catalog{ghostTrackContentIdentity(payload),1,1};
    std::optional<EnhancedRecordNotice> earned;
    for(unsigned route=0;route<3;++route){
        const auto data=run/std::to_string(route);auto g=career(pack,data);
        {EnhancedHallArchive archive(data/"shared-hall",catalog);std::vector<std::shared_ptr<const EnhancedSubmission>> entries;
            for(auto kind:{HallRecordKind::Lap,HallRecordKind::Race}){
                const auto time=route==2?(kind==HallRecordKind::Lap?earned->lap:earned->race)->milliseconds*1000:1;
                // Faster records in another league must not mask this race.
                auto a=fixture::attempt(500+unsigned(kind),time,{0,route?HallLeague::Normal:HallLeague::Super,kind});
                entries.push_back(std::make_shared<const EnhancedSubmission>(makeEnhancedSubmission(a)));
            }archive.commit(entries);
        }
        tap(*g,0,4);tap(*g);check(g->page()==NativeMenuPage::Matchup,"Record notice missed season matchup");tap(*g);tap(*g);
        check(g->page()==NativeMenuPage::Race&&g->competitive()&&g->memory()[0x1ca33]==0,"Record notice missed Little Ramp race");
        auto seed=std::make_unique<NativeMemory>(captured);std::copy_n(g->memory().begin()+0x1ecab,256,seed->begin()+0x1ecab);
        (*seed)[0x5eb79]=11;(*seed)[0x5eb76]=0;(*seed)[0x1ca22]=0x80;
        for(unsigned base:{0x1c908u,0x1c920u,0x1c938u})(*seed)[base+23]=0;
        auto opponent=state::opponent::readState(*seed);auto opponentData=std::make_unique<OpponentDrivingData>(state::opponent::readData(*seed));
        opponent.segment=std::uint8_t(((*seed)[0x1ca32]+1)%(*seed)[0x1ca1a]);opponent.subSegment=0;opponent.lateral=128;
        initializeOpponentCoordinates(*opponentData,state::opponent::readInputs(*seed),opponent);state::opponent::writeState(*seed,opponent);
        auto& scene=const_cast<RaceScene&>(g->race());scene.loadNativeState(*seed,payload,{});scene.recordHallAttempts(newHallRunId(),true);scene.setGhostTimingCategory(TimingCategory::Enhanced);
        auto baseline=std::make_unique<RaceScene>();baseline->loadNativeState(*seed,payload,{});
        for(const auto in:plan){if(g->raceEnded())break;NativeGameInput input;input.driveControls=in.controls;input.quit=in.quit;g->update(.12,input);baseline->stepLive(in.controls,in.quit);}
        check(g->raceEnded()&&g->flushRecords(),"Record notice race did not finish and save");
        auto actual=std::make_unique<NativeMemory>(),expected=std::make_unique<NativeMemory>();scene.copyLiveExit(*actual);baseline->copyLiveExit(*expected);
        check(*actual==*expected,"Enhanced record notices changed full simulation memory/RNG");
        // Reload at the pending cockpit finish, before either report is acknowledged.
        check(g->requestClose(),"Record notice finish did not close");g.reset();
        g=std::make_unique<NativeGameSession>(pack,fs::path{});g->enableCareers(data);g->setNavigation(NativeMenuNavigation::Responsive);g->setTimingCategory(TimingCategory::Enhanced);g->update(0,{});tap(*g);tap(*g);
        check(g->hasSuspendedSeason(),"Record notice finish was not resumable");tap(*g,0,4);tap(*g);check(g->raceEnded(),"Pending finish did not restore");tap(*g);
        check(g->page()==NativeMenuPage::Results,"Pending finish missed results");
        // Deliberately disagree with the Original notice trigger in both directions.
        const_cast<NativeMemory&>(g->memory())[0x1bbb1]=route?0xc1:0;
        tap(*g);
        if(route){check(g->page()==NativeMenuPage::RoundResults,"Enhanced announced an Original-only record or millisecond tie");}
        else{
            check(g->page()==NativeMenuPage::Records,"Enhanced record depended on Original flags");
            const auto hall=g->enhancedHall();EnhancedRecordNotice notice;
            for(auto kind:{HallRecordKind::Lap,HallRecordKind::Race}){const auto& a=hall.hall->entries({0,HallLeague::Normal,kind}).front().attempt;
                (kind==HallRecordKind::Lap?notice.lap:notice.race)=EnhancedRecordLine{a.name,a.time.milliseconds()};}
            earned=notice;std::vector<std::uint32_t> pixels;packNativeMenuPixels(makeEnhancedRecordNoticeImage(g->memory(),g->menuState().division,notice),pixels);
            check(g->render()==pixels,"Post-race values differ from the Enhanced Hall");image(root/"records-enhanced.ppm",pixels);
            g->setTimingCategory(TimingCategory::Original);packNativeMenuPixels(makeNativeCompetitionImage(g->memory(),NativeMenuPage::Records,g->menuState().division),pixels);
            check(g->render()==pixels,"Original record notice changed");image(root/"records-original.ppm",pixels);
            g->setTimingCategory(TimingCategory::Enhanced);image(root/"records-enhanced-restored.ppm",g->render());
            // The notice itself also survives closing on the records page.
            const auto retained=g->render();check(g->requestClose(),"Record notice page did not close");g.reset();
            g=std::make_unique<NativeGameSession>(pack,fs::path{});g->enableCareers(data);g->setNavigation(NativeMenuNavigation::Responsive);g->setTimingCategory(TimingCategory::Enhanced);g->update(0,{});tap(*g);tap(*g);tap(*g,0,4);tap(*g);
            check(g->page()==NativeMenuPage::Records&&g->render()==retained,"Saved records page lost Enhanced values");
        }
        check(g->requestClose(),"Record notice scenario did not close");
        ++recordNoticeRoutes;
    }
    EnhancedRecordNotice example;example.race=EnhancedRecordLine{"Craig",134191};example.lap=EnhancedRecordLine{"Craig",46540};
    auto game=startNativeGame(pack);const auto memory=std::make_unique<NativeMemory>(*game.memory);std::vector<std::uint32_t> pixels;
    packNativeMenuPixels(makeEnhancedRecordNoticeImage(*game.memory,3,example),pixels);image(root/"records-playtest-example.ppm",pixels);
    example.race.reset();packNativeMenuPixels(makeEnhancedRecordNoticeImage(*game.memory,3,example),pixels);image(root/"records-lap-only.ppm",pixels);
    example.lap.reset();example.race=EnhancedRecordLine{"TWELVE NAMES",734191};packNativeMenuPixels(makeEnhancedRecordNoticeImage(*game.memory,3,example),pixels);image(root/"records-race-only.ppm",pixels);
    example.race.reset();packNativeMenuPixels(makeEnhancedRecordNoticeImage(*game.memory,3,example),pixels);image(root/"records-none.ppm",pixels);
    check(*memory==*game.memory,"Record notice rendering changed native memory");
}
}
int main(int argc,char** argv)try{
    check(argc==6,"Usage: native_enhanced_ui_tests pack output chip payload inputs");for(unsigned n:{1u,3u,4u,5u})if(!fs::exists(argv[n])){std::cout<<"SKIP: missing local test input\n";return 0;}
    const fs::path root=argv[2];fs::create_directories(root);const auto captured=replay::loadCapture(argv[3]);std::ifstream file(argv[4],std::ios::binary);const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),{}};const auto plan=replay::loadInputs(argv[5]);
    preferences(root/"preferences");freshTimer(argv[1],root);completed(*captured,payload,plan,root);browser(argv[1],root/"browser");finishCheckpoint(argv[1],root);recordNotices(argv[1],*captured,payload,plan,root);
    std::ofstream report(root/"results.json");report<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"migration_versions\":9,\"view_mask\":"<<viewMask<<",\"native_recovery_checks\":"<<recoveryChecks<<",\"interpolated_crossings\":"<<crossings<<",\"hall_category_switches\":"<<hallSwitches<<",\"record_notice_routes\":"<<recordNoticeRoutes<<",\"apply_cancel\":true,\"pinned_category\":true,\"finish_checkpoint\":true,\"full_memory_and_rng_isolation\":true}\n";std::cout<<"Enhanced UI: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
