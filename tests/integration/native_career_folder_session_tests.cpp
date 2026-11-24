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
void scenario(const fs::path& pack,const fs::path& root,NativeMenuNavigation navigation,bool keepBoth){
    const auto data=root/L"data";auto game=open(pack,data);create(*game);const auto id=game->activeCareer()->id;
    choose(*game,2);tap(*game);check(game->page()==NativeMenuPage::Matchup,"Fixture was not prepared");const auto fixture=progress(game->memory());
    const auto fixtureMemory=std::make_unique<NativeMemory>(game->memory());
    tap(*game,0,true);check(game->hasSuspendedSeason(),"Season did not suspend before copying");check(game->requestClose(),"Initial folder career did not close");game.reset();
    NativeCareerInfo original;
    {NativeCareerStore store(data,{}, {CareerStorageLayout::Folders,{},{}});check(store.list().size()==1,"Fresh folder discovery failed");original=store.list().front();}
    const auto controlData=root/L"control";FolderTransactions::copy(data,controlData);
    auto control=open(pack,controlData);tap(*control);tap(*control);choose(*control,2);tap(*control);
    check(control->page()==NativeMenuPage::Matchup&&control->memory()==*fixtureMemory,"Ordinary Resume changed full native memory");
    FolderTransactions::copy(original.folder,data/L"careers/incoming");const auto sources=snapshot(data/L"careers");
    game=open(pack,data);tap(*game);check(game->careerHome()&&game->menuState().count==1,"Conflicted last-used identity exposed Continue");
    game->setNavigation(navigation);const auto unchanged=std::make_unique<NativeMemory>(game->memory());
    choose(*game,1);tap(*game);image(*game,root/L"browse.ppm");tap(*game);
    check(game->menuState().selected==2,"Copy conflict did not default to Cancel");image(*game,root/L"conflict-cancel.ppm");
    const auto firstFrame=game->render();check(game->render()==firstFrame&&game->memory()==*unchanged,"Rendering a conflict mutated native state");
    tap(*game);check(snapshot(data/L"careers")==sources,"Cancel changed a copied career");
    tap(*game,2);tap(*game);choose(*game,keepBoth?1:0);image(*game,root/L"conflict-selected.ppm");tap(*game);
    for(unsigned n=0;game->careerCopyPending()&&n<10000;++n){game->update(0,{});Sleep(1);}
    check(!game->careerCopyPending()&&game->memory()==*unchanged,"Copy resolution changed native state or never completed");image(*game,root/L"resolved.ppm");
    {NativeCareerStore store(data,{}, {CareerStorageLayout::Folders,{},{}});
        check(store.list().size()==(keepBoth?2u:1u),"Conflict action retained the wrong number of identities");
        for(const auto& entry:store.list())check(entry.state==CareerFileState::Valid,"Resolution left a conflict/unavailable entry");
    }
    game->setNavigation(NativeMenuNavigation::Responsive);tap(*game);tap(*game);
    check(game->activeCareer().has_value()&&game->hasSuspendedSeason(),"Resolved career lost its season continuation");
    check(keepBoth?game->activeCareer()->id!=id:game->activeCareer()->id==id,"Conflict action assigned the wrong identity");
    choose(*game,2);tap(*game);check(game->page()==NativeMenuPage::Matchup&&progress(game->memory())==fixture,"Resume after copy changed fixture, damage, progression or RNG");
    check(game->memory()==*fixtureMemory,"Resume after copy changed full native memory");
    check(game->sharedHall().state==HallArchiveState::Missing&&game->enhancedHall().state==HallArchiveState::Missing,"Copied checkpoints minted Hall attempts");
    tap(*game);tap(*control);tap(*game);tap(*control);
    check(game->page()==NativeMenuPage::Race&&game->memory()==control->memory(),"Copied career prepared a different race");
    for(unsigned n=0;n<16;++n){game->update(.12,{});control->update(.12,{});check(game->memory()==control->memory(),"Copied career diverged during subsequent simulation updates");}
    check(game->race().liveTicks()==16&&game->render()==control->render(),"Copied career changed simulation timing or presentation");
    const auto loaded=game->activeCareer()->id;NativeGameInput leave;leave.leaveRace=true;game->update(0,leave);
    tap(*game,0,true);tap(*game,0,true);check(game->careerHome(),"Resolved career did not return home");
    choose(*game,2);tap(*game);tap(*game);choose(*game,1);tap(*game);choose(*game,1);tap(*game);
    const bool closed=game->requestClose();if(!closed)check(game->closePending(),"Close did not wait for the deletion worker");
    for(unsigned n=0;game->careerCopyPending()&&n<10000;++n){game->update(0,{});Sleep(1);}
    check(game->closeReady()&&!game->activeCareer(),"Background deletion did not finish the pending close");
    {NativeCareerStore store(data,{}, {CareerStorageLayout::Folders,{},{}});check(store.readCareer(loaded).state==CareerFileState::Missing,"Deleted career was rediscovered from recovery");}
}
}
int main(int argc,char** argv)try{
    check(argc==3,"Usage: native_career_folder_session_tests pack isolated-output");const fs::path root=argv[2];
    scenario(argv[1],root/L"responsive",NativeMenuNavigation::Responsive,true);
    scenario(argv[1],root/L"original",NativeMenuNavigation::Original,false);
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"both_navigation_modes\":true,\"cancel_replace_keep_both\":true,\"exact_season_rng\":true,\"full_native_memory_and_subsequent_updates\":true,\"visual_states\":8}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
