#include "native_game_session.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace scr::viewer;
unsigned names{},routes{},ticks{},snapshots{},hallRoutes{};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void settle(NativeGameSession& game){
    for(unsigned n=0;game.transitioning()&&n<16;++n)game.update(1,{});
    require(!game.transitioning(),"Opening palette transition did not finish");
}
void ready(NativeGameSession& game){settle(game);game.update(1,{});game.update(1,{});}
NativeGameUpdate confirm(NativeGameSession& game){
    ready(game);game.update(0,{{16,0},0,false});return game.update(1,{});
}
void choose(NativeGameSession& game,unsigned row){
    ready(game);game.update(0,{{0,std::uint8_t(1u<<row)},0,false});ready(game);
}
void key(NativeGameSession& game,unsigned vk,bool pressed,unsigned scan=0,bool extended=false){
    const auto raw=nativeAmigaRawKey(vk,scan,extended);require(raw.has_value(),"Host key has no Amiga mapping");
    NativeGameInput input;input.rawKeys.push_back({*raw,pressed});game.update(0,input);
}
void tap(NativeGameSession& game,unsigned vk){key(game,vk,true);key(game,vk,false);}
void text(NativeGameSession& game,const std::string& value){
    game.update(0,{});
    for(unsigned char c:value){
        const bool upper=c>='A'&&c<='Z';
        if(upper)key(game,0x10,true,0x2a);
        unsigned vk=c;
        if(c>='a'&&c<='z')vk=c-'a'+'A';
        else if(c=='.')vk=0xbe;
        else if(c=='/')vk=0xbf;
        else if(c==':')vk=0xba;
        tap(game,vk);
        if(upper)key(game,0x10,false,0x2a);
    }
}
void finishName(NativeGameSession& game){tap(game,0x0d);++names;}
void fire(NativeGameSession& game){
    settle(game);game.update(0,{});game.update(0.25,{});game.update(0,{{16,0},0,false});settle(game);
}
void image(NativeGameSession& game,const std::filesystem::path& path){
    const auto& pixels=game.render();require(pixels.size()==64000,"Opening canvas size differs");
    std::ofstream out(path,std::ios::binary);
    const auto u16=[&](unsigned x){out.put(char(x));out.put(char(x>>8));};
    const auto u32=[&](unsigned x){u16(x);u16(x>>16);};
    out.write("BM",2);u32(54+320*200*4);u32(0);u32(54);u32(40);u32(320);u32(unsigned(-200));u16(1);u16(32);
    u32(0);u32(320*200*4);u32(2835);u32(2835);u32(0);u32(0);for(auto pixel:pixels)u32(pixel);
    require(bool(out),"Cannot export opening image");++snapshots;
}
void hall(NativeGameSession& game,const std::filesystem::path& path){
    const std::string before(game.memory().begin()+0x1ecaa,game.memory().begin()+0x1edaa);
    choose(game,0);confirm(game);settle(game);
    require(game.page()==NativeMenuPage::HallOfFame,"Named startup did not enter Hall of Fame");
    image(game,path);fire(game);
    require(game.page()==NativeMenuPage::Main,"Hall of Fame did not return to the named main menu");
    for(unsigned i=0;i<before.size();++i)
        require(game.memory()[0x1ecaa+i]==std::uint8_t(before[i]),"Hall of Fame changed entered player names");
    ++hallRoutes;
}
std::string player(const NativeGameSession& game,unsigned additional){
    const auto a=0x1ecab+16*(11-additional);return std::string(game.memory().begin()+a,game.memory().begin()+a+12);
}
void openName(NativeGameSession& game,unsigned mode){
    settle(game);require(game.openingPage()==NativeOpeningPage::Mode,"Original chooser was skipped");
    choose(game,mode);confirm(game);require(game.enteringName(),"Mode did not open name entry");game.update(0,{});
}
void drive(NativeGameSession& game,const std::filesystem::path& folder,bool competitive){
    if(competitive){choose(game,2);confirm(game);require(game.page()==NativeMenuPage::Matchup,"Named season did not enter matchup");fire(game);}
    else{
        choose(game,1);confirm(game);require(game.page()==NativeMenuPage::Division,"Named Practice did not open divisions");
        confirm(game);require(game.page()==NativeMenuPage::Track,"Named Practice did not open tracks");confirm(game);game.update(1,{});settle(game);
    }
    require(game.page()==NativeMenuPage::Preview,"Named race did not enter overview");fire(game);
    require(game.page()==NativeMenuPage::Race,"Named overview did not start race");
    std::filesystem::create_directories(folder);
    std::ofstream seed(folder/"native-initial.bin",std::ios::binary);
    seed.write(reinterpret_cast<const char*>(game.memory().data()),game.memory().size());seed.close();
    const auto recording=game.race().liveRecording();
    for(unsigned n=0;n<64;++n){game.update(0.12,{{},1,false});++ticks;}
    require(game.race().liveTicks()==64,"Opening changed original physics rate");image(game,folder/"race.bmp");
    for(unsigned n=0;n<600&&game.page()==NativeMenuPage::Race;++n){
        const auto before=game.race().liveTicks();game.update(0.12,{{},1,true});ticks+=game.race().liveTicks()-before;
        if(game.raceEnded())fire(game);
    }
    settle(game);require(game.page()!=NativeMenuPage::Race,"Named race did not exit");
    std::filesystem::copy_file(recording,folder/"controls.csv",std::filesystem::copy_options::overwrite_existing);
    auto trace=recording.string();trace.replace(trace.size()-13,13,"-native.csv");
    std::filesystem::copy_file(trace,folder/"native.csv",std::filesystem::copy_options::overwrite_existing);
    ++routes;
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::invalid_argument("native_opening_session_tests asset-pack output-directory");
    const std::filesystem::path root=argv[2];std::filesystem::create_directories(root);
    {
        NativeGameSession game(argv[1],root/"single-recordings");settle(game);image(game,root/"mode.bmp");
        openName(game,0);image(game,root/"name-empty.bmp");tap(game,0x0d);
        require(game.enteringName(),"Blank name bypassed original retry");game.update(0,{});
        tap(game,0x20);text(game,"Craix");tap(game,0x08);text(game,"g");
        require(player(game,0).substr(0,5)=="Craig","Case or Backspace differs after host raw-key mapping");
        tap(game,0x08);tap(game,0x08);tap(game,0x08);tap(game,0x08);tap(game,0x08);tap(game,0x08);
        text(game,"MI wasd");image(game,root/"name-typing.bmp");
        for(unsigned n=0;n<7;++n)tap(game,0x08);
        text(game,"Craig");finishName(game);settle(game);
        require(game.openingPage()==NativeOpeningPage::Roster,"Single player did not enter original driver roster");
        image(game,root/"roster.bmp");fire(game);
        require(!game.openingActive()&&game.page()==NativeMenuPage::Main,"Roster did not enter main menu");
        require(player(game,0)=="Craig       ","Completed name was not padded/preserved");
        require(game.memory()[0x57c64]==0xf3,"Opening protocol finalization is missing");image(game,root/"main.bmp");
        hall(game,root/"hall-of-fame.bmp");
        drive(game,root/"single-practice",false);require(player(game,0)=="Craig       ","Practice exit lost entered name");
        choose(game,2);confirm(game);require(game.page()==NativeMenuPage::Main,"Practice Cancel did not return main menu");
        drive(game,root/"single-season",true);require(player(game,0)=="Craig       ","Season results lost entered name");
    }
    for(unsigned count=2;count<=8;++count){
        const auto folder=root/("local-"+std::to_string(count));std::filesystem::create_directories(folder);
        NativeGameSession game(argv[1],folder/"recordings");openName(game,1);
        for(unsigned i=0;i<count;++i){
            const auto value=i==count-1?"Player123456789":"Player"+std::to_string(i+1);
            text(game,value);finishName(game);
            const auto expected=value.substr(0,12)+std::string(12-std::min<std::size_t>(12,value.size()),' ');
            require(player(game,i)==expected,"Local names or twelve-character limit differ");
            if(i+1<count){
                require(game.openingPage()==NativeOpeningPage::AddPlayer,"Missing original add-player menu");
                if(!i){choose(game,1);confirm(game);require(game.openingPage()==NativeOpeningPage::AddPlayer,"One-player multiplayer setup was accepted");}
                choose(game,0);confirm(game);require(game.enteringName(),"Add player did not request next name");
            }else if(count<8){choose(game,1);confirm(game);}
        }
        require(game.openingPage()==NativeOpeningPage::LocalStandings&&!game.transitioning(),"Local startup added a fade or omitted standings");
        image(game,folder/"standings.bmp");fire(game);
        require(game.page()==NativeMenuPage::Main&&game.memory()[0x5eb76]==count-1,"Local setup did not preserve player count");
        hall(game,folder/"hall-of-fame.bmp");
        if(count==2)drive(game,root/"local-practice",false);
        else{
            choose(game,2);const auto started=confirm(game);
            require(started.unavailable==NativeGameUnavailable::none,"Local championship remained unavailable");
            require(game.page()==NativeMenuPage::Matchup&&game.memory()[0x5eb76]==count-1,"Local championship changed player setup");
        }
    }
    {
        NativeGameSession game(argv[1],root/"link-recordings");settle(game);choose(game,2);
        require(confirm(game).unavailable==NativeGameUnavailable::computerLink,"Computer Link lacked its boundary");
        require(game.openingPage()==NativeOpeningPage::Mode,"Unavailable link did not return to mode chooser");
    }
    std::cout<<"{\"status\":\"passed\",\"single_player_startups\":1,\"local_startups\":7,\"entered_names\":"<<names
        <<",\"named_race_routes\":"<<routes<<",\"ticks\":"<<ticks<<",\"snapshots\":"<<snapshots
        <<",\"hall_of_fame_routes\":"<<hallRoutes
        <<",\"blank_retry\":true,\"backspace\":true,\"case_and_host_keys\":true,\"maximum_length\":12"
        <<",\"local_player_counts\":[2,3,4,5,6,7,8],\"explicit_transport_boundary\":true"
        <<",\"capture_input\":false,\"cpu_emulation\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
