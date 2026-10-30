#include "session/native_game_session.h"
#include "driving_replay_io.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace scr::runtime;
void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
bool settle(NativeGameSession& game){
    bool started=false;
    for(unsigned n=0;game.transitioning()&&n<10;++n)started=game.update(1,{}).raceStarted||started;
    require(!game.transitioning(),"Original palette transition did not complete");return started;
}
void ready(NativeGameSession& game){settle(game);game.update(1,{});game.update(1,{});}
void choose(NativeGameSession& game,unsigned row){
    ready(game);game.update(0,{{0,std::uint8_t(1u<<row)},0,false});ready(game);
}
void confirm(NativeGameSession& game){
    ready(game);game.update(0,{{16,0},0,false});game.update(1,{});
}
std::uint64_t hash(const std::vector<std::uint32_t>& pixels){
    require(pixels.size()==320*200,"Native game canvas is not320x200");
    std::uint64_t value=14695981039346656037ULL;
    for(auto pixel:pixels){value^=pixel;value*=1099511628211ULL;}return value;
}
void bitmap(const std::filesystem::path& path,const std::vector<std::uint32_t>& pixels){
    std::ofstream out(path,std::ios::binary);
    const auto u16=[&](unsigned x){out.put(char(x));out.put(char(x>>8));};
    const auto u32=[&](unsigned x){u16(x);u16(x>>16);};
    out.write("BM",2);u32(54+320*200*4);u32(0);u32(54);u32(40);u32(320);u32(unsigned(-200));u16(1);u16(32);
    u32(0);u32(320*200*4);u32(2835);u32(2835);u32(0);u32(0);
    for(auto pixel:pixels)u32(pixel);require(bool(out),"Cannot export native game screenshot");
}
void cockpitLabels(const NativeGameSession& game,const CockpitInstrumentPixels& expected){
    const auto& actual=game.race().frames().back().instrumentPixels;
    for(unsigned y=168;y<188;++y)for(unsigned x=35;x<68;++x){
        if(x>=45&&x<58)continue;
        const auto index=(y-cockpitInstrumentTop)*320+x;
        if(actual[index]!=expected[index])throw std::runtime_error(
            "Cockpit L/B labels differ after menu entry at x="+std::to_string(x)+" y="+std::to_string(y));
    }
}
void hall(NativeGameSession& game,const std::filesystem::path& screenshot={}){
    const auto main=hash(game.render());
    std::array<std::uint8_t,512> records{};
    std::copy_n(game.memory().begin()+0x7a61a,records.size(),records.begin());
    choose(game,0);confirm(game);
    require(game.page()==NativeMenuPage::HallOfFame&&game.transitioning(),"Hall of Fame did not begin its original transition");
    settle(game);require(game.memory()[0x5d724]==0x42,"Hall of Fame did not select its display mode");
    const auto displayed=hash(game.render());
    require(displayed!=main,"Hall of Fame did not replace the menu image");
    if(!screenshot.empty())bitmap(screenshot,game.render());
    for(unsigned n=0;n<3;++n){
        game.update(1,{{16,0},0,false});
        require(game.page()==NativeMenuPage::HallOfFame&&hash(game.render())==displayed,"Held confirmation dismissed Hall of Fame");
    }
    game.update(0,{{15,15},0,true});
    const auto delay=nativeMenuDelaySeconds(5);
    game.update(delay/2,{{31,15},0,true});
    require(game.page()==NativeMenuPage::HallOfFame,"Hall of Fame ignored its fire debounce");
    game.update(delay/2,{{31,15},0,true});
    require(game.page()==NativeMenuPage::Main&&game.transitioning(),"New fire did not return from Hall of Fame");
    settle(game);
    require(game.memory()[0x5d724]==0x41&&hash(game.render())==main,"Hall of Fame did not restore the main menu");
    require(std::equal(records.begin(),records.end(),game.memory().begin()+0x7a61a),"Hall of Fame modified stored records");
}
void wideTransitions(const std::filesystem::path& pack){
    for(auto view:{NativeView::Widescreen,NativeView::Ultrawide}){
        NativeGameSession game(pack,{},NativeGameStart::EstablishedMenu);
        confirm(game);choose(game,3);confirm(game);choose(game,0);confirm(game);game.update(1,{});settle(game);
        require(game.page()==NativeMenuPage::Preview,"Wide fade fixture did not reach Practice preview");
        game.render(false,false,view); // Select the race presentation before entry capture.
        game.update(0,{});game.update(0,{{16,0},0,false});
        unsigned wideFadeFrames=0,changedFrames=0;
        std::vector<std::uint32_t> previous;
        for(unsigned n=0;game.transitioning()&&n<600;++n){
            const auto image=game.render(false,false,view);
            if(image.size()==viewWidth(view)*200){
                ++wideFadeFrames;changedFrames+=!previous.empty()&&image!=previous;previous=image;
            }else require(image.size()==320*200,"Race entry fade has an invalid canvas");
            game.update(1.0/60,{});
        }
        require(!game.transitioning()&&game.page()==NativeMenuPage::Race&&wideFadeFrames>1&&changedFrames>0,"Wide race entry did not render its tinted palette fade");
        for(unsigned n=0;n<600&&game.page()==NativeMenuPage::Race;++n){
            game.update(.12,{{},0,true});game.render(false,false,view);
            if(game.raceEnded()){game.update(0,{});game.update(1,{{16,0},0,false});}
        }
        settle(game);
        require(game.page()==NativeMenuPage::Track&&game.render().size()==320*200,"Wide race exit did not restore the original menu presentation");
    }
}
void throttlePresets(const std::filesystem::path& pack,const std::filesystem::path& directory){
    for(auto preset:{DriveControlPreset::Original,DriveControlPreset::Modern}){
        const bool modern=preset==DriveControlPreset::Modern;
        const auto folder=directory/(modern?"control-modern":"control-original");
        NativeGameSession game(pack,folder,NativeGameStart::EstablishedMenu);
        confirm(game);choose(game,3);confirm(game);choose(game,0);confirm(game);game.update(1,{});settle(game);
        require(game.page()==NativeMenuPage::Preview,"Control test did not reach Practice preview");
        game.update(0,{});game.update(0,{{16,0},0,false});settle(game);
        require(game.page()==NativeMenuPage::Race,"Control test did not enter Practice");
        {std::ofstream seed(folder/"native-initial.bin",std::ios::binary);
            seed.write(reinterpret_cast<const char*>(game.memory().data()),game.memory().size());require(bool(seed),"Cannot export control test seed");}
        const auto step=[&](unsigned mask,double elapsed=0.12){
            NativeGameInput input;input.driveControls=mask;input.drivePreset=preset;game.update(elapsed,input);
            require(game.page()==NativeMenuPage::Race&&!game.raceEnded(),"Control test ended its race early");
            return game.race().frames().back();
        };
        bool moving=false;
        for(unsigned n=0;n<128;++n){const auto frame=step(1);if(frame.gasOutput&&frame.forwardSpeed>256&&frame.forwardSpeed<0x7800){moving=true;break;}}
        require(moving,"Held gas did not start the car");
        // Releasing between presentation polls must affect the next 120 ms
        // update, even if a gas press is pending in the input sampler.
        const auto before=game.race().liveTicks();step(1,0.06);step(0,0.059);
        require(game.race().liveTicks()==before,"Controls changed the fixed tick phase");
        auto frame=step(0,0.001);
        require(game.race().liveTicks()==before+1,"Control release missed the next physics tick");
        for(unsigned n=0;n<8;++n){
            if(n)frame=step(0);
            require(modern?(frame.gasOutput==0&&frame.accelerationState==0):(frame.gasOutput!=0&&(frame.accelerationState&0x80)),
                "Released throttle did not follow the selected preset");
        }
        require(frame.forwardSpeed!=0,"Modern coast applied a stop instead of retaining momentum");
        frame=step(17);require(frame.gasOutput&&frame.boostActive,"Held throttle and boost no longer work together");
        const auto fuel=frame.boostFuel;frame=step(16);
        require(modern?(frame.gasOutput==0&&!frame.boostActive&&frame.boostFuel==fuel):(frame.gasOutput!=0&&frame.boostActive),
            "Boost after throttle release did not follow the selected preset");
        frame=step(2);require(frame.gasOutput==0xff10&&!frame.accelerationState,"Brake no longer uses the original reverse force");
        frame=step(0);require(!frame.gasOutput&&!frame.accelerationState,"Brake release restarted acceleration");
        if(modern){
            // Settings suspend the host and clear pending input. They must
            // preserve Original's latch, then clear it on a Modern neutral tick.
            preset=DriveControlPreset::Original;step(1);game.releaseInput();
            frame=step(0);require(frame.gasOutput&&(frame.accelerationState&0x80),"Pause input clear removed Original's throttle latch");
            const auto ticks=game.race().liveTicks();const auto image=game.render(true);
            for(unsigned n=0;n<3;++n)game.render(true,true);
            require(game.race().liveTicks()==ticks&&game.render(true)==image,"Paused presentation changed throttle or tick state");
            preset=DriveControlPreset::Modern;game.releaseInput();frame=step(0);
            require(!frame.gasOutput&&!frame.accelerationState,"Switching to Modern retained Original's acceleration latch");
            preset=DriveControlPreset::Original;frame=step(0);
            require(!frame.gasOutput&&!frame.accelerationState,"Switching to Original invented a throttle press");
        }
        const auto recording=game.race().liveRecording();
        std::filesystem::copy_file(recording,folder/"controls.csv",std::filesystem::copy_options::overwrite_existing);
        auto trace=recording.string();trace.replace(trace.size()-13,13,"-native.csv");
        std::filesystem::copy_file(trace,folder/"native.csv",std::filesystem::copy_options::overwrite_existing);
        if(modern){
            bool rejected=false;try{(void)scr::test::replay::loadInputs(recording.string().c_str());}
            catch(const std::runtime_error& error){rejected=std::string(error.what()).find("Modern held-acceleration")!=std::string::npos;}
            require(rejected,"Modern recording was admitted to original-code verification");
        }else require(scr::test::replay::loadInputs(recording.string().c_str()).size()==game.race().liveTicks(),"Original recording no longer loads");
    }
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("native_game_session_tests pack output-directory");
    const std::filesystem::path directory=argv[2];std::filesystem::create_directories(directory);
    unsigned routes=0,ticks=0,labelChecks=0;bool turning=false;
    const auto directRace=startNativeRace(argv[1],0);
    const auto labels=NativeCockpitPresentation(*directRace.memory).capture();
    for(unsigned ordinal=0;ordinal<8;++ordinal){
        const auto folder=directory/("route-"+std::to_string(ordinal));std::filesystem::create_directories(folder);
        NativeGameSession game(argv[1],folder,NativeGameStart::EstablishedMenu);
        settle(game);
        require(game.page()==NativeMenuPage::Main,"Native startup did not enter original main menu");
        const auto originalMain=hash(game.render());if(!ordinal)bitmap(directory/"main.bmp",game.render());
        hall(game,ordinal?std::filesystem::path{}:directory/"hall-of-fame.bmp");
        confirm(game);require(game.page()==NativeMenuPage::Division,"Practice did not enter division menu");
        if(!ordinal)bitmap(directory/"divisions.bmp",game.render());
        choose(game,(ordinal/2)^3);confirm(game);
        require(game.page()==NativeMenuPage::Track,"Division did not enter track menu");
        choose(game,ordinal&1);if(!ordinal)bitmap(directory/"tracks.bmp",game.render());
        confirm(game);game.update(1,{});
        settle(game);
        require(game.page()==NativeMenuPage::Preview,"Track did not enter original preview");
        const auto before=hash(game.render());if(!ordinal)bitmap(directory/"preview.bmp",game.render());
        game.update(0,{});game.update(0,{{4,0},0,false});const auto rotated=hash(game.render());
        require(rotated!=before,"Preview left input did not rotate the track");
        game.update(0,{});game.update(0,{{8,0},0,false});
        require(hash(game.render())==before,"Preview left/right did not return to the same image");
        game.update(0,{});game.update(0,{{16,0},0,false});
        require(settle(game)&&game.page()==NativeMenuPage::Race,"Preview fire did not start native Practice");
        {
            std::ofstream seed(folder/"native-initial.bin",std::ios::binary);
            seed.write(reinterpret_cast<const char*>(game.memory().data()),game.memory().size());
            require(bool(seed),"Cannot export native menu race seed");
        }
        const auto controls=game.race().liveRecording();
        cockpitLabels(game,labels);++labelChecks;
        if(!ordinal)bitmap(directory/"race.bmp",game.render());
        if(!ordinal){
            const auto unpaused=hash(game.render());
            require(hash(game.render(false,true))!=unpaused,"Original pause panel did not render");
            bitmap(directory/"paused.bmp",game.render(false,true));
            require(hash(game.render())==unpaused,"Pause panel contaminated race rendering");
        }
        for(unsigned n=0;n<64;++n){game.update(0.12,{{},1,false});++ticks;}
        require(game.race().liveTicks()==64,"Menu route changed original physics rate");
        for(unsigned n=0;n<600&&game.page()==NativeMenuPage::Race;++n){
            const auto previous=game.race().liveTicks();game.update(0.12,{{},1,true});ticks+=game.race().liveTicks()-previous;
            if(game.raceEnded()){game.update(0,{});game.update(1,{{16,0},0,false});}
        }
        settle(game);
        require(game.page()==NativeMenuPage::Track,"Original Practice exit did not return to track selection");
        require(game.memory()[0x57c5b]==0x80&&game.memory()[0x5d724]==0,"Original Practice exit cleanup missing");
        std::filesystem::copy_file(controls,folder/"controls.csv",std::filesystem::copy_options::overwrite_existing);
        auto trace=controls.string();trace.replace(trace.size()-13,13,"-native.csv");
        std::filesystem::copy_file(trace,folder/"native.csv",std::filesystem::copy_options::overwrite_existing);
        if(!ordinal)bitmap(directory/"return.bmp",game.render());
        // Repeat the same selected race without destroying the menu session.
        confirm(game);game.update(1,{});settle(game);require(game.page()==NativeMenuPage::Preview,"Repeat Practice preview failed");
        game.update(0,{});game.update(0,{{16,0},0,false});
        settle(game);
        require(game.page()==NativeMenuPage::Race&&game.race().liveTicks()==0,"Repeat Practice reused old race ticks");
        cockpitLabels(game,labels);++labelChecks;
        const auto repeatFolder=folder/"repeat";std::filesystem::create_directories(repeatFolder);
        {
            std::ofstream seed(repeatFolder/"native-initial.bin",std::ios::binary);
            seed.write(reinterpret_cast<const char*>(game.memory().data()),game.memory().size());
            require(bool(seed),"Cannot export repeated race seed");
        }
        const auto repeatControls=game.race().liveRecording();
        for(unsigned n=0;n<600&&game.page()==NativeMenuPage::Race;++n){
            const auto previous=game.race().liveTicks();game.update(0.12,{{},1,true});ticks+=game.race().liveTicks()-previous;
            if(game.raceEnded()){game.update(0,{});game.update(1,{{16,0},0,false});}
        }
        settle(game);
        require(game.page()==NativeMenuPage::Track,"Repeated Practice did not return");
        std::filesystem::copy_file(repeatControls,repeatFolder/"controls.csv",std::filesystem::copy_options::overwrite_existing);
        auto repeatTrace=repeatControls.string();repeatTrace.replace(repeatTrace.size()-13,13,"-native.csv");
        std::filesystem::copy_file(repeatTrace,repeatFolder/"native.csv",std::filesystem::copy_options::overwrite_existing);
        choose(game,2);confirm(game);require(game.page()==NativeMenuPage::Main,"Track Cancel did not return to main menu");
        require(hash(game.render())==originalMain,"Practice route changed original main-menu appearance");
        hall(game);
        turning=true;++routes;
    }
    wideTransitions(argv[1]);throttlePresets(argv[1],directory);
    std::cout<<"{\"status\":\"passed\",\"practice_routes\":"<<routes<<",\"repeated_races\":"<<routes
        <<",\"ticks\":"<<ticks<<",\"preview_rotation\":"<<(turning?"true":"false")
        <<",\"hall_of_fame_routes\":"<<routes*2<<",\"hall_of_fame_fire_wait_checked\":true"
        <<",\"cockpit_label_checks\":"<<labelChecks
        <<",\"throttle_presets\":2,\"modern_coasts_on_release\":true,\"preset_switch_and_pause_checked\":true,\"modern_original_replay_rejected\":true"
        <<",\"return_to_main_menu\":true,\"capture_input\":false,\"cpu_emulation\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
