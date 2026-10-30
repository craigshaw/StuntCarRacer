#include "session/native_game_session.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace scr::runtime;
void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
void settle(NativeGameSession& game){
    for(unsigned n=0;game.transitioning()&&n<16;++n)game.update(1,{});
    require(!game.transitioning(),"Competition palette transition did not finish");
}
void fire(NativeGameSession& game){
    settle(game);game.update(0,{});game.update(0.25,{});game.update(0,{{16,0},0,false});settle(game);
}
void image(const std::filesystem::path& path,const std::vector<std::uint32_t>& pixels){
    require(pixels.size()==64000,"Competition canvas is not320x200");
    std::ofstream out(path,std::ios::binary);
    const auto u16=[&](unsigned x){out.put(char(x));out.put(char(x>>8));};
    const auto u32=[&](unsigned x){u16(x);u16(x>>16);};
    out.write("BM",2);u32(54+320*200*4);u32(0);u32(54);u32(40);u32(320);u32(unsigned(-200));u16(1);u16(32);
    u32(0);u32(320*200*4);u32(2835);u32(2835);u32(0);u32(0);
    for(auto pixel:pixels)u32(pixel);
    require(bool(out),"Cannot export competition image");
}
}
int main(int argc,char** argv)try{
    using namespace scr::runtime;
    if(argc!=3)throw std::invalid_argument("native_opponent_session_tests asset-pack output-directory");
    const std::filesystem::path directory=argv[2];std::filesystem::create_directories(directory);
    NativeGameSession game(argv[1],directory/"recordings",NativeGameStart::EstablishedMenu);settle(game);
    require(game.page()==NativeMenuPage::Main,"Session did not start at main menu");
    game.update(1,{});game.update(1,{});game.update(0,{{0,4},0,false});
    game.update(1,{});game.update(1,{});game.update(1,{});
    game.update(0,{{16,0},0,false});game.update(1,{});settle(game);
    require(game.page()==NativeMenuPage::Matchup,"Start the Racing Season did not open original matchup");
    unsigned races=0,ticks=0;std::set<unsigned> pages;bool originalRate=true;
    for(unsigned actions=0;actions<80&&game.page()!=NativeMenuPage::Main;++actions){
        pages.insert(unsigned(game.page()));
        if(game.page()==NativeMenuPage::Matchup){
            require(game.memory()[0x1ca22]==0x80&&game.memory()[0x1ca29]!=11,"Matchup did not select legitimate original opponent");
            const auto folder=directory/("race-"+std::to_string(races));std::filesystem::create_directories(folder);
            image(folder/"matchup.bmp",game.render());fire(game);
            require(game.page()==NativeMenuPage::Preview,"Competitive matchup did not enter preview");pages.insert(unsigned(game.page()));
            image(folder/"preview.bmp",game.render());fire(game);
            require(game.page()==NativeMenuPage::Race&&game.race().liveTicks()==0,"Competitive preview did not start fresh native race");
            {
                std::ofstream seed(folder/"native-initial.bin",std::ios::binary);
                seed.write(reinterpret_cast<const char*>(game.memory().data()),game.memory().size());require(bool(seed),"Cannot export competitive seed");
            }
            const auto recording=game.race().liveRecording();
            image(folder/"race.bmp",game.render());
            for(unsigned tick=0;tick<64;++tick){game.update(0.12,{{},0,false});++ticks;}
            originalRate=originalRate&&game.race().liveTicks()==64;
            require(game.race().frames().back().opponentEnabled,"Live race did not enable original opponent rendering");
            image(folder/"race-after64.bmp",game.render(true));
            if(!races){
                for(unsigned step=0;step<2000&&!game.raceEnded();++step){game.update(0.12,{{},0,false});++ticks;}
                require(game.raceEnded()&&game.race().frames().back().raceComplete,"Original opponent did not finish an actual race");
                fire(game);
            }else for(unsigned step=0;step<600&&game.page()==NativeMenuPage::Race;++step){
                const auto before=game.race().liveTicks();game.update(0.12,{{},0,true});ticks+=game.race().liveTicks()-before;
                if(game.raceEnded()){game.update(0,{});game.update(0.25,{});game.update(0,{{16,0},0,false});}
            }
            settle(game);require(game.page()==NativeMenuPage::Results,"Competitive exit did not enter original results artwork");
            std::filesystem::copy_file(recording,folder/"controls.csv",std::filesystem::copy_options::overwrite_existing);
            auto trace=recording.string();trace.replace(trace.size()-13,13,"-native.csv");
            std::filesystem::copy_file(trace,folder/"native.csv",std::filesystem::copy_options::overwrite_existing);
            image(folder/"results.bmp",game.render());++races;
            require(races<=4,"Original local season exceeded four human fixtures");
        }else{
            image(directory/("page-"+std::to_string(unsigned(game.page()))+".bmp"),game.render());fire(game);
        }
    }
    require(game.page()==NativeMenuPage::Main&&races==4,"Complete season did not return to original main menu");
    require(originalRate,"Competitive route changed original120ms physics rate");
    // Early quits set no new lap/race record, so the conditional Records and
    // championship award pages are covered by the original pixel oracle.
    for(auto page:{NativeMenuPage::Results,NativeMenuPage::RoundResults,NativeMenuPage::Standings,NativeMenuPage::SeasonEnd,NativeMenuPage::Roster})
        require(pages.count(unsigned(page))!=0,"Complete season skipped an original results page");
    require(game.memory()[0x1ca35]==0&&game.memory()[0x57c72]==0&&game.memory()[0x57c73]==0,"Season did not reset original race counters");
    std::cout<<"{\"status\":\"passed\",\"local_seasons\":1,\"human_races\":"<<races<<",\"opponent_completed_races\":1,\"early_quits\":3,\"ticks\":"<<ticks
        <<",\"distinct_pages\":"<<pages.size()<<",\"original_physics_rate\":true,\"original_opponents_selected\":true,\"return_to_main_menu\":true,\"captured_state_used\":false,\"cpu_emulation\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
