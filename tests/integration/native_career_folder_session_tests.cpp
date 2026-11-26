#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "session/native_game_session.h"
#include "persistence/player_data_layout.h"
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace scr::runtime;namespace fs=std::filesystem;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
NativeGameInput input(unsigned controls=0,bool back=false,unsigned number=0){NativeGameInput value;value.menu={std::uint8_t(controls),std::uint8_t(number),back};return value;}
void tap(NativeGameSession& game,unsigned controls=16,bool back=false,unsigned number=0){game.update(0,{});game.update(0,input(controls,back,number));game.update(0,{});}
void choose(NativeGameSession& game,unsigned row){tap(game,0,false,1u<<row);}
std::unique_ptr<NativeGameSession> open(const fs::path& pack,const fs::path& root){
    auto game=std::make_unique<NativeGameSession>(pack,fs::path{});const auto layout=playerDataLayout(root);
    game->enableCareers(root,layout.originalHall,layout.enhancedHall);game->setNavigation(NativeMenuNavigation::Responsive);game->update(0,{});return game;
}
void create(NativeGameSession& game){
    tap(game);tap(game);check(game.enteringName(),"Folder career setup did not request a driver");
    for(unsigned c:std::string("CRAIG")){NativeGameInput key;key.rawKeys.push_back({*nativeAmigaRawKey(c,0,false),true});game.update(0,key);key.rawKeys[0].pressed=false;game.update(0,key);}
    tap(game);tap(game);check(game.activeCareer().has_value(),"Folder career was not created");
}
void image(NativeGameSession& game,const fs::path& path){
    fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file<<"P6\n320 200\n255\n";
    for(const auto colour:game.render())for(unsigned shift:{16u,8u,0u})file.put(char(colour>>shift));
}
std::vector<std::uint8_t> progress(const NativeMemory& memory){
    std::vector<std::uint8_t> result(memory.begin()+0x1c9c2,memory.begin()+0x1ca44);
    for(const auto& range:{std::pair<unsigned,unsigned>{0x625a2,5},{0x636b8,4}})result.insert(result.end(),memory.begin()+range.first,memory.begin()+range.first+range.second);return result;
}
std::map<fs::path,std::string> snapshot(const fs::path& root){
    std::map<fs::path,std::string> result;for(const auto& file:fs::recursive_directory_iterator(root))if(file.is_regular_file()){
        std::ifstream in(file.path(),std::ios::binary);result[file.path().lexically_relative(root)]={std::istreambuf_iterator<char>(in),{}};}return result;
}
void scenario(const fs::path& pack,const fs::path& root,NativeMenuNavigation navigation){
    const auto data=root/L"data";auto game=open(pack,data);create(*game);const auto id=game->activeCareer()->id;
    choose(*game,2);tap(*game);check(game->page()==NativeMenuPage::Matchup,"Fixture was not prepared");const auto fixture=progress(game->memory());
    const auto fixtureMemory=std::make_unique<NativeMemory>(game->memory());
    tap(*game,0,true);check(game->hasSuspendedSeason(),"Season did not suspend before copying");check(game->requestClose(),"Initial folder career did not close");game.reset();
    NativeCareerInfo original;
    {NativeCareerStore store(data,{}, {});check(store.list().size()==1,"Fresh folder discovery failed");original=store.list().front();}
    const auto controlData=root/L"control";fs::copy(data,controlData,fs::copy_options::recursive);
    auto control=open(pack,controlData);tap(*control);tap(*control);choose(*control,2);tap(*control);
    check(control->page()==NativeMenuPage::Matchup&&control->memory()==*fixtureMemory,"Ordinary Resume changed full native memory");
    fs::copy(original.folder,data/L"careers/incoming",fs::copy_options::recursive);const auto sources=snapshot(data/L"careers"),originalFiles=snapshot(original.folder);
    game=open(pack,data);tap(*game);check(game->careerHome()&&game->menuState().count==2,"Copied folder hid Continue");
    game->setNavigation(navigation);const auto unchanged=std::make_unique<NativeMemory>(game->memory());
    choose(*game,2);tap(*game);image(*game,root/L"browse.ppm");
    check(game->render()==game->render()&&game->memory()==*unchanged,"Browsing changed native state");
    check(snapshot(data/L"careers")==sources,"Discovery rewrote copied files");
    // Equal timestamps sort by folder. Select the incoming copy directly.
    unsigned row=0;{NativeCareerStore store(data,{}, {});const auto entries=store.list();
        check(entries.size()==2,"Copy was not independently discoverable");for(unsigned n=0;n<entries.size();++n)if(entries[n].folder.filename()==L"incoming")row=n;}
    for(unsigned n=0;n<row;++n)tap(*game,2);tap(*game);image(*game,root/L"loaded.ppm");
    check(game->activeCareer()&&game->activeCareer()->id==id&&game->activeCareer()->folder.filename()==L"incoming"&&game->hasSuspendedSeason(),"Choosing copied career added an action screen or lost continuation");
    game->setNavigation(NativeMenuNavigation::Responsive);choose(*game,2);tap(*game);check(game->page()==NativeMenuPage::Matchup&&progress(game->memory())==fixture,"Resume after copy changed fixture, damage, progression or RNG");
    check(game->memory()==*fixtureMemory,"Resume after copy changed full native memory");
    check(game->sharedHall().state==HallArchiveState::Missing&&game->enhancedHall().state==HallArchiveState::Missing,"Copied checkpoints minted Hall attempts");
    tap(*game);tap(*control);tap(*game);tap(*control);
    check(game->page()==NativeMenuPage::Race&&game->memory()==control->memory(),"Copied career prepared a different race");
    for(unsigned n=0;n<16;++n){game->update(.12,{});control->update(.12,{});check(game->memory()==control->memory(),"Copied career diverged during subsequent simulation updates");}
    check(game->race().liveTicks()==16&&game->render()==control->render(),"Copied career changed simulation timing or presentation");
    NativeGameInput leave;leave.leaveRace=true;game->update(0,leave);check(game->requestClose(),"Copied career did not close");
    check(snapshot(original.folder)==originalFiles,"Saving a copy changed its sibling");

}
}
int main(int argc,char** argv)try{
    check(argc==3,"Usage: native_career_folder_session_tests pack isolated-output");const fs::path root=argv[2];
    scenario(argv[1],root/L"responsive",NativeMenuNavigation::Responsive);
    scenario(argv[1],root/L"original",NativeMenuNavigation::Original);
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"both_navigation_modes\":true,\"direct_folder_selection\":true,\"exact_season_rng\":true,\"full_native_memory_and_subsequent_updates\":true,\"visual_states\":4}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
