#include "native_game_session.h"
#include "native_display_settings.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace scr::viewer;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
NativeGameInput input(unsigned controls=0,bool back=false,unsigned number=0){
    NativeGameInput value;value.menu={std::uint8_t(controls),std::uint8_t(number),back};return value;
}
NativeGameUpdate tap(NativeGameSession& game,unsigned controls=0,bool back=false,unsigned number=0){
    game.update(0,{});auto result=game.update(0,input(controls,back,number));
    require(!game.transitioning(),"Responsive action retained a blocking transition");game.update(0,{});return result;
}
void choose(NativeGameSession& game,unsigned row){tap(game,0,false,1u<<row);}
void confirm(NativeGameSession& game){tap(game,16);}
void back(NativeGameSession& game){tap(game,0,true);}
void key(NativeGameSession& game,unsigned vk){
    const auto raw=nativeAmigaRawKey(vk,0,false);require(bool(raw),"Missing test key mapping");
    NativeGameInput value;value.rawKeys.push_back({*raw,true});game.update(0,value);
    value.rawKeys[0].pressed=false;game.update(0,value);
}
void text(NativeGameSession& game,const std::string& value){for(unsigned char c:value)key(game,c>='a'&&c<='z'?c-'a'+'A':c);}
std::string name(const NativeGameSession& game,unsigned index){
    const auto begin=game.memory().begin()+0x1ecab+(11-index)*16;return {begin,begin+12};
}
std::array<std::uint8_t,5> rng(const NativeGameSession& game){
    std::array<std::uint8_t,5> value{};std::copy_n(game.memory().begin()+0x625a2,5,value.begin());return value;
}
void responsive(NativeGameSession& game){game.setNavigation(NativeMenuNavigation::Responsive);game.update(0,{});}
void clockChecks(){
    NativeMenuInputClock clock;
    require(clock.sample({2,0},0).controls==2,"Direction did not move immediately");
    require(!clock.sample({2,0},.299).controls,"Direction repeated too early");
    require(clock.sample({2,0},.001).controls==2,"Direction missed the 300 ms repeat");
    require(!clock.sample({2,0},.099).controls&&clock.sample({2,0},.001).controls==2,"Direction missed the 100 ms repeat");
    clock.sample({},0);require(clock.sample({2,0},0).controls==2,"Separate rapid direction tap was lost");
    require(clock.sample({1,0},0).controls==1,"Direction change was delayed");
    clock.sample({},0);require(clock.sample({16,0},0).controls==16&&!clock.sample({16,0},2).controls,"Held confirm repeated");
    clock.enterScreen({18,0});require(!clock.sample({18,0},2).controls,"Held arrival controls moved the new screen");
    require(clock.sample({20,0},0).controls==4,"Fresh direction waited for unrelated held confirm");
    clock.sample({},0);require(clock.sample({0,0,true},0).back&&!clock.sample({0,0,true},2).back,"Held Back repeated");
    for(unsigned hz:{30u,60u,120u}){
        clock.clear();unsigned moves=bool(clock.sample({2,0},0).controls);
        for(unsigned n=0;n<hz;++n)moves+=bool(clock.sample({2,0},1.0/hz).controls);
        require(moves==9,"Direction repeat depends on presentation rate");
    }
}
void practice(const std::filesystem::path& pack){
    for(unsigned ordinal=0;ordinal<8;++ordinal){
        NativeGameSession game(pack,{},NativeGameStart::EstablishedMenu);responsive(game);
        const auto main=game.menuState().selected;back(game);require(game.page()==NativeMenuPage::Main&&game.menuState().selected==main,"Main Back was not inert");
        game.update(0,input(16));require(game.page()==NativeMenuPage::Division,"Practice confirmation was delayed");
        game.update(.5,input(16));require(game.page()==NativeMenuPage::Division,"Held confirm crossed another screen");game.update(0,{});
        choose(game,(ordinal/2)^3);confirm(game);choose(game,ordinal&1);confirm(game);
        require(game.page()==NativeMenuPage::Preview,"Track confirmation did not immediately open preview");
        const auto random=rng(game);const auto image=game.render();
        back(game);require(game.page()==NativeMenuPage::Track&&game.menuState().selected==(ordinal&1),"Preview Back lost track selection");
        confirm(game);require(game.page()==NativeMenuPage::Preview&&rng(game)==random&&game.render()==image,"Preview revisit repeated preparation or changed image");
        auto memory=std::make_unique<NativeMemory>(game.memory());
        game.setNavigation(NativeMenuNavigation::Original);game.setNavigation(NativeMenuNavigation::Responsive);
        require(game.memory()==*memory,"Switching navigation modified game state");
        back(game);choose(game,2);confirm(game);
        require(game.page()==NativeMenuPage::Division&&game.menuState().selected==((ordinal/2)^3),"Cancel did not restore division selection");
        back(game);require(game.page()==NativeMenuPage::Main&&game.menuState().selected==main,"Division Back lost main selection");
        choose(game,0);confirm(game);require(game.page()==NativeMenuPage::HallOfFame,"Hall entry was delayed");
        back(game);require(game.page()==NativeMenuPage::Main&&game.menuState().selected==0,"Hall Back did not restore main selection");
    }
}
void opening(const std::filesystem::path& pack){
    {
        NativeGameSession game(pack,{});responsive(game);confirm(game);
        game.update(0,input(16));
        auto letter=input(16);letter.rawKeys.push_back({*nativeAmigaRawKey('B',0,false),true});game.update(0,letter);
        game.update(0,{});confirm(game);
        require(game.openingPage()==NativeOpeningPage::Roster&&name(game,0)=="b           ","Blank-name retry swallowed a fresh letter while confirm stayed held");
    }
    {
        NativeGameSession game(pack,{});for(unsigned n=0;n<8;++n)game.update(1,{});
        game.update(0,input(16));game.update(1,{});require(game.enteringName(),"Original name setup failed");
        game.setNavigation(NativeMenuNavigation::Responsive);text(game,"new");confirm(game);
        require(game.openingPage()==NativeOpeningPage::Roster&&name(game,0)=="new         ","Switching policy during name entry lost the first fresh letter");
    }
    {
        NativeGameSession game(pack,{});responsive(game);const auto random=rng(game);back(game);
        require(game.openingPage()==NativeOpeningPage::Mode&&rng(game)==random,"Mode chooser Back was not inert");
        confirm(game);text(game,"bob");back(game);require(game.openingPage()==NativeOpeningPage::Mode,"First-name Back missed mode chooser");
        confirm(game);text(game,"by");
        auto heldReturn=input(16);heldReturn.rawKeys.push_back({0x44,true});game.update(0,heldReturn);
        heldReturn.rawKeys.clear();game.update(.5,heldReturn);
        require(game.openingPage()==NativeOpeningPage::Roster&&name(game,0)=="bobby       ","Held keyboard Return crossed roster or draft text was lost");game.update(0,{});
        back(game);require(game.enteringName(),"Opening roster did not return to name");key(game,8);text(game,"a");confirm(game);
        require(name(game,0)=="bobba       ","Editing a confirmed name did not retain its cursor");confirm(game);require(game.page()==NativeMenuPage::Main,"Opening did not complete immediately");
    }
    for(unsigned count=2;count<=8;++count){
        NativeGameSession game(pack,{});responsive(game);choose(game,1);confirm(game);
        for(unsigned n=0;n<count;++n){
            text(game,"driver"+std::to_string(n+1));confirm(game);
            if(n+1<count){choose(game,0);confirm(game);}
        }
        if(count<8){
            back(game);require(game.enteringName()&&game.memory()[0x5eb76]==count-1,"Add Player Back changed player count");confirm(game);
            choose(game,0);confirm(game);text(game,"unfinished");back(game);
            require(game.openingPage()==NativeOpeningPage::AddPlayer&&game.memory()[0x5eb76]==count-1,"Cancelling unfinished player changed completed players");
            choose(game,1);confirm(game);
        }
        require(game.openingPage()==NativeOpeningPage::LocalStandings,"Multiplayer setup missed standings");
        back(game);
        require(count==8?game.enteringName():game.openingPage()==NativeOpeningPage::AddPlayer,"Opening standings returned to wrong setup step");
        if(count<8){choose(game,1);confirm(game);}else confirm(game);
        if(count<8)require(game.openingPage()==NativeOpeningPage::LocalStandings,"Revised setup missed standings");
        if(count==8)require(game.openingPage()==NativeOpeningPage::LocalStandings,"Eighth name did not return to standings");
        confirm(game);require(game.page()==NativeMenuPage::Main&&game.memory()[0x5eb76]==count-1,"Multiplayer setup lost its player count");
    }
}
void policySwitch(const std::filesystem::path& pack){
    NativeGameSession game(pack,{},NativeGameStart::EstablishedMenu);
    for(unsigned n=0;n<8;++n)game.update(1,{});
    game.update(0,input(0,false,1));game.update(1,{});game.update(1,{});
    game.update(0,input(16));game.update(1,{});
    for(unsigned n=0;game.transitioning()&&n<16;++n)game.update(1,{});
    require(game.page()==NativeMenuPage::HallOfFame,"Original Hall setup failed");
    responsive(game);back(game);
    require(game.page()==NativeMenuPage::Main&&game.menuState().selected==0,"Policy switch lost the parent selection");
    game.releaseInput({16,0});game.update(0,input(18));
    require(game.page()==NativeMenuPage::Main&&game.menuState().selected==1,"Fresh direction waited for an unrelated held confirm after input release");
}
void competition(const std::filesystem::path& pack){
    NativeGameSession game(pack,{},NativeGameStart::EstablishedMenu);responsive(game);choose(game,2);confirm(game);
    require(game.page()==NativeMenuPage::Matchup,"Season did not prepare matchup immediately");
    auto memory=std::make_unique<NativeMemory>(game.memory());back(game);
    // Input scratch may change; score/fixture/RNG must not.
    require(game.page()==NativeMenuPage::Matchup&&rng(game)==std::array<std::uint8_t,5>{(*memory)[0x625a2],(*memory)[0x625a3],(*memory)[0x625a4],(*memory)[0x625a5],(*memory)[0x625a6]},"Matchup Back altered fixture");
    confirm(game);const auto random=rng(game);back(game);require(game.page()==NativeMenuPage::Matchup,"Season preview Back missed prepared matchup");
    confirm(game);require(game.page()==NativeMenuPage::Preview&&rng(game)==random,"Season preview revisit repeated preparation");
    const auto started=tap(game,16);require(started.raceStarted&&game.page()==NativeMenuPage::Race,"Race handoff retained a presentation delay");
    for(unsigned n=0;n<600&&game.page()==NativeMenuPage::Race;++n){
        if(game.raceEnded())confirm(game);else{NativeGameInput value;value.quit=true;game.update(.12,value);}
    }
    require(game.page()==NativeMenuPage::Results,"Race did not return committed results");
    for(unsigned n=0;n<8&&game.page()!=NativeMenuPage::Matchup&&game.page()!=NativeMenuPage::Main;++n){
        const auto page=game.page();const auto randomBefore=rng(game);back(game);
        require(game.page()==page&&rng(game)==randomBefore,"Back advanced a championship report");confirm(game);
    }
    require(game.page()==NativeMenuPage::Matchup||game.page()==NativeMenuPage::Main,"Report continuation did not advance competition");
}
void saves(const std::filesystem::path& pack,const std::filesystem::path& output){
    const auto file=output/"navigation.scr-save";std::filesystem::remove(file);
    NativeGameSession game(pack,{},NativeGameStart::EstablishedMenu,file);responsive(game);
    choose(game,3);confirm(game);choose(game,1);confirm(game);
    require(game.savePhase()==NativeSaveMenuPhase::InsertDisc,"Save entry missed original prompt");back(game);
    require(game.page()==NativeMenuPage::LoadSave&&!game.savePhase()&&game.menuState().selected==1,"Save prompt Back missed submenu");
    confirm(game);confirm(game);if(game.savePhase()==NativeSaveMenuPhase::FormatWarning)confirm(game);
    require(game.savePhase()==NativeSaveMenuPhase::SaveSelection,"Save directory did not reach slot selection");
    confirm(game);text(game,"draft");back(game);require(game.savePhase()==NativeSaveMenuPhase::SaveSelection,"Save-name Back did not return to slots");
    back(game);require(game.page()==NativeMenuPage::LoadSave&&!std::filesystem::exists(file),"Cancelled draft committed a save");
    confirm(game);confirm(game);if(game.savePhase()==NativeSaveMenuPhase::FormatWarning)confirm(game);confirm(game);text(game,"career");confirm(game);
    require(std::filesystem::exists(file)&&!game.savePhase(),"Confirmed save was not committed");
    std::ifstream saved(file,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(saved),{}};saved.close();
    back(game);require(game.page()==NativeMenuPage::Main,"Save completion Back did not dismiss to main");
    choose(game,3);confirm(game);choose(game,0);confirm(game);confirm(game);confirm(game);
    require(!game.savePhase(),"Confirmed load did not finish");back(game);require(game.page()==NativeMenuPage::Main,"Load completion Back missed main");
    std::ifstream loaded(file,std::ios::binary);const std::string after{std::istreambuf_iterator<char>(loaded),{}};
    require(after.size()==bytes.size(),"Load completion changed save extent");
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::invalid_argument("native_navigation_tests pack output-directory");
    std::filesystem::create_directories(argv[2]);clockChecks();practice(argv[1]);opening(argv[1]);policySwitch(argv[1]);competition(argv[1]);saves(argv[1],argv[2]);
    std::cout<<"{\"status\":\"passed\",\"practice_routes\":8,\"local_player_counts\":[2,3,4,5,6,7,8],\"responsive_taps\":true,\"repeat_timing\":true,\"back_routes\":true,\"preview_rng_preserved\":true,\"save_cancellation\":true,\"competition_progression\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
