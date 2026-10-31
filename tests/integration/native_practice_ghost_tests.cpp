#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "session/native_game_session.h"
#include "persistence/ghost_lap_codec.h"
#include "platform/windows/game_display.h"
#include "state/replay_io.h"
#include <fstream>
#include <iostream>
#include <set>

namespace {
using namespace scr;using namespace scr::runtime;namespace replay=scr::state::replay;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void tap(NativeGameSession& game,unsigned controls=16,unsigned number=0){NativeGameInput in;in.menu={std::uint8_t(controls),std::uint8_t(number)};game.update(0,{});game.update(0,in);game.update(0,{});}
void tap(NativeDisplaySettings& menu,unsigned controls){menu.navigate(0);menu.navigate(controls);menu.navigate(0);}
std::unique_ptr<NativeGameSession> open(const std::filesystem::path& pack,const std::filesystem::path& root,bool create){
    auto game=std::make_unique<NativeGameSession>(pack,std::filesystem::path{});game->enableCareers(root);game->setNavigation(NativeMenuNavigation::Responsive);game->update(0,{});
    tap(*game);tap(*game);
    if(create){
        require(game->enteringName(),"Ghost career fixture did not reach name entry");
        for(unsigned char c:std::string("GHOST")){NativeGameInput in;in.rawKeys.push_back({*nativeAmigaRawKey(c,0,false),true});game->update(0,in);in.rawKeys[0].pressed=false;game->update(0,in);}
        tap(*game);tap(*game);
    }
    require(game->activeCareer()&&game->page()==NativeMenuPage::Main,"Ghost career did not open its main menu");return game;
}
void practice(NativeGameSession& game){
    unsigned ordinal=0;while(ordinal<8&&game.memory()[0x648c2+ordinal]!=0)++ordinal;require(ordinal<8,"Missing track zero");
    tap(game,0,2);tap(game);if(game.page()==NativeMenuPage::Division){tap(game,0,1u<<(3-ordinal/2));tap(game);}
    require(game.page()==NativeMenuPage::Track,"Ghost Practice selection missing");tap(game,0,1u<<(ordinal%2));tap(game);tap(game);
    require(game.page()==NativeMenuPage::Race&&game.memory()[0x1ca33]==0&&game.practiceGhostActive(),"Ghost Practice did not attach to track zero");
}
void league(NativeGameSession& game){
    auto& m=const_cast<NativeMemory&>(game.memory());unsigned ordinal=0;while(ordinal<8&&m[0x648c2+ordinal]!=0)++ordinal;
    require(ordinal<8&&!(ordinal&1),"League fixture requires track zero on its division's even route");const unsigned division=ordinal/2;m[0x1c9ce]=std::uint8_t(division);
    unsigned local=0;while(local<12&&m[0x1c9c2+local]!=11)++local;require(local<12,"Missing human league driver");std::swap(m[0x1c9c2+local],m[0x1c9c2+m[0x5ab42+division]+2]);
    tap(game,0,4);tap(game);require(game.page()==NativeMenuPage::Matchup&&game.memory()[0x1ca33]==0,"League fixture did not select track zero");tap(game);tap(game);
    require(game.page()==NativeMenuPage::Race&&game.competitive(),"Ghost league race did not start");
}
void handoff(NativeGameSession& game,const NativeMemory& seed,const std::vector<std::uint8_t>& payload,GhostLapSource source){
    // Synthetic test-only entry handoff, before driving. The production menu
    // path above still selects the real career/mode/key. No midrace injection.
    auto& race=const_cast<RaceScene&>(game.race());race.loadNativeState(seed,payload,{});
    auto& bests=const_cast<PersonalBestLaps&>(game.personalBestLaps());
    race.recordPersonalBestLaps(bests,{{game.activeCareer()->id,0,false},source});
}
struct Surface {
    HDC dc{CreateCompatibleDC(nullptr)};HBITMAP bitmap{};HGDIOBJ old{};std::uint32_t* pixels{};int width,height;
    Surface(int w,int h):width(w),height(h){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);require(bitmap&&pixels,"Cannot allocate ghost UI capture");old=SelectObject(dc,bitmap);}
    ~Surface(){SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);}
    void save(const std::filesystem::path& path){GdiFlush();std::ofstream out(path,std::ios::binary);out<<"P6\n"<<width<<' '<<height<<"\n255\n";
        for(int n=0;n<width*height;++n)for(unsigned shift:{16u,8u,0u})out.put(char(pixels[n]>>shift));require(bool(out),"Cannot save ghost UI capture");}
};
void image(NativeGameSession& game,WindowsGameDisplay& display,const NativeDisplaySettings& settings,const std::filesystem::path& path,int w=1280,int h=720){
    Surface surface(w,h);auto prefs=settings.preferences();prefs.scaling=NativeScaling::Fit;prefs.view=NativeView::Widescreen;prefs.interpolate=true;
    display.paint(surface.dc,w,h,game.render(true,settings.blocksGame(),NativeView::Widescreen),prefs);display.overlay(surface.dc,w,h,settings,L"");
    if(settings.page()!=NativeDisplayPage::Settings){const auto feedback=game.ghostFeedback();display.ghostFeedback(surface.dc,w,h,std::wstring(feedback.begin(),feedback.end()),game.ghostFeedbackWarning());}surface.save(path);
}
void drive(NativeGameSession& game,replay::ReplayInput input){NativeGameInput in;in.driveControls=input.controls;in.quit=input.quit;game.update(.12,in);}
struct Counts {unsigned practiceLaps{},leagueLaps{},visibleQueries{},viewMask{},toggleChecks{},feedback{},savedSource{},clearChecks{};};
std::string record(const std::filesystem::path& pack,const std::filesystem::path& root,const NativeMemory& captured,
    const std::vector<std::uint8_t>& payload,const std::vector<replay::ReplayInput>& plan,bool competitive,Counts& counts){
    auto game=open(pack,root,true);if(competitive)league(*game);else practice(*game);
    auto seed=std::make_unique<NativeMemory>(captured);
    if(competitive){(*seed)[0x1ca22]=0x80;auto data=std::make_unique<OpponentDrivingData>(scr::state::opponent::readData(*seed));
        auto opponent=scr::state::opponent::readState(*seed);opponent.segment=std::uint8_t(((*seed)[0x1ca32]+(*seed)[0x1ca1a]/2)%(*seed)[0x1ca1a]);opponent.subSegment=0;opponent.lateral=128;
        initializeOpponentCoordinates(*data,scr::state::opponent::readInputs(*seed),opponent);scr::state::opponent::writeState(*seed,opponent);}
    handoff(*game,*seed,payload,competitive?GhostLapSource::League:GhostLapSource::Practice);game->setPracticeGhostVisible(false);
    bool notice=false;
    for(const auto input:plan){if(game->raceEnded())break;drive(*game,input);notice|=game->ghostFeedback().find("New personal best:")!=std::string::npos;
        if(competitive)require(!game->race().lapGhostPlayback(1),"League enabled ghost playback");}
    const auto stats=game->race().lapRecordingStatistics();require(stats&&stats->recordedLaps==3&&stats->improvements>0&&notice,"Automatic saving/feedback did not observe completed laps");
    if(competitive)counts.leagueLaps=stats->recordedLaps;else counts.practiceLaps=stats->recordedLaps;++counts.feedback;
    const auto id=game->activeCareer()->id;require(game->requestClose(),"Completed ghost did not flush on close");game.reset();
    NativeCareerStore store(root);const auto saved=store.readGhost({id,0,false},ghostTrackContentIdentity(payload));
    require(saved.state==CareerFileState::Valid&&decodeGhostLap(saved.data).context.source==(competitive?GhostLapSource::League:GhostLapSource::Practice),"Saved ghost lost its mode or did not survive close");return id;
}
void player(const std::filesystem::path& pack,const std::filesystem::path& root,const NativeMemory& seed,const std::vector<std::uint8_t>& payload,
    const std::vector<replay::ReplayInput>& plan,const std::filesystem::path& output,Counts& counts){
    auto game=open(pack,root,false);practice(*game);const GhostLapKey key{game->activeCareer()->id,0,false};
    require(game->personalBestLaps().best(key)&&game->personalBestLaps().best(key)->context.source==GhostLapSource::League,"Practice did not load the saved league target");++counts.savedSource;
    handoff(*game,seed,payload,GhostLapSource::Practice);auto baseline=std::make_unique<RaceScene>();baseline->loadNativeState(seed,payload,{});
    WindowsGameDisplay display;NativeDisplaySettings menu;std::filesystem::create_directories(output/"frames");unsigned imageIndex=0;
    for(unsigned tick=0;tick<250;++tick){
        drive(*game,plan.at(tick));baseline->stepLive(plan[tick].controls,plan[tick].quit);
        if(tick%12==0)for(auto view:{NativeView::Original,NativeView::Extended,NativeView::Widescreen,NativeView::Ultrawide})for(bool smooth:{false,true}){
            game->setPracticeGhostVisible(true);const auto shown=game->render(smooth,false,view);const auto before=game->race().lapGhostPlayback(1);
            game->setPracticeGhostVisible(false);const auto hidden=game->render(smooth,false,view);
            game->setPracticeGhostVisible(true);require(game->render(smooth,false,view)==shown,"Re-enabling the ghost changed its playback position");
            const auto after=game->race().lapGhostPlayback(1);require(bool(before)==bool(after)&&(!before||before->elapsedMicroseconds==after->elapsedMicroseconds),"Toggle reset playback");++counts.toggleChecks;
            if(shown!=hidden){++counts.visibleQueries;counts.viewMask|=1u<<unsigned(view);}
        }
        if(tick>=35&&tick<155){const auto name=std::to_string(10000+imageIndex++).substr(1);image(*game,display,menu,output/"frames"/(name+".ppm"),864,480);}
    }
    require(counts.visibleQueries&&counts.viewMask==15,"Ghost was not visible through all four player apertures");
    NativeDisplaySettings settings;settings.openSettings();for(unsigned n=0;n<7;++n)tap(settings,2);image(*game,display,settings,output/"settings.ppm");
    const auto bestTime=game->practiceGhostTime();menu.setPracticeGhostContext(true,std::wstring(bestTime.begin(),bestTime.end()),true);
    menu.openGameMenu(true);const auto paused=game->render(true,true,NativeView::Widescreen);require(game->render(true,true,NativeView::Widescreen)==paused,"Paused ghost advanced");
    image(*game,display,menu,output/"pause.ppm");tap(menu,2);tap(menu,2);tap(menu,2);tap(menu,16);
    require(menu.page()==NativeDisplayPage::ClearGhostConfirmation,"Practice clear confirmation missing");image(*game,display,menu,output/"clear-confirmation.ppm");
    tap(menu,16);require(bool(game->personalBestLaps().best(key)),"Cancelling clear removed the target");
    tap(menu,16);tap(menu,2);tap(menu,16);require(menu.takeAction()==NativeDisplayAction::ClearGhost&&game->clearPracticeGhost(),"Confirmed Practice clear failed");++counts.clearChecks;
    require(!game->race().lapGhostPlayback(1)&&!game->personalBestLaps().best(key),"Clear retained the active ghost");
    menu.setPracticeGhostContext(true,L"Not set",false);image(*game,display,menu,output/"cleared.ppm");
    menu.back();
    const auto file=root/"ghosts"/key.career/"0-normal.scr-ghost";
    for(unsigned tick=250;tick<=505;++tick){drive(*game,plan.at(tick));baseline->stepLive(plan[tick].controls,plan[tick].quit);}
    require(!game->personalBestLaps().best(key)&&!std::filesystem::exists(file),"Cleared in-progress candidate restored the deleted best");++counts.clearChecks;
    for(unsigned tick=506;tick<=961;++tick){drive(*game,plan.at(tick));baseline->stepLive(plan[tick].controls,plan[tick].quit);}
    require(game->personalBestLaps().best(key)&&game->ghostFeedback().find("New personal best:")!=std::string::npos,"Fresh lap after clear did not resume recording and feedback");
    menu.back();image(*game,display,menu,output/"new-best.ppm");
    auto a=std::make_unique<NativeMemory>(),b=std::make_unique<NativeMemory>();const_cast<RaceScene&>(game->race()).quitLive();baseline->quitLive();game->race().copyLiveExit(*a);baseline->copyLiveExit(*b);
    require(*a==*b,"Player ghost/toggle/clear queries changed simulation or RNG");
    require(game->requestClose(),"New best after clear did not save");game.reset();
    NativeCareerStore store(root);require(store.readGhost(key,ghostTrackContentIdentity(payload)).state==CareerFileState::Valid,"New best after clear did not survive close");
}
}
int main(int argc,char** argv)try{
    require(argc==7,"Usage: native_practice_ghost_tests pack output chip payload controls original-trace");
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);Counts counts;
    if(!std::filesystem::exists(argv[3])||!std::filesystem::exists(argv[4])){std::cout<<"SKIP: missing local test input for Practice ghost acceptance\n";return 0;}
    auto seed=replay::loadCapture(argv[3]);std::ifstream file(argv[4],std::ios::binary);const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),{}};const auto plan=replay::loadInputs(argv[5]);
    record(argv[1],output/"practice-data",*seed,payload,plan,false,counts);record(argv[1],output/"league-data",*seed,payload,plan,true,counts);
    player(argv[1],output/"league-data",*seed,payload,plan,output,counts);
    std::ofstream report(output/"results.json");report<<"{\"status\":\"passed\",\"practice_recorded_laps\":"<<counts.practiceLaps<<",\"league_recorded_laps\":"<<counts.leagueLaps
        <<",\"visible_player_queries\":"<<counts.visibleQueries<<",\"view_mask\":"<<counts.viewMask<<",\"toggle_checks\":"<<counts.toggleChecks<<",\"feedback_modes\":"<<counts.feedback
        <<",\"loaded_league_ghost_in_practice\":"<<counts.savedSource<<",\"clear_checks\":"<<counts.clearChecks<<",\"simulation_and_rng_unchanged\":true,\"synthetic_capture_entry_handoff\":true,\"midrace_injection\":false}\n";
    std::cout<<"Saved Practice ghost player acceptance passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
