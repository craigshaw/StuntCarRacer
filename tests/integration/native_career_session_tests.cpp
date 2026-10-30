#include "session/native_game_session.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace scr::runtime;
unsigned races{},ticks{},forfeits{},loads{},saves{},replays{},relegations{};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void settle(NativeGameSession& game){
    for(unsigned n=0;game.transitioning()&&n<20;++n)game.update(1,{});
    require(!game.transitioning(),"Career transition did not finish");
}
void ready(NativeGameSession& game){settle(game);game.update(1,{});game.update(1,{});}
void choose(NativeGameSession& game,unsigned row){ready(game);game.update(0,{{0,std::uint8_t(1u<<row)},0,false});ready(game);}
void confirm(NativeGameSession& game){ready(game);game.update(0,{{16,0},0,false});game.update(1,{});game.update(1,{});settle(game);}
void fire(NativeGameSession& game){settle(game);game.update(0,{});game.update(0.25,{});game.update(0,{{16,0},0,false});settle(game);}
void key(NativeGameSession& game,unsigned vk,bool down){
    const auto raw=nativeAmigaRawKey(vk,0,false);require(raw.has_value(),"Career key mapping missing");
    NativeGameInput input;input.rawKeys.push_back({*raw,down});game.update(0,input);
}
void tap(NativeGameSession& game,unsigned vk){key(game,vk,true);key(game,vk,false);}
void text(NativeGameSession& game,const std::string& value){game.update(0,{});for(unsigned char c:value)tap(game,c);}
void image(NativeGameSession& game,const std::filesystem::path& path){
    std::ofstream out(path,std::ios::binary);out<<"P6\n320 200\n255\n";
    for(auto pixel:game.render())for(unsigned shift:{16u,8u,0u})out.put(char(pixel>>shift));
    require(bool(out),"Cannot export career image");
}
std::array<std::uint8_t,27> career(const NativeGameSession& game){
    std::array<std::uint8_t,27> result{};
    std::copy_n(game.memory().begin()+0x1ed5b,12,result.begin());
    std::copy_n(game.memory().begin()+0x1c9c2,15,result.begin()+12);return result;
}
void seedCareer(const std::filesystem::path& pack,const std::filesystem::path& file,unsigned division,unsigned context){
    auto source=startNativeGame(pack);auto& m=*source.memory;initializeNativeMenuSession(m);
    std::fill_n(m.begin()+0x1ed5b,12,' ');const std::string name="CAREER"+std::to_string(division)+std::to_string(context);
    std::copy(name.begin(),name.end(),m.begin()+0x1ed5b);
    const unsigned offset=m[0x5ab42+division];std::swap(m[0x1c9c2+11],m[0x1c9c2+offset+1]);
    m[0x1c9ce]=std::uint8_t(division);m[0x1c9d0]=std::uint8_t(context);m[0x1c9cf]=7;
    beginNativeSaveTransaction(m,true);const auto payload=makeNativeSavePayload(m,NativeSaveKind::Career);
    std::array<std::uint8_t,8> nameBytes{'C','A','R','E','E','R',' ',' '};commitNativeSaveDirectory(m,0,nameBytes,true);
    NativeSaveBlock directory{};std::copy_n(m.begin()+0x7a21a,512,directory.begin());NativeSaveStore store(file);
    require(!store.writeSlot(0,directory,payload),"Cannot store career fixture");
}
void slots(NativeGameSession& game,bool write){
    choose(game,3);confirm(game);choose(game,write?1:0);confirm(game);fire(game);
    if(game.savePhase()==NativeSaveMenuPhase::FormatWarning)fire(game);game.update(0,{});
    require(game.savePhase()==(write?NativeSaveMenuPhase::SaveName:NativeSaveMenuPhase::LoadSelection),"Career slot screen missing");
}
void slot(NativeGameSession& game,unsigned index){
    for(unsigned n=0;n<30&&game.memory()[0x62bb4]!=index;++n)tap(game,0x28);
    require(game.memory()[0x62bb4]==index,"Career slot navigation failed");
}
void returnFromSave(NativeGameSession& game){
    settle(game);require(game.page()==NativeMenuPage::Roster,"Career save/load omitted driver roster");fire(game);
    require(game.page()==NativeMenuPage::Main,"Career save/load did not return to main");
}
void load(NativeGameSession& game,unsigned index){slots(game,false);slot(game,index);tap(game,0x0d);returnFromSave(game);++loads;}
void save(NativeGameSession& game){
    slots(game,true);slot(game,1);for(unsigned n=0;n<8;++n)tap(game,0x08);text(game,"CONTINUE");tap(game,0x0d);returnFromSave(game);++saves;
}
void startSeason(NativeGameSession& game){
    choose(game,2);confirm(game);require(game.page()==NativeMenuPage::Matchup,"Career did not start season");
}
void drive(NativeGameSession& game,const std::filesystem::path& folder){
    fire(game);require(game.page()==NativeMenuPage::Preview,"Career matchup omitted preview");fire(game);
    require(game.page()==NativeMenuPage::Race&&game.race().liveTicks()==0,"Career race did not start fresh");
    {std::ofstream seed(folder/"native-initial.bin",std::ios::binary);seed.write(reinterpret_cast<const char*>(game.memory().data()),game.memory().size());}
    const auto recording=game.race().liveRecording();
    for(unsigned n=0;n<64;++n){game.update(0.12,{{},0,false});++ticks;}
    require(game.race().liveTicks()==64,"Career changed physics update rate");image(game,folder/"race.ppm");
    for(unsigned n=0;n<600&&game.page()==NativeMenuPage::Race;++n){
        const auto before=game.race().liveTicks();game.update(0.12,{{},0,true});ticks+=game.race().liveTicks()-before;
        if(game.raceEnded()&&!game.transitioning())fire(game);
    }
    settle(game);require(game.page()==NativeMenuPage::Results,"Career race omitted result artwork");
    std::filesystem::copy_file(recording,folder/"controls.csv",std::filesystem::copy_options::overwrite_existing);
    auto trace=recording.string();trace.replace(trace.size()-13,13,"-native.csv");
    std::filesystem::copy_file(trace,folder/"native.csv",std::filesystem::copy_options::overwrite_existing);
    for(unsigned n=0;n<8&&game.page()!=NativeMenuPage::Matchup;++n)fire(game);
    require(game.page()==NativeMenuPage::Matchup,"Career result pages did not reach next fixture");++races;
}
void forfeit(NativeGameSession& game,const std::filesystem::path& folder){
    const auto humanRaces=game.memory()[0x1c9f6+11],round=game.memory()[0x1ca35];
    require(round<6,"F1 fixture is outside season");
    const auto records=std::vector<std::uint8_t>(game.memory().begin()+0x7a61a,game.memory().begin()+0x7a81a);
    key(game,0x70,true);fire(game);key(game,0x70,false);
    require(game.page()==NativeMenuPage::SeasonEnd,"F1 did not skip remaining single-player fixtures");
    require(game.memory()[0x57c68]==0&&game.memory()[0x1bb5b]==0xc0&&game.memory()[0x1bbb4]==0xc0,"F1 lost original forfeit flags");
    require(game.memory()[0x1ca35]==0&&game.memory()[0x57c72]==0&&game.memory()[0x57c73]==6,"F1 did not complete six original fixtures");
    require(game.memory()[0x1c9f6+11]==4&&humanRaces<4,"F1 omitted or double-scored human fixtures");
    require(std::equal(records.begin(),records.end(),game.memory().begin()+0x7a61a),"F1 changed track records without racing");
    image(game,folder/"season-end.ppm");const bool award=!(game.memory()[0x593f6]&0x80);fire(game);
    require(game.page()==(award?NativeMenuPage::SeasonAward:NativeMenuPage::Roster),"Career award branch differs from original");
    if(award)fire(game);require(game.page()==NativeMenuPage::Roster,"Career end omitted roster");fire(game);
    require(game.page()==NativeMenuPage::Main&&game.memory()[0x1ca35]==0&&game.memory()[0x57c73]==0,"Career end did not reset race counters");
    for(unsigned n=0;n<48;++n)require(game.memory()[0x1c9de+n]==0,"Career end left old score tables");++forfeits;
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::invalid_argument("native_career_session_tests asset-pack output-directory");
    const std::filesystem::path root=argv[2];std::filesystem::create_directories(root);
    for(unsigned context:{0u,11u})for(unsigned division=0;division<4;++division){
        const auto folder=root/("career-"+std::to_string(context)+"-"+std::to_string(division));std::filesystem::create_directories(folder);
        const auto file=folder/"career.scr-save";std::filesystem::remove(file);seedCareer(argv[1],file,division,context);
        NativeGameSession game(argv[1],folder/"recordings",NativeGameStart::EstablishedMenu,file);load(game,0);
        require(game.memory()[0x1c9ce]==division&&game.memory()[0x1c9d0]==context&&game.memory()[0x1c9cf]==7,"Career load lost division, damage or car context");
        startSeason(game);drive(game,folder);forfeit(game,folder);
        require(game.memory()[0x1c9d0]==context&&game.memory()[0x1c9ce]==(division?division-1:0),"Forfeited career did not relegate correctly");
        if(division)++relegations;
        const auto snapshot=career(game);save(game);require(career(game)==snapshot,"Saving changed career state");
        NativeGameSession restored(argv[1],folder/"restored-recordings",NativeGameStart::EstablishedMenu,file);load(restored,1);
        require(career(restored)==snapshot,"Fresh launch did not restore continued career");
        startSeason(restored);forfeit(restored,folder);
        choose(restored,3);confirm(restored);choose(restored,2);confirm(restored);returnFromSave(restored);
        require(career(restored)==snapshot,"Replay did not restore the retained start-of-season career");++replays;
    }
    std::cout<<"{\"status\":\"passed\",\"career_contexts\":8,\"real_races\":"<<races<<",\"ticks\":"<<ticks<<",\"forfeited_seasons\":"<<forfeits
        <<",\"relegations\":"<<relegations<<",\"native_saves\":"<<saves<<",\"native_loads\":"<<loads<<",\"replay_routes\":"<<replays
        <<",\"fresh_launch_continuation\":true,\"original_physics_rate\":true,\"capture_input\":false,\"synthetic_career_fixtures\":true,\"cpu_emulation\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
