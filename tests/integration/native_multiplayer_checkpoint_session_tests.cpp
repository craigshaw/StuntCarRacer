#include "session/native_game_session.h"
#include "persistence/player_data_layout.h"
#include "../support/hall_fixture.h"
#include "../support/enhanced_record_fixture.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <map>

namespace {
using namespace scr::runtime;namespace fs=std::filesystem;
unsigned checks{},boundaries{},races{},championships{},forfeitedTracks{},interrupted{},prepared{},naturalFinishes{},ticks{},bonusAwards{};
MultiplayerBytes naturalFinishCheckpoint;
std::set<unsigned> pages,humans,tracks;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
NativeGameInput input(unsigned controls=0,unsigned number=0){NativeGameInput value;value.menu={std::uint8_t(controls),std::uint8_t(number),false};return value;}
void settle(NativeGameSession& game){for(unsigned n=0;game.transitioning()&&n<32;++n)game.update(1,{});check(!game.transitioning(),"Checkpoint test did not settle a transition");}
void ready(NativeGameSession& game){settle(game);game.update(1,{});game.update(1,{});}
void press(NativeGameSession& game){settle(game);game.update(0,{});game.update(.25,{});game.update(0,input(16));settle(game);game.update(0,{});if(game.page()!=NativeMenuPage::Race)ready(game);}
void choose(NativeGameSession& game,unsigned row){ready(game);game.update(0,input(0,1u<<row));ready(game);}
void key(NativeGameSession& game,unsigned vk,bool down){NativeGameInput value;value.rawKeys.push_back({*nativeAmigaRawKey(vk,0,false),down});game.update(0,value);}
void opening(NativeGameSession& game,unsigned count){
    game.setNavigation(NativeMenuNavigation::Responsive);choose(game,1);press(game);
    for(unsigned n=0;n<count;++n){check(game.enteringName(),"Multiplayer checkpoint omitted setup driver");
        for(unsigned char c:std::string("DRIVER")+std::to_string(n+1)){key(game,c,true);key(game,c,false);}key(game,0x0d,true);key(game,0x0d,false);
        if(n+1<count){choose(game,0);press(game);}else if(count<8){choose(game,1);press(game);}
    }
    press(game);check(game.page()==NativeMenuPage::Main&&game.multiplayer(),"Multiplayer setup did not complete");ready(game);game.releaseInput();
}
std::unique_ptr<NativeMemory> baseline(const fs::path& pack){auto data=startNativeGame(pack);initializeNativeOpeningSession(*data.memory);NativeOpeningFlow opening(*data.memory);return std::move(data.memory);}
std::unique_ptr<NativeGameSession> restored(const fs::path& pack,const MultiplayerBytes& checkpoint,NativeMenuNavigation navigation){
    auto game=std::make_unique<NativeGameSession>(pack,fs::path{});game->setNavigation(navigation);game->restoreMultiplayerCheckpoint(checkpoint);return game;
}
void equal(NativeGameSession& source,NativeGameSession& resumed){
    source.update(0,{});resumed.update(0,{});
    source.releaseInput();resumed.releaseInput();
    if(source.memory()!=resumed.memory()){
        const auto at=std::mismatch(source.memory().begin(),source.memory().end(),resumed.memory().begin()).first-source.memory().begin();
        throw std::runtime_error("Resumed multiplayer native memory diverged at "+std::to_string(at)+" page "+std::to_string(unsigned(source.page()))+" values "+std::to_string(source.memory()[at])+"/"+std::to_string(resumed.memory()[at]));
    }
    check(source.page()==resumed.page(),"Resumed multiplayer changed the pending page");
}
void snapshot(NativeGameSession& source,std::unique_ptr<NativeGameSession>& resumed,const fs::path& pack,const NativeMemory& base,const fs::path& file,NativeMenuNavigation navigation){
    source.releaseInput();resumed->releaseInput();const auto before=std::make_unique<NativeMemory>(source.memory());
    const auto bytes=source.captureMultiplayerCheckpoint(),own=resumed->captureMultiplayerCheckpoint();check(bytes==own,"Resumed checkpoint differs from uninterrupted checkpoint");
    check(source.memory()==*before,"Capturing multiplayer consumed RNG or changed native memory");
    NativeMultiplayerSave save{{std::string(32,'a'),"Checkpoint test",1000+boundaries},boundaries+1,own};
    const auto encoded=encodeNativeMultiplayerSave(base,save);fs::create_directories(file.parent_path());
    {std::ofstream output(file,std::ios::binary);output.write(reinterpret_cast<const char*>(encoded.data()),std::streamsize(encoded.size()));check(bool(output),"Cannot write isolated multiplayer checkpoint");}
    std::ifstream stream(file,std::ios::binary);const MultiplayerBytes disk{std::istreambuf_iterator<char>(stream),{}};
    const auto saved=decodeNativeMultiplayerSave(base,disk);check(saved.checkpoint==own,"On-disk multiplayer checkpoint changed bytes");
    resumed=restored(pack,saved.checkpoint,navigation);++boundaries;
    if(source.page()!=NativeMenuPage::Preview&&source.page()!=NativeMenuPage::Race)equal(source,*resumed);
}
void fullExit(NativeGameSession& source,NativeGameSession& resumed){
    auto a=std::make_unique<NativeMemory>(),b=std::make_unique<NativeMemory>();source.race().copyLiveExit(*a);resumed.race().copyLiveExit(*b);
    check(*a==*b,"Resumed simulation/RNG differed at terminal race exit");
}
void drive(NativeGameSession& source,std::unique_ptr<NativeGameSession>& resumed,const fs::path& pack,const NativeMemory& base,const fs::path& file,NativeMenuNavigation navigation,bool natural,bool interrupt){
    const unsigned human=source.memory()[0x5eb79];humans.insert(human);tracks.insert(source.memory()[0x1ca33]);
    press(source);press(*resumed);check(source.page()==NativeMenuPage::Preview,"Saved matchup did not prepare preview");equal(source,*resumed);
    source.update(0,input(4));resumed->update(0,input(4));source.update(0,{});resumed->update(0,{});equal(source,*resumed);
    snapshot(source,resumed,pack,base,file,navigation);check(resumed->page()==NativeMenuPage::Matchup,"Saved preview did not resume at a matchup");
    press(*resumed);equal(source,*resumed);check(source.render()==resumed->render(),"Resumed prepared preview changed its track geometry or rotation");++prepared;
    press(source);press(*resumed);check(source.page()==NativeMenuPage::Race&&source.race().liveTicks()==0,"Saved preview did not start a fresh race");equal(source,*resumed);
    NativeGameInput controls;controls.driveControls=5;
    for(unsigned n=0;n<4;++n){source.update(.12,controls);resumed->update(.12,controls);++ticks;}
    if(interrupt){
        snapshot(source,resumed,pack,base,file,navigation);check(resumed->page()==NativeMenuPage::Matchup,"Interrupted race restored a mid-race car");
        check(resumed->memory()[0x5eb79]==human,"Interrupted race advanced to another human");press(*resumed);press(*resumed);
        for(unsigned n=0;n<4;++n)resumed->update(.12,controls);++interrupted;
    }
    if(natural){
        for(unsigned n=0;n<4000&&!source.raceEnded();++n){source.update(.12,{});resumed->update(.12,{});++ticks;}
        check(source.raceEnded()&&resumed->raceEnded(),"Pace car did not reach a natural finish");fullExit(source,*resumed);
        const auto original=source.render();source.setTimingCategory(TimingCategory::Enhanced);const auto enhanced=source.render();source.setTimingCategory(TimingCategory::Original);
        snapshot(source,resumed,pack,base,file,navigation);check(resumed->raceEnded()&&resumed->render()==original,"Saved finish lost Original presentation");
        naturalFinishCheckpoint=resumed->captureMultiplayerCheckpoint();
        resumed->setTimingCategory(TimingCategory::Enhanced);check(resumed->render()==enhanced,"Saved finish lost Enhanced presentation");resumed->setTimingCategory(TimingCategory::Original);
        press(source);press(*resumed);++naturalFinishes;
    }else{
        // Exercise the private terminal-copy boundary before normal acknowledgement.
        const_cast<RaceScene&>(source.race()).quitLive();const_cast<RaceScene&>(resumed->race()).quitLive();fullExit(source,*resumed);
        const auto sourceState=std::make_unique<NativeMemory>();source.race().copyLiveExit(*sourceState);
        snapshot(source,resumed,pack,base,file,navigation);check(resumed->page()==NativeMenuPage::Results,"Saved quit did not retain committed results");
        const auto after=std::make_unique<NativeMemory>();source.race().copyLiveExit(*after);check(*after==*sourceState,"Terminal checkpoint mutated the live exit");
        press(source);
    }
    equal(source,*resumed);check(source.page()==NativeMenuPage::Results,"Race exit omitted multiplayer results");++races;
}
void season(NativeGameSession& source,std::unique_ptr<NativeGameSession>& resumed,const fs::path& pack,const NativeMemory& base,const fs::path& file,NativeMenuNavigation navigation,bool natural){
    const auto before=source.memory()[0x57c67],division=source.memory()[0x1c9ce],league=source.memory()[0x1c9d0];
    choose(source,2);choose(*resumed,2);press(source);press(*resumed);equal(source,*resumed);unsigned played=0;
    for(unsigned action=0;source.page()!=NativeMenuPage::Main&&action<160;++action){
        pages.insert(unsigned(source.page()));
        const bool award=source.page()==NativeMenuPage::RaceResults&&source.memory()[0x1ca35]==source.memory()[0x5eb76];
        const unsigned lapWinner=11-source.memory()[0x5eb76];
        if(award){
            // Controlled winning-holder fixture, independent of driven Hall laps.
            for(auto* game:{&source,resumed.get()}){auto& m=const_cast<NativeMemory&>(game->memory());m[0x1ca23]=std::uint8_t(lapWinner);m[0x1ca24]=11;}
        }
        const auto wins=source.memory()[0x1c9de+11],laps=source.memory()[0x1c9ea+lapWinner];
        snapshot(source,resumed,pack,base,file,navigation);
        if(source.page()==NativeMenuPage::Matchup){drive(source,resumed,pack,base,file,navigation,natural&&played==0,played==1);++played;}
        else{press(source);press(*resumed);equal(source,*resumed);}
        if(award){check(source.page()==NativeMenuPage::TrackResults&&source.memory()[0x1c9de+11]==std::uint8_t(wins+1)&&source.memory()[0x1c9ea+lapWinner]==std::uint8_t(laps+1),"Resumed track bonus was lost or applied twice");++bonusAwards;}
    }
    check(source.page()==NativeMenuPage::Main&&played==2*(source.memory()[0x5eb76]+1),"Resumed championship missed a human or track");
    check(source.memory()[0x1c9ce]==division&&source.memory()[0x1c9d0]==league,"Resumed championship changed configuration");
    check(source.memory()[0x57c67]==(before==3?0:before+1),"Resumed championship counter advanced/reset twice");
    if(before==3)for(unsigned n=0;n<48;++n)check(source.memory()[0x1c9de+n]==0,"Fourth resumed championship did not reset scores once");
    ready(source);ready(*resumed);snapshot(source,resumed,pack,base,file,navigation);++championships;
}
void forfeit(NativeGameSession& source,std::unique_ptr<NativeGameSession>& resumed,const fs::path& pack,const NativeMemory& base,const fs::path& file,NativeMenuNavigation navigation){
    choose(source,2);choose(*resumed,2);press(source);press(*resumed);
    for(unsigned track=0;track<2;++track){
        const auto before=unsigned(source.memory()[0x57c72])*256+source.memory()[0x57c73];
        key(source,0x70,true);key(*resumed,0x70,true);press(source);press(*resumed);key(source,0x70,false);key(*resumed,0x70,false);equal(source,*resumed);
        check(source.page()==NativeMenuPage::TrackResults,"F1 forfeiture lost track summary");
        const auto after=unsigned(source.memory()[0x57c72])*256+source.memory()[0x57c73];check(after==before+source.memory()[0x5eb76]+1,"F1 restored forfeiture scored a human twice");
        snapshot(source,resumed,pack,base,file,navigation);press(source);press(*resumed);equal(source,*resumed);
        snapshot(source,resumed,pack,base,file,navigation);press(source);press(*resumed);equal(source,*resumed);++forfeitedTracks;
    }check(source.page()==NativeMenuPage::Main,"Forfeited saved championship did not return home");
}
std::map<fs::path,MultiplayerBytes> files(const fs::path& root){
    std::map<fs::path,MultiplayerBytes> result;for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.is_regular_file()){
        std::ifstream input(entry.path(),std::ios::binary);result[entry.path().lexically_relative(root)]={std::istreambuf_iterator<char>(input),{}};}return result;
}
void image(NativeGameSession& game,const fs::path& path){
    fs::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);output<<"P6\n320 200\n255\n";
    for(const auto pixel:game.render())for(unsigned shift:{16u,8u,0u})output.put(char(pixel>>shift));check(bool(output),"Cannot retain checkpoint presentation");
}
void presentation(const fs::path& pack,const NativeMemory& base,const fs::path& root){
    check(!naturalFinishCheckpoint.empty(),"Natural finish fixture was not retained");auto c=decodeNativeMultiplayerCheckpoint(base,naturalFinishCheckpoint);
    c.enhancedRecordNotice=EnhancedRecordNotice{{EnhancedRecordLine{"DRIVER1",123457}},{EnhancedRecordLine{"DRIVER2",234567}}};const auto bytes=encodeNativeMultiplayerCheckpoint(base,c);
    auto data=startNativeGame(pack);const auto content=ghostTrackContentIdentity(data.payload);
    for(const auto navigation:{NativeMenuNavigation::Responsive,NativeMenuNavigation::Original}){
        const auto folder=root/(navigation==NativeMenuNavigation::Responsive?L"responsive":L"original");const auto layout=playerDataLayout(folder/L"data");
        SharedHallArchive original(layout.originalHall,{content,1});EnhancedHallArchive enhanced(layout.enhancedHall,{content,1,enhancedTimingMethodVersion});
        original.commit({std::make_shared<const HallSubmission>(makeHallSubmission(scr::test::hall::attempt(1)))});
        enhanced.commit({std::make_shared<const EnhancedSubmission>(makeEnhancedSubmission(scr::test::enhanced::attempt(2)))});
        const auto before=files(layout.root/L"hall-of-fame");
        auto source=std::make_unique<NativeGameSession>(pack,fs::path{});source->enableCareers(layout.root,layout.originalHall,layout.enhancedHall);source->setNavigation(navigation);
        source->restoreMultiplayerCheckpoint(bytes,{16,0,false});const auto memory=std::make_unique<NativeMemory>(source->memory());
        image(*source,folder/L"finish-original.ppm");source->setTimingCategory(TimingCategory::Enhanced);image(*source,folder/L"finish-enhanced.ppm");
        check(source->memory()==*memory,"Restored finish presentation changed native memory");source->update(1,input(16));check(source->raceEnded(),"Held Confirm acknowledged a restored finish");
        const auto captured=source->captureMultiplayerCheckpoint();auto damaged=captured;damaged.back()^=1;bool rejected=false;
        try{source->restoreMultiplayerCheckpoint(damaged);}catch(const std::exception&){rejected=true;}
        check(rejected&&source->memory()==*memory&&source->captureMultiplayerCheckpoint()==captured,"Rejected restore partially replaced the session");
        auto resumed=restored(pack,bytes,navigation);resumed->setTimingCategory(TimingCategory::Enhanced);
        press(*source);press(*resumed);equal(*source,*resumed);check(source->page()==NativeMenuPage::Results,"Restored finish acknowledgement skipped results");
        press(*source);press(*resumed);equal(*source,*resumed);check(source->page()==NativeMenuPage::Records,"Restored Enhanced notice was skipped");pages.insert(unsigned(source->page()));
        image(*source,folder/L"records-enhanced.ppm");source->setTimingCategory(TimingCategory::Original);image(*source,folder/L"records-original.ppm");source->setTimingCategory(TimingCategory::Enhanced);
        snapshot(*source,resumed,pack,base,folder/L"game.scr-multiplayer",navigation);resumed->setTimingCategory(TimingCategory::Enhanced);
        press(*source);press(*resumed);equal(*source,*resumed);check(source->page()==NativeMenuPage::RoundResults,"Restored record notice repeated scoring or skipped a report");
        check(source->flushRecords()&&files(layout.root/L"hall-of-fame")==before,"Restoring finish/record metadata changed or minted Hall attempts");
        check(source->sharedHall().generation==1&&source->enhancedHall().generation==1,"Restore changed a shared Hall generation");
    }
}
}
int main(int argc,char** argv)try{
    check(argc==3,"Usage: native_multiplayer_checkpoint_session_tests pack isolated-output");const fs::path pack=argv[1],root=argv[2];const auto base=baseline(pack);
    for(unsigned count=2;count<=8;++count)for(unsigned division=0;division<4;++division)for(unsigned league:{0u,11u}){
        auto source=std::make_unique<NativeGameSession>(pack,fs::path{});opening(*source,count);
        // Stage 4 supplies the setup UI; exercise each native configuration here.
        auto& memory=const_cast<NativeMemory&>(source->memory());memory[0x1c9ce]=std::uint8_t(division);memory[0x1c9d0]=std::uint8_t(league);
        const auto navigation=(division+count+league)%2||(count==8&&division==3&&league==11)?NativeMenuNavigation::Original:NativeMenuNavigation::Responsive;source->setNavigation(navigation);
        auto resumed=restored(pack,source->captureMultiplayerCheckpoint(),navigation);const auto file=root/(std::to_string(count)+"-"+std::to_string(division)+"-"+std::to_string(league))/L"game.scr-multiplayer";
        season(*source,resumed,pack,*base,file,navigation,count==2&&division==0&&league==0);
        if((count==2||count==8)&&division==3&&league==11)for(unsigned n=1;n<4;++n)season(*source,resumed,pack,*base,file,navigation,false);
        forfeit(*source,resumed,pack,*base,file,navigation);
        if(count==2&&division==0&&league==0)presentation(pack,*base,root/L"presentation");
        std::cout<<"Completed "<<count<<" drivers, division "<<4-division<<", league "<<league<<'\n'<<std::flush;
    }
    check(humans.size()==8&&tracks.size()==8,"Checkpoint matrix missed a human or track");
    for(auto page:{NativeMenuPage::Matchup,NativeMenuPage::Results,NativeMenuPage::RoundResults,NativeMenuPage::Standings,NativeMenuPage::RaceResults,NativeMenuPage::TrackResults})check(pages.count(unsigned(page)),"Checkpoint matrix missed a report stage");
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"fresh_session_boundaries\":"<<boundaries<<",\"configurations\":56,\"human_races\":"<<races<<",\"championships\":"<<championships<<",\"forfeited_tracks\":"<<forfeitedTracks<<",\"bonus_award_fixtures\":"<<bonusAwards<<",\"interrupted_races\":"<<interrupted<<",\"prepared_previews\":"<<prepared<<",\"natural_finishes\":"<<naturalFinishes<<",\"ticks\":"<<ticks<<",\"full_native_memory_rng\":true,\"both_navigation_modes\":true,\"four_championship_reset\":true,\"hall_restore_isolation\":true,\"visual_states\":8}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
