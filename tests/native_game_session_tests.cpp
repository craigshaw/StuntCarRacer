#include "../app/native_game_session.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace scr::viewer;
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
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("native_game_session_tests pack output-directory");
    const std::filesystem::path directory=argv[2];std::filesystem::create_directories(directory);
    unsigned routes=0,ticks=0;bool turning=false;
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
        for(unsigned n=0;n<600&&game.page()==NativeMenuPage::Race;++n){
            const auto previous=game.race().liveTicks();game.update(0.12,{{},1,true});ticks+=game.race().liveTicks()-previous;
            if(game.raceEnded()){game.update(0,{});game.update(1,{{16,0},0,false});}
        }
        settle(game);
        require(game.page()==NativeMenuPage::Track,"Repeated Practice did not return");
        choose(game,2);confirm(game);require(game.page()==NativeMenuPage::Main,"Track Cancel did not return to main menu");
        require(hash(game.render())==originalMain,"Practice route changed original main-menu appearance");
        hall(game);
        turning=true;++routes;
    }
    std::cout<<"{\"status\":\"passed\",\"practice_routes\":"<<routes<<",\"repeated_races\":"<<routes
        <<",\"ticks\":"<<ticks<<",\"preview_rotation\":"<<(turning?"true":"false")
        <<",\"hall_of_fame_routes\":"<<routes*2<<",\"hall_of_fame_fire_wait_checked\":true"
        <<",\"return_to_main_menu\":true,\"capture_input\":false,\"cpu_emulation\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
