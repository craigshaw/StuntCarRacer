#include "native_game_session.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
using namespace scr::viewer;
unsigned races{},ticks{},seasons{},forfeits{},completed{},loads{},saves{};
std::set<unsigned> drivers,pages;
void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
void settle(NativeGameSession& game){
    for(unsigned n=0;game.transitioning()&&n<20;++n)game.update(1,{});
    require(!game.transitioning(),"Multiplayer palette transition did not finish");
}
void ready(NativeGameSession& game){settle(game);game.update(1,{});game.update(1,{});}
void choose(NativeGameSession& game,unsigned row){ready(game);game.update(0,{{0,std::uint8_t(1u<<row)},0,false});ready(game);}
void confirm(NativeGameSession& game){ready(game);game.update(0,{{16,0},0,false});game.update(1,{});game.update(1,{});settle(game);}
void fire(NativeGameSession& game){settle(game);game.update(0,{});game.update(0.25,{});game.update(0,{{16,0},0,false});settle(game);}
void key(NativeGameSession& game,unsigned vk,bool down){
    const auto raw=nativeAmigaRawKey(vk,0,false);require(raw.has_value(),"Multiplayer test key has no original mapping");
    NativeGameInput input;input.rawKeys.push_back({*raw,down});game.update(0,input);
}
void tap(NativeGameSession& game,unsigned vk){key(game,vk,true);key(game,vk,false);}
void text(NativeGameSession& game,const std::string& value){game.update(0,{});for(unsigned char c:value)tap(game,c);}
void opening(NativeGameSession& game,unsigned count){
    choose(game,1);confirm(game);
    for(unsigned i=0;i<count;++i){
        require(game.enteringName(),"Multiplayer startup omitted a driver name");text(game,"DRIVER"+std::to_string(i+1));tap(game,0x0d);
        if(i+1<count){choose(game,0);confirm(game);}else if(count<8){choose(game,1);confirm(game);}
    }
    require(game.openingPage()==NativeOpeningPage::LocalStandings,"Multiplayer startup omitted standings");fire(game);
    require(game.page()==NativeMenuPage::Main&&game.memory()[0x5eb76]==count-1,"Multiplayer startup lost driver count");
}
void image(NativeGameSession& game,const std::filesystem::path& path){
    std::ofstream out(path,std::ios::binary);out<<"P6\n320 200\n255\n";
    for(auto pixel:game.render())for(unsigned shift:{16u,8u,0u})out.put(char(pixel>>shift));
    require(bool(out),"Cannot export multiplayer screenshot");
}
void slots(NativeGameSession& game,bool write){
    choose(game,3);confirm(game);choose(game,write?1:0);confirm(game);fire(game);
    if(game.savePhase()==NativeSaveMenuPhase::FormatWarning)fire(game);game.update(0,{});
    require(game.savePhase()==(write?NativeSaveMenuPhase::SaveName:NativeSaveMenuPhase::LoadSelection),"Multiplayer save selector did not open");
}
void selectSlot(NativeGameSession& game,unsigned index){
    for(unsigned n=0;n<30&&game.memory()[0x62bb4]!=index;++n)tap(game,0x28);
    require(game.memory()[0x62bb4]==index,"Multiplayer save slot did not select");
}
void finishSave(NativeGameSession& game){settle(game);require(game.page()==NativeMenuPage::Standings,"Multiplayer save omitted standings");fire(game);require(game.page()==NativeMenuPage::Main,"Multiplayer save did not return main");}
void load(NativeGameSession& game,unsigned index){slots(game,false);selectSlot(game,index);tap(game,0x0d);finishSave(game);++loads;}
void save(NativeGameSession& game){slots(game,true);selectSlot(game,0);for(unsigned n=0;n<8;++n)tap(game,0x08);text(game,"MP TEST");tap(game,0x0d);finishSave(game);++saves;}
void seedCareer(const std::filesystem::path& pack,const std::filesystem::path& file){
    auto source=startNativeGame(pack);initializeNativeMenuSession(*source.memory);auto& m=*source.memory;
    m[0x1c9ce]=1;m[0x1c9d0]=11;m[0x1c9cf]=7;beginNativeSaveTransaction(m,true);
    const auto payload=makeNativeSavePayload(m,NativeSaveKind::Career);std::array<std::uint8_t,8> name{'C','A','R','E','E','R',' ',' '};
    commitNativeSaveDirectory(m,1,name,true);NativeSaveBlock directory{};std::copy_n(m.begin()+0x7a21a,512,directory.begin());
    NativeSaveStore store(file);require(!store.writeSlot(1,directory,payload),"Cannot create career access fixture");
}
void drive(NativeGameSession& game,const std::filesystem::path& folder,bool natural){
    const auto human=game.memory()[0x5eb79];const auto oldDamage=game.memory()[0x1c9cf];
    const auto damage=game.memory()[0x1ca36+human];
    std::array<std::uint8_t,12> allDamage{};std::copy_n(game.memory().begin()+0x1ca36,12,allDamage.begin());
    image(game,folder/"matchup.ppm");fire(game);require(game.page()==NativeMenuPage::Preview,"Multiplayer matchup did not enter overview");
    fire(game);require(game.page()==NativeMenuPage::Race&&game.race().liveTicks()==0,"Multiplayer overview did not start fresh race");
    require(game.memory()[0x1bbb5]==oldDamage&&game.memory()[0x1c9cf]==damage,"Race did not load only this driver's persistent damage");
    {std::ofstream seed(folder/"native-initial.bin",std::ios::binary);seed.write(reinterpret_cast<const char*>(game.memory().data()),game.memory().size());}
    const auto recording=game.race().liveRecording();
    for(unsigned tick=0;tick<64;++tick){game.update(0.12,{{},0,false});++ticks;}
    require(game.race().liveTicks()==64,"Multiplayer changed original 120ms physics clock");
    require(game.race().frames().back().opponentEnabled,"Multiplayer race omitted pace car rendering");image(game,folder/"race.ppm");
    if(natural){
        for(unsigned n=0;n<3000&&!game.raceEnded();++n){game.update(0.12,{});++ticks;}
        require(game.raceEnded()&&game.race().frames().back().raceComplete,"Multiplayer pace car did not finish race");++completed;fire(game);
    }else for(unsigned n=0;n<600&&game.page()==NativeMenuPage::Race;++n){
        const auto before=game.race().liveTicks();game.update(0.12,{{},0,true});ticks+=game.race().liveTicks()-before;
        if(game.raceEnded()&&!game.transitioning())fire(game);
    }
    settle(game);if(game.page()!=NativeMenuPage::Results)throw std::runtime_error("Multiplayer race omitted results: page="+std::to_string(unsigned(game.page()))+" ticks="+std::to_string(game.race().liveTicks())+" folder="+folder.string());
    require(game.memory()[0x1c9cf]==oldDamage,"Race exit did not restore shared damage backup");
    for(unsigned id=0;id<12;++id)if(id!=human)require(game.memory()[0x1ca36+id]==allDamage[id],"Race exit changed another driver's damage");
    require(game.memory()[0x1c9f6+human]!=0,"Race exit did not score current human");
    std::filesystem::copy_file(recording,folder/"controls.csv",std::filesystem::copy_options::overwrite_existing);
    auto trace=recording.string();trace.replace(trace.size()-13,13,"-native.csv");
    std::filesystem::copy_file(trace,folder/"native.csv",std::filesystem::copy_options::overwrite_existing);++races;
}
void season(NativeGameSession& game,const std::filesystem::path& root,unsigned count,bool natural){
    const auto division=game.memory()[0x1c9ce],context=game.memory()[0x1c9d0],before=game.memory()[0x57c67];
    choose(game,2);confirm(game);unsigned played=0,trackSummaries=0,raceSummaries=0;
    for(unsigned actions=0;actions<220&&game.page()!=NativeMenuPage::Main;++actions){
        pages.insert(unsigned(game.page()));
        if(game.page()==NativeMenuPage::Matchup){
            const auto& m=game.memory();const auto expected=11-(played%count);const auto track=played/count;
            require(m[0x5eb79]==expected&&m[0x1ca27]==expected,"Multiplayer turn order differs from original");drivers.insert(expected);
            require(m[0x1ca29]==1-track&&m[0x1ca28]==1-track,"Multiplayer selected wrong track's pace car");
            require(m[0x1ca33]==m[0x648c2+2*division+track],"Multiplayer selected wrong season track");
            const auto folder=root/("race-"+std::to_string(played));std::filesystem::create_directories(folder);
            drive(game,folder,natural&&played==0);++played;
        }else{
            if(game.page()==NativeMenuPage::TrackResults)++trackSummaries;
            if(game.page()==NativeMenuPage::RaceResults)++raceSummaries;
            image(game,root/("page-"+std::to_string(played)+"-"+std::to_string(unsigned(game.page()))+".ppm"));fire(game);
        }
    }
    require(game.page()==NativeMenuPage::Main&&played==2*count,"Multiplayer did not complete both tracks for every driver");
    require(trackSummaries==2&&raceSummaries==2*count,"Multiplayer results page sequence differs");
    require(game.memory()[0x1c9ce]==division&&game.memory()[0x1c9d0]==context,"Multiplayer applied promotion or changed car context");
    require(game.memory()[0x57c67]==(before==3?0:before+1),"Multiplayer championship counter did not advance/reset");
    if(before==3){for(unsigned n=0;n<48;++n)require(game.memory()[0x1c9de+n]==0,"Fourth championship did not reset score tables");}
    ++seasons;
}
void forfeit(NativeGameSession& game,unsigned count){
    choose(game,2);confirm(game);require(game.page()==NativeMenuPage::Matchup,"Forfeit season did not start");
    for(unsigned track=0;track<2;++track){
        const auto before=unsigned(game.memory()[0x57c72])*256+game.memory()[0x57c73];
        key(game,0x70,true);fire(game);key(game,0x70,false);
        require(game.page()==NativeMenuPage::TrackResults,"F1 did not skip remaining drivers on this track");
        const auto after=unsigned(game.memory()[0x57c72])*256+game.memory()[0x57c73];
        require(after==before+count,"F1 did not score every skipped driver");++forfeits;
        fire(game);require(game.page()==NativeMenuPage::Standings,"Forfeit omitted track-end standings");fire(game);
    }
    require(game.page()==NativeMenuPage::Main,"Forfeit did not return main after both tracks");
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::invalid_argument("native_multiplayer_session_tests asset-pack output-directory");
    const std::filesystem::path root=argv[2];std::filesystem::create_directories(root);
    for(unsigned count=2;count<=8;++count){
        const auto folder=root/("local-"+std::to_string(count));std::filesystem::create_directories(folder);
        const auto file=folder/"session.scr-save";std::filesystem::remove(file);if(count==3)seedCareer(argv[1],file);
        NativeGameSession game(argv[1],folder/"recordings",NativeGameStart::OriginalOpening,file);opening(game,count);
        if(count==3){
            const auto names=std::vector<std::uint8_t>(game.memory().begin()+0x1ecea,game.memory().begin()+0x1ed6a);load(game,1);
            require(game.memory()[0x1c9ce]==1&&game.memory()[0x1c9d0]==11&&game.memory()[0x5eb76]==2,"Career load did not unlock correct multiplayer tracks and context");
            require(std::equal(names.begin(),names.end(),game.memory().begin()+0x1ecea),"Career access load replaced local driver names");
        }
        season(game,folder,count,count==2);
        if(count==2)for(unsigned n=1;n<4;++n){const auto next=folder/("season-"+std::to_string(n));std::filesystem::create_directories(next);season(game,next,count,false);}
        if(count==3){
            save(game);const auto state=std::vector<std::uint8_t>(game.memory().begin()+0x1c9de,game.memory().begin()+0x1ca1a);
            NativeGameSession restored(argv[1],folder/"restored-recordings",NativeGameStart::OriginalOpening,file);opening(restored,2);load(restored,0);
            require(restored.memory()[0x5eb76]==2&&std::equal(state.begin(),state.end(),restored.memory().begin()+0x1c9de),"MP load did not continue saved scores and count");
            const auto next=folder/"restored-season";std::filesystem::create_directories(next);season(restored,next,3,false);
        }
        forfeit(game,count);
    }
    require(drivers.size()==8,"Local races did not exercise every original human ID");
    for(auto page:{NativeMenuPage::Results,NativeMenuPage::RoundResults,NativeMenuPage::Standings,NativeMenuPage::RaceResults,NativeMenuPage::TrackResults})
        require(pages.count(unsigned(page))!=0,"Local championship omitted results page");
    std::cout<<"{\"status\":\"passed\",\"player_counts\":[2,3,4,5,6,7,8],\"human_ids\":"<<drivers.size()<<",\"human_races\":"<<races
        <<",\"seasons\":"<<seasons<<",\"forfeited_tracks\":"<<forfeits<<",\"pace_car_finishes\":"<<completed<<",\"ticks\":"<<ticks
        <<",\"native_saves\":"<<saves<<",\"native_loads\":"<<loads<<",\"original_physics_rate\":true,\"per_driver_damage\":true,\"career_track_access\":true,\"four_season_reset\":true,\"capture_input\":false,\"cpu_emulation\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
