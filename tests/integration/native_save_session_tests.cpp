#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_game_session.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace scr::viewer;
unsigned saved{},loaded{},races{},ticks{},replays{};
void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
void settle(NativeGameSession& game){
    for(unsigned n=0;game.transitioning()&&n<20;++n)game.update(1,{});
    require(!game.transitioning(),"Save palette transition did not finish");
}
void ready(NativeGameSession& game){settle(game);game.update(1,{});game.update(1,{});}
void choose(NativeGameSession& game,unsigned row){ready(game);game.update(0,{{0,std::uint8_t(1u<<row)},0,false});ready(game);}
void confirm(NativeGameSession& game){ready(game);game.update(0,{{16,0},0,false});game.update(1,{});game.update(1,{});settle(game);}
void fire(NativeGameSession& game){settle(game);game.update(0,{});game.update(0.25,{});game.update(0,{{16,0},0,false});game.update(0,{});settle(game);}
void key(NativeGameSession& game,unsigned vk,bool down,unsigned scan=0){
    const auto raw=nativeAmigaRawKey(vk,scan,false);require(raw.has_value(),"Save test key has no original mapping");
    NativeGameInput input;input.rawKeys.push_back({*raw,down});game.update(0,input);
}
void tap(NativeGameSession& game,unsigned vk){key(game,vk,true);key(game,vk,false);}
void text(NativeGameSession& game,const std::string& value){
    game.update(0,{});
    for(unsigned char c:value){
        const bool upper=c>='A'&&c<='Z';if(upper)key(game,VK_SHIFT,true,0x2a);
        tap(game,c>='a'&&c<='z'?c-'a'+'A':c);
        if(upper)key(game,VK_SHIFT,false,0x2a);
    }
}
std::string player(const NativeGameSession& game){return {game.memory().begin()+0x1ed5b,game.memory().begin()+0x1ed67};}
void opening(NativeGameSession& game,const std::string& name){
    settle(game);confirm(game);require(game.enteringName(),"Save relaunch skipped original name entry");
    text(game,name);tap(game,VK_RETURN);settle(game);fire(game);
    require(game.page()==NativeMenuPage::Main,"Named save relaunch did not reach main menu");
}
void slots(NativeGameSession& game,bool write){
    choose(game,3);confirm(game);require(game.page()==NativeMenuPage::LoadSave,"Load/Save/Replay submenu missing");
    choose(game,write?1:0);confirm(game);
    require(game.savePhase()==NativeSaveMenuPhase::InsertDisc,"Save route omitted original media prompt");
    fire(game);
    if(game.savePhase()==NativeSaveMenuPhase::FormatWarning)fire(game);
    game.update(0,{});
    require(game.savePhase()==(write?NativeSaveMenuPhase::SaveName:NativeSaveMenuPhase::LoadSelection),"Save slot selector did not become ready");
}
void slot(NativeGameSession& game,unsigned target){
    for(unsigned n=0;n<30&&game.memory()[0x62bb4]!=target;++n)tap(game,VK_DOWN);
    require(game.memory()[0x62bb4]==target,"Save slot navigation did not wrap to target");
}
void finishReturn(NativeGameSession& game){
    settle(game);
    if(game.page()==NativeMenuPage::Roster||game.page()==NativeMenuPage::Standings)fire(game);
    if(game.page()!=NativeMenuPage::Main)throw std::runtime_error("Save return did not restore main menu: page="+std::to_string(unsigned(game.page()))+
        " phase="+(game.savePhase()?std::to_string(unsigned(*game.savePhase())):"none")+" saved="+std::to_string(saved)+" loaded="+std::to_string(loaded));
}
void save(NativeGameSession& game,unsigned index,const std::string& name){
    slots(game,true);slot(game,index);
    for(unsigned n=0;n<8;++n)tap(game,VK_BACK);
    text(game,name);tap(game,VK_RETURN);finishReturn(game);++saved;
}
void load(NativeGameSession& game,unsigned index){slots(game,false);slot(game,index);tap(game,VK_RETURN);finishReturn(game);++loaded;}
void image(NativeGameSession& game,const std::filesystem::path& path){
    std::ofstream out(path,std::ios::binary);out<<"P6\n320 200\n255\n";
    for(auto pixel:game.render())for(unsigned shift:{16u,8u,0u})out.put(char(pixel>>shift));
    require(bool(out),"Cannot export save screenshot");
}
void seed(NativeMemory& memory,const std::filesystem::path& file,unsigned index,const std::string& name,NativeSaveKind kind){
    beginNativeSaveTransaction(memory,true);const auto payload=makeNativeSavePayload(memory,kind);
    std::array<std::uint8_t,8> title{};title.fill(' ');std::copy_n(name.begin(),std::min(name.size(),title.size()),title.begin());
    commitNativeSaveDirectory(memory,index,title,true);NativeSaveBlock directory{};
    std::copy_n(memory.begin()+0x7a21a,512,directory.begin());NativeSaveStore store(file);
    NativeSaveBlock existing{};require(!store.readDirectory(existing),"Cannot read fixture directory");
    for(unsigned other=0;other<30;++other)if(other!=index)
        std::copy_n(existing.begin()+other*16,8,directory.begin()+other*16);
    require(!store.writeSlot(index,directory,payload),"Cannot store authentic save fixture");
}
void drive(NativeGameSession& game,const std::filesystem::path& folder){
    std::filesystem::create_directories(folder);choose(game,1);confirm(game);confirm(game);confirm(game);game.update(1,{});settle(game);
    require(game.page()==NativeMenuPage::Preview,"Loaded career did not enter track overview");fire(game);
    require(game.page()==NativeMenuPage::Race,"Loaded career did not start native race");
    {std::ofstream seedFile(folder/"native-initial.bin",std::ios::binary);seedFile.write(reinterpret_cast<const char*>(game.memory().data()),game.memory().size());}
    const auto recording=game.race().liveRecording();
    for(unsigned n=0;n<64;++n){game.update(0.12,{{},1,false});++ticks;}
    require(game.race().liveTicks()==64,"Loading changed original physics rate");
    for(unsigned n=0;n<600&&game.page()==NativeMenuPage::Race;++n){
        const auto before=game.race().liveTicks();game.update(0.12,{{},1,true});ticks+=game.race().liveTicks()-before;
        if(game.raceEnded())fire(game);
    }
    settle(game);require(game.page()==NativeMenuPage::Track,"Loaded Practice race failed its exit route");
    std::filesystem::copy_file(recording,folder/"controls.csv",std::filesystem::copy_options::overwrite_existing);
    auto trace=recording.string();trace.replace(trace.size()-13,13,"-native.csv");
    std::filesystem::copy_file(trace,folder/"native.csv",std::filesystem::copy_options::overwrite_existing);
    choose(game,2);confirm(game);++races;
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::invalid_argument("native_save_session_tests asset-pack output-directory");
    const std::filesystem::path root=argv[2];std::filesystem::create_directories(root);
    const auto file=root/"session.scr-save";std::filesystem::remove(file);
    {
        NativeGameSession game(argv[1],root/"first-recordings",NativeGameStart::OriginalOpening,file);opening(game,"Craig");
        choose(game,3);confirm(game);image(game,root/"submenu.ppm");confirm(game);
        require(game.page()==NativeMenuPage::Main,"Default submenu Cancel failed");
        save(game,0,"CAREER");require(player(game)=="Craig       ","Saving changed current driver");
    }
    {
        NativeGameSession game(argv[1],root/"second-recordings",NativeGameStart::OriginalOpening,file);opening(game,"Other");
        load(game,0);require(player(game)=="Craig       ","Relaunch did not restore saved driver");
        drive(game,root/"loaded-career-race");
        const auto encoded=std::vector<std::uint8_t>(game.memory().begin()+0x7a01a,game.memory().begin()+0x7a11a);
        const auto flag=game.memory()[0x1ca34];slots(game,true);tap(game,VK_ESCAPE);finishReturn(game);
        require(std::equal(encoded.begin(),encoded.end(),game.memory().begin()+0x7a01a)&&game.memory()[0x1ca34]==flag,"Cancelled save changed retained Replay career");
        choose(game,3);confirm(game);choose(game,2);confirm(game);finishReturn(game);++replays;
        require(player(game)=="Craig       "&&game.memory()[0x1ca35]==0,"Replay did not restore original retained career");
    }
    auto original=startNativeGame(argv[1]);initializeNativeMenuSession(*original.memory);
    for(unsigned context:{0u,11u}){
        auto source=std::make_unique<NativeMemory>(*original.memory);
        std::fill_n(source->begin()+0x1ed5b,12,' ');const std::string name=context?"Super Driver":"Saved Driver";
        std::copy(name.begin(),name.end(),source->begin()+0x1ed5b);
        (*source)[0x1c9ce]=1;(*source)[0x1c9cf]=7;(*source)[0x1c9d0]=std::uint8_t(context);
        std::swap((*source)[0x1c9c2+4],(*source)[0x1c9c2+11]);
        seed(*source,file,28+context/11,context?"SUPER":"SAVED",NativeSaveKind::Career);
        NativeGameSession game(argv[1],root/("rich-recordings-"+std::to_string(context)),NativeGameStart::OriginalOpening,file);opening(game,"Other");
        load(game,28+context/11);
        require(player(game)==name&&game.memory()[0x1c9ce]==1&&game.memory()[0x1c9cf]==7&&game.memory()[0x1c9d0]==context,"Career restore lost division, damage, context or name");
        require(game.memory()[0x1c9c2+4]==11&&game.memory()[0x1c9c2+11]==4,"Career restore lost driver order");
        drive(game,root/(context?"loaded-super-race":"loaded-rich-race"));
    }
    {
        auto source=std::make_unique<NativeMemory>(*original.memory);
        for(unsigned table=0;table<2;++table)for(unsigned n=0;n<16;++n){
            const auto a=0x7a61a+table*256+n*16;std::fill_n(source->begin()+a,12,' ');
            const std::string name="Record";std::copy(name.begin(),name.end(),source->begin()+a);
            (*source)[a+12]=1;(*source)[a+13]=0x20;(*source)[a+14]=std::uint8_t(((n/10)<<4)|(n%10));(*source)[a+15]=0;
        }
        seed(*source,file,1,"HALL",NativeSaveKind::Records);
        NativeGameSession game(argv[1],root/"hall-recordings",NativeGameStart::OriginalOpening,file);opening(game,"Other");load(game,1);
        for(unsigned a=0x7a61a;a<0x7a81a;++a)if((a-0x7a61a)%16!=15&&(*source)[a]!=game.memory()[a])
            throw std::runtime_error("Hall record mismatch: address="+std::to_string(a)+" expected="+std::to_string((*source)[a])+" actual="+std::to_string(game.memory()[a]));
        const std::vector<std::uint8_t> keptHall(game.memory().begin()+0x7a61a,game.memory().begin()+0x7a81a);
        require(player(game)=="Other       ","Hall record import replaced current driver");
        choose(game,0);confirm(game);settle(game);image(game,root/"loaded-hall.ppm");fire(game);save(game,2,"HALL TWO");
        choose(game,3);confirm(game);choose(game,2);key(game,VK_F1,true);confirm(game);settle(game);
        require(game.openingActive()&&game.openingPage()==NativeOpeningPage::Mode,"F1 Replay did not restart original chooser");++replays;
        require(std::equal(keptHall.begin(),keptHall.end(),game.memory().begin()+0x7a61a),"F1 Replay erased Hall records");
    }
    {
        auto source=std::make_unique<NativeMemory>(*original.memory);(*source)[0x5eb76]=2;
        for(unsigned player=0;player<3;++player){
            const auto a=0x1ed5b-player*16;std::fill_n(source->begin()+a,12,' ');
            const std::string name="Player"+std::to_string(player+1);std::copy(name.begin(),name.end(),source->begin()+a);
        }
        seed(*source,file,3,"MP THREE",NativeSaveKind::Multiplayer);
        NativeGameSession game(argv[1],root/"mp-recordings",NativeGameStart::OriginalOpening,file);opening(game,"Other");load(game,3);
        require(game.memory()[0x5eb76]==2,"Multiplayer save lost original local player count");
        for(unsigned i=0;i<128;++i)require(game.memory()[0x1ecea+i]==(*source)[0x1ecea+i],"Multiplayer save lost driver names");
        save(game,4,"MP NEW");
        choose(game,3);confirm(game);choose(game,2);confirm(game);
        require(game.page()==NativeMenuPage::Standings&&!game.transitioning(),"Multiplayer Replay did not finish its original standings transition");
        finishReturn(game);require(game.memory()[0x5eb76]==2,"Multiplayer Replay lost its local player count");++replays;
    }
    {
        NativeGameSession game(argv[1],root/"retry-recordings",NativeGameStart::OriginalOpening,file);opening(game,"Other");
        NativeSaveStore store(file);NativeSaveBlock before{},after{};
        require(!store.readDirectory(before),"Cannot snapshot saved directory");
        slots(game,true);slot(game,5);text(game,"RETRY");
        require(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"Cannot protect save medium");
        tap(game,VK_RETURN);const auto failed=game.savePhase();
        require(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_NORMAL)!=0,"Cannot restore save medium access");
        require(failed==NativeSaveMenuPhase::Retry,"Failed save did not show original retry/abort prompt");
        require(!store.readDirectory(after)&&after==before,"Failed save changed committed directory");
        tap(game,VK_RETURN);require(game.savePhase()==NativeSaveMenuPhase::RetryDelay,"Save retry omitted original delay");
        game.update(nativeMenuDelaySeconds(50)+0.001,{});finishReturn(game);++saved;
        require(!store.readDirectory(after)&&after[5*16]=='R',"Save retry did not commit the selected name");
        slots(game,true);slot(game,6);text(game,"ABORT");
        require(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"Cannot protect save medium for abort");
        tap(game,VK_RETURN);const auto aborted=game.savePhase();
        require(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_NORMAL)!=0,"Cannot restore aborted save medium access");
        require(aborted==NativeSaveMenuPhase::Retry,"Aborted save missed original retry prompt");
        tap(game,VK_ESCAPE);finishReturn(game);
        require(!store.readDirectory(before)&&before==after,"Aborted save changed the directory");
    }
    {
        NativeSaveStore store(file);NativeSaveBlock directory{};require(!store.readDirectory(directory),"Cannot prepare corrupt save test");
        require(!store.writeSlot(0,directory,{}),"Cannot prepare rejected payload");
        NativeGameSession game(argv[1],root/"corrupt-recordings",NativeGameStart::OriginalOpening,file);opening(game,"Other");
        std::array<std::uint8_t,15> career{};std::copy_n(game.memory().begin()+0x1c9c2,career.size(),career.begin());
        slots(game,false);slot(game,0);tap(game,VK_RETURN);
        require(game.savePhase()==NativeSaveMenuPhase::InvalidData,"Damaged save was accepted or lost original error prompt");
        require(player(game)=="Other       "&&std::equal(career.begin(),career.end(),game.memory().begin()+0x1c9c2),"Damaged save changed the live career");fire(game);finishReturn(game);
    }
    std::cout<<"{\"status\":\"passed\",\"native_saves\":"<<saved<<",\"native_loads\":"<<loaded<<",\"loaded_races\":"<<races<<",\"ticks\":"<<ticks
        <<",\"replay_routes\":"<<replays<<",\"fresh_launch_load\":true,\"career_contexts\":2,\"hall_banks\":2,\"multiplayer_players\":3,\"cancel_preserves_replay\":true,\"corrupt_load_preserves_live_state\":true,\"write_retry_and_abort\":true,\"cpu_emulation\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
