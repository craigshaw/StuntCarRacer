#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "session/native_game_session.h"
#include "persistence/player_data_layout.h"
#include "persistence/career_file_io.h"
#include "../support/hall_fixture.h"
#include "../support/enhanced_record_fixture.h"
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace scr::runtime;namespace fs=std::filesystem;
unsigned checks{},setups{},resumes{},reports{},races{},failureCases{},copyCases{},completedChampionships{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
NativeGameInput input(unsigned controls=0,unsigned number=0,bool back=false){NativeGameInput value;value.menu={std::uint8_t(controls),std::uint8_t(number),back};return value;}
void settle(NativeGameSession& game){for(unsigned n=0;game.transitioning()&&n<32;++n)game.update(1,{});check(!game.transitioning(),"Saved-game UI transition did not settle");}
void ready(NativeGameSession& game){settle(game);if(game.page()!=NativeMenuPage::Race){game.update(1,{});game.update(1,{});}else game.update(0,{});}
void press(NativeGameSession& game){game.update(0,{});game.update(.25,{});game.update(0,input(16));settle(game);ready(game);}
void choose(NativeGameSession& game,unsigned row){ready(game);game.update(0,input(0,1u<<row));ready(game);}
void back(NativeGameSession& game){game.update(0,{});game.update(0,input(0,0,true));settle(game);game.update(0,{});}
void key(NativeGameSession& game,unsigned code,bool down){NativeGameInput value;value.rawKeys.push_back({*nativeAmigaRawKey(code,0,false),down});game.update(0,value);}
void text(NativeGameSession& game,const std::string& text){ready(game);for(unsigned char c:text){key(game,c,true);key(game,c,false);}}
std::unique_ptr<NativeGameSession> open(const fs::path& pack,const fs::path& root,NativeMenuNavigation navigation){
    const auto layout=playerDataLayout(root);auto game=std::make_unique<NativeGameSession>(pack,fs::path{},NativeGameStart::OriginalOpening,root/L"saves/legacy.scr-save");game->enableCareers(root,layout.originalHall,layout.enhancedHall,{CareerStorageLayout::Folders,{},{}});game->enableMultiplayerGames();game->setNavigation(navigation);ready(*game);return game;
}
void selectMode(NativeGameSession& game){choose(game,1);press(game);check(game.multiplayerSelection(),"Multiplayer mode did not open saved-game selection");}
void image(NativeGameSession&,const fs::path&);
void create(NativeGameSession& game,unsigned count,unsigned division,unsigned league,bool confirmRoster=true,bool existing=false,const fs::path& snapshots={}){
    selectMode(game);check(game.menuState().count==(existing?2u:0u),"Multiplayer selection exposed the wrong saved-game actions");if(existing)choose(game,1);press(game);check(game.usingTextInput()&&!game.enteringName(),"New game did not request its independent title");
    if(!snapshots.empty())image(game,snapshots/L"title.ppm");
    text(game,"FRIDAY");press(game);check(game.menuState().count==3,"New game omitted configuration");
    if(!snapshots.empty())image(game,snapshots/L"configuration.ppm");
    for(unsigned n=0;n<division;++n){choose(game,0);press(game);}if(league){choose(game,1);press(game);}choose(game,2);press(game);
    for(unsigned n=0;n<count;++n){check(game.enteringName(),"New game omitted roster name");text(game,"DRIVER"+std::to_string(n+1));key(game,0x0d,true);key(game,0x0d,false);ready(game);
        if(n+1<count){choose(game,0);press(game);}else if(count<8){choose(game,1);press(game);}}
    check(game.openingPage()==NativeOpeningPage::LocalStandings&&(existing||!game.activeMultiplayer()),"Draft became active before roster confirmation");if(!snapshots.empty())image(game,snapshots/L"roster.ppm");if(!confirmRoster)return;press(game);
    check(game.multiplayerHome()&&game.activeMultiplayer()&&!game.hasSuspendedChampionship(),"Confirmed roster did not durably create a game home");
    check(game.activeMultiplayer()->players==count&&game.memory()[0x1c9ce]==division&&game.memory()[0x1c9d0]==league,"Saved-game setup lost unrestricted division/league/roster");++setups;
}
void continueLast(NativeGameSession& game){selectMode(game);check(game.menuState().count==2,"Saved game did not expose Continue/New/Browse");press(game);check(game.multiplayerHome(),"Continue bypassed the saved-game home");}
MultiplayerBytes fileBytes(const fs::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
std::map<fs::path,MultiplayerBytes> files(const fs::path& root){std::map<fs::path,MultiplayerBytes> result;if(fs::exists(root))for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.is_regular_file())result[entry.path().lexically_relative(root)]=fileBytes(entry.path());return result;}
void image(NativeGameSession& game,const fs::path& path){fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file<<"P6\n320 200\n255\n";for(auto colour:game.render())for(unsigned shift:{16u,8u,0u})file.put(char(colour>>shift));check(bool(file),"Cannot capture saved-game menu");}
std::unique_ptr<NativeMemory> baseline(const fs::path& pack){auto data=startNativeGame(pack);initializeNativeOpeningSession(*data.memory);NativeOpeningFlow opening(*data.memory);return std::move(data.memory);}
std::vector<SavedGameEntry> entries(const fs::path& pack,const fs::path& root){const auto base=baseline(pack);NativeCareerStore actor(root);NativeMultiplayerStore games(actor,*base);return games.list();}
std::vector<std::uint8_t> progression(const NativeMemory& m){std::vector<std::uint8_t> value(m.begin()+0x1c9c2,m.begin()+0x1ca42);value.insert(value.end(),m.begin()+0x625a2,m.begin()+0x625a7);value.insert(value.end(),m.begin()+0x636b8,m.begin()+0x636bc);return value;}
void work(NativeGameSession& game){for(unsigned n=0;game.careerCopyPending()&&n<10000;++n){game.update(0,{});Sleep(1);}check(!game.careerCopyPending(),"Saved-game folder worker never completed");game.update(0,{});}
void matrix(const fs::path& pack,const fs::path& root){
    for(auto navigation:{NativeMenuNavigation::Responsive,NativeMenuNavigation::Original})for(unsigned count:{2u,8u})for(unsigned division=0;division<4;++division)for(unsigned league:{0u,11u}){
        const auto folder=root/(std::to_string(unsigned(navigation))+"-"+std::to_string(count)+"-"+std::to_string(division)+"-"+std::to_string(league));auto game=open(pack,folder,navigation);create(*game,count,division,league,true,false,count==2&&division==0&&!league?folder:fs::path{});
        const auto id=game->activeMultiplayer()->id;choose(*game,3);check(game->menuState().selected<3&&!game->savePhase(),"Retired manual-save route was still selectable");choose(*game,0);press(*game);check(game->page()==NativeMenuPage::Matchup,"Start Championship did not prepare a matchup");
        const auto memory=std::make_unique<NativeMemory>(game->memory());back(*game);check(game->multiplayerHome()&&game->hasSuspendedChampionship()&&game->memory()==*memory,"Back advanced the prepared competition");
        const auto first=game->render();check(game->render()==first&&game->memory()==*memory,"Saved-game home rendering changed native memory");check(game->requestClose(),"Matrix game did not drain before close");game.reset();
        game=open(pack,folder,navigation);continueLast(*game);check(game->activeMultiplayer()->id==id&&game->memory()==*memory&&game->hasSuspendedChampionship(),"Reopen changed configuration, native bytes or RNG");
        game->update(0,input(16));for(unsigned n=0;n<4;++n)game->update(.25,input(16));check(game->page()==NativeMenuPage::Matchup,"Held Resume acknowledged the next matchup");game->update(0,{});++resumes;
        check(game->requestClose(),"Resumed matrix game did not close");
    }
}
void liveFlow(const fs::path& pack,const fs::path& root,NativeMenuNavigation navigation){
    fs::create_directories(root/L"saves");{std::ofstream file(root/L"saves/legacy.scr-save",std::ios::binary);file<<"legacy is not a native game";}const auto legacy=fileBytes(root/L"saves/legacy.scr-save");
    auto game=open(pack,root,navigation);create(*game,2,2,11);const auto id=game->activeMultiplayer()->id;image(*game,root/L"home.ppm");press(*game);check(game->page()==NativeMenuPage::Matchup,"Live game did not begin");
    press(*game);check(game->page()==NativeMenuPage::Preview,"Live game did not enter overview");game->update(0,input(4));game->update(0,{});image(*game,root/L"preview.ppm");press(*game);check(game->page()==NativeMenuPage::Race&&game->race().liveTicks()==0,"Overview did not hand off to tick zero");
    NativeGameInput controls;controls.driveControls=5;for(unsigned n=0;n<8;++n)game->update(.12,controls);check(game->requestClose(),"Interrupted race did not close safely");
    const_cast<RaceScene&>(game->race()).quitLive();auto expected=std::make_unique<NativeMemory>();game->race().copyLiveExit(*expected);game.reset();
    game=open(pack,root,navigation);continueLast(*game);press(*game);check(game->page()==NativeMenuPage::Matchup&&game->memory()[0x5eb79]==11,"Interrupted race resumed mid-race or advanced human");press(*game);press(*game);for(unsigned n=0;n<8;++n)game->update(.12,controls);
    const_cast<RaceScene&>(game->race()).quitLive();auto actual=std::make_unique<NativeMemory>();game->race().copyLiveExit(*actual);check(*actual==*expected,"Restarted prepared fixture diverged in simulation/RNG");
    NativeGameInput leave;leave.leaveRace=true;game->update(0,leave);settle(*game);ready(*game);check(game->page()==NativeMenuPage::Results,"Live departure omitted committed results");++races;
    for(unsigned action=0;action<8&&game->page()!=NativeMenuPage::Matchup;++action){
        const auto page=game->page();const auto memory=std::make_unique<NativeMemory>(game->memory());back(*game);check(game->multiplayerHome()&&game->memory()==*memory,"Back acknowledged a multiplayer report");
        check(game->requestClose(),"Report close did not drain");game.reset();game=open(pack,root,navigation);continueLast(*game);check(game->memory()==*memory&&game->activeMultiplayer()->id==id,"Saved report lost scores, damage or RNG");press(*game);check(game->page()==page,"Resume skipped the pending report");++reports;press(*game);
    }
    check(game->page()==NativeMenuPage::Matchup&&game->memory()[0x5eb79]==10,"Completed turn was applied twice or current human was lost");
    const auto before=std::make_unique<NativeMemory>(game->memory());back(*game);choose(*game,1);press(*game);check(!game->hasSuspendedSeason(),"MP Practice borrowed a career season");
    for(unsigned n=0;n<8&&game->page()!=NativeMenuPage::Race;++n)press(*game);check(game->page()==NativeMenuPage::Race,"MP Practice did not start");for(unsigned n=0;n<4;++n)game->update(.12,{});game->update(0,leave);settle(*game);ready(*game);
    for(unsigned n=0;n<4&&!game->multiplayerHome();++n)back(*game);check(game->multiplayerHome()&&game->hasSuspendedChampionship()&&game->memory()==*before,"Practice advanced/replaced a suspended championship");
    choose(*game,2);press(*game);check(game->multiplayerSelection(),"Home did not return to saved-game selection");choose(*game,2);press(*game);image(*game,root/L"browse.ppm");press(*game);choose(*game,1);press(*game);image(*game,root/L"delete-cancel.ppm");press(*game);back(*game);back(*game);
    check(game->requestClose()&&fileBytes(root/L"saves/legacy.scr-save")==legacy,"Native journey touched the legacy virtual disk");
}
void failures(const fs::path& pack,const fs::path& root,NativeMenuNavigation navigation){
    {const auto folder=root/L"first-save";auto game=open(pack,folder,navigation);create(*game,2,3,11,false);fs::create_directories(folder);{std::ofstream block(folder/L"multiplayer");block<<"blocked category";}
        press(*game);const auto state=progression(game->memory());check(!game->activeMultiplayer()&&game->saveStatus()=="NOT SAVED"&&game->menuState().count==1,"Failed draft entered racing or hid first-save failure");image(*game,folder/L"create-error.ppm");
        check(!game->requestClose()&&game->closePending(),"First-save failure silently closed");choose(*game,2);press(*game);check(!game->closePending()&&progression(game->memory())==state,"Close cancellation discarded setup state/RNG");
        fs::remove(folder/L"multiplayer");press(*game);check(game->multiplayerHome()&&game->activeMultiplayer()->players==2&&game->memory()[0x1c9ce]==3&&game->memory()[0x1c9d0]==11,"First-save Retry lost draft configuration");check(game->requestClose(),"Retried draft did not close");++failureCases;}
    {const auto folder=root/L"cancel-draft";auto game=open(pack,folder,navigation);create(*game,2,1,0,false);fs::create_directories(folder);{std::ofstream block(folder/L"multiplayer");block<<"keep me";}press(*game);choose(*game,1);press(*game);work(*game);
        check(game->multiplayerSelection()&&!game->activeMultiplayer()&&fileBytes(folder/L"multiplayer")==MultiplayerBytes({'k','e','e','p',' ','m','e'}),"Cancelling a blocked first save failed or removed unrelated data");check(game->requestClose(),"Cancelled draft left a failing retry job");++failureCases;}
    {const auto folder=root/L"active-save";auto game=open(pack,folder,navigation);create(*game,2,0,0);press(*game);check(game->flushRecords(),"Cannot drain failure fixture");const auto saved=entries(pack,folder).front();const auto head=saved.folder/L"game.scr-multiplayer";const auto lastGood=fileBytes(head);fs::create_directory(head.wstring()+L".tmp");
        press(*game);for(unsigned n=0;game->saveStatus()!="NOT SAVED"&&n<2000;++n){game->update(0,{});Sleep(1);}game->update(0,{});check(game->saveStatus()=="NOT SAVED"&&fileBytes(head)==lastGood,"Failed checkpoint damaged the last good generation");image(*game,folder/L"save-error.ppm");
        choose(*game,1);press(*game);check(game->page()==NativeMenuPage::Preview,"Continue without saving lost the current RAM overview");const auto state=progression(game->memory());back(*game);press(*game);check(game->page()==NativeMenuPage::Matchup&&progression(game->memory())==state,"Unsaved Resume lost retained RAM progress/RNG");
        check(!game->requestClose()&&game->closePending(),"Failed active save silently closed");image(*game,folder/L"departure.ppm");choose(*game,2);press(*game);check(!game->closePending()&&progression(game->memory())==state,"Departure Cancel changed RAM state");
        fs::remove(head.wstring()+L".tmp");check(game->requestClose(),"Close Retry did not persist the retained RAM candidate");game.reset();game=open(pack,folder,navigation);continueLast(*game);check(progression(game->memory())==state,"Recovered storage wrote stale state");check(game->requestClose(),"Recovered failure fixture did not close");++failureCases;}
    {const auto folder=root/L"discard";auto game=open(pack,folder,navigation);create(*game,2,0,0);press(*game);check(game->flushRecords(),"Cannot drain discard fixture");const auto head=entries(pack,folder).front().folder/L"game.scr-multiplayer";const auto good=fileBytes(head);const auto saved=decodeNativeMultiplayerSave(*baseline(pack),good);const auto state=progression(*decodeNativeMultiplayerCheckpoint(*baseline(pack),saved.checkpoint).memory);fs::create_directory(head.wstring()+L".tmp");press(*game);
        for(unsigned n=0;game->saveStatus()!="NOT SAVED"&&n<2000;++n){game->update(0,{});Sleep(1);}game->update(0,{});choose(*game,1);press(*game);back(*game);choose(*game,2);press(*game);choose(*game,1);press(*game);
        check(game->multiplayerSelection()&&!game->activeMultiplayer()&&fileBytes(head)==good,"Leave unsaved retained discarded RAM or changed the disk checkpoint");fs::remove(head.wstring()+L".tmp");press(*game);check(game->multiplayerHome()&&progression(game->memory())==state,"Continue after Leave unsaved restored discarded progress");check(game->requestClose(),"Discard fixture did not close");++failureCases;}
}
void browse(NativeGameSession& game,unsigned row){
    if(game.multiplayerHome()){choose(game,2);press(game);}check(game.multiplayerSelection(),"Browse helper requires saved-game selection");choose(game,game.menuState().count);press(game);
    for(unsigned n=0;n<8;++n){game.update(0,input(1));game.update(0,{});}
    for(unsigned n=0;n<row;++n){game.update(0,input(2));game.update(0,{});}
}
void copyAndDelete(const fs::path& pack,const fs::path& root,NativeMenuNavigation navigation,bool keepBoth){
    auto game=open(pack,root,navigation);create(*game,2,1,11);press(*game);const auto id=game->activeMultiplayer()->id;back(*game);check(game->requestClose(),"Copy fixture did not close");game.reset();
    const auto original=entries(pack,root).front();const auto incoming=root/L"multiplayer/incoming";FolderTransactions::copy(original.folder,incoming);
    if(!keepBoth){auto save=decodeNativeMultiplayerSave(*baseline(pack),fileBytes(incoming/L"game.scr-multiplayer"));auto c=decodeNativeMultiplayerCheckpoint(*baseline(pack),save.checkpoint);(*c.memory)[0x1ca36+11]=3;save.info.lastPlayed=0;save.checkpoint=encodeNativeMultiplayerCheckpoint(*baseline(pack),c);std::string error;check(career_files::atomicWrite(incoming/L"game.scr-multiplayer",encodeNativeMultiplayerSave(*baseline(pack),save),error),"Cannot seed divergent incoming copy");}
    const auto before=files(root/L"multiplayer");game=open(pack,root,navigation);selectMode(*game);check(game->menuState().count==1,"Conflicted last-used game exposed Continue");browse(*game,0);press(*game);check(game->menuState().selected==2,"Copy conflict did not default to Cancel");image(*game,root/L"conflict-cancel.ppm");press(*game);check(before==files(root/L"multiplayer"),"Cancel changed a copied game");
    game->update(0,input(2));game->update(0,{});press(*game);choose(*game,keepBoth?1:0);image(*game,root/L"conflict-choice.ppm");press(*game);work(*game);image(*game,root/L"copy-result.ppm");press(*game);press(*game);image(*game,root/L"copy-loaded.ppm");check(game->multiplayerHome(),"Resolved copy did not load");
    check(keepBoth?game->activeMultiplayer()->id!=id:game->activeMultiplayer()->id==id,"Copy action assigned the wrong identity");if(!keepBoth)check(game->memory()[0x1ca36+11]==3&&fs::exists(incoming/L"recovery/replaced"),"Replace chose by date or lost previous contents");
    const auto removed=game->activeMultiplayer()->id;check(game->flushRecords(),"Resolved game did not drain");const auto list=entries(pack,root);unsigned row=0;for(unsigned n=0;n<list.size();++n)if(list[n].id==removed)row=n;
    browse(*game,row);press(*game);choose(*game,1);press(*game);choose(*game,1);press(*game);const bool closed=game->requestClose();if(!closed)check(game->closePending(),"Close did not wait for deletion");work(*game);
    image(*game,root/L"delete-result.ppm");check(game->closeReady()&&!game->activeMultiplayer(),"Deleting the active game did not complete pending close");for(const auto& item:entries(pack,root))check(item.id!=removed,"Deleted game resurrected from backups");++copyCases;
}
void reopenReport(std::unique_ptr<NativeGameSession>& game,const fs::path& pack,const fs::path& root,NativeMenuNavigation navigation){
    const auto page=game->page();const auto state=progression(game->memory());back(*game);check(game->requestClose(),"Boundary did not drain");game.reset();game=open(pack,root,navigation);continueLast(*game);check(progression(game->memory())==state,"Report reopen changed accumulated progress/RNG");press(*game);check(game->page()==page,"Resume acknowledged a pending boundary");++reports;
}
void independentGames(const fs::path& pack,const fs::path& root,NativeMenuNavigation navigation){
    auto game=open(pack,root,navigation);create(*game,2,3,11);const auto first=game->activeMultiplayer()->id;press(*game);const auto state=progression(game->memory());back(*game);back(*game);back(*game);
    choose(*game,0);press(*game);press(*game);text(*game,"CAREER");press(*game);press(*game);check(game->activeCareer()&&!game->activeMultiplayer(),"Creating a career retained multiplayer ownership");choose(*game,2);press(*game);back(*game);back(*game);check(game->careerHome(),"Mixed career did not return home");check(game->requestClose(),"Mixed career did not close");game.reset();
    SavedGameEntry career;{NativeCareerStore actor(root,{}, {CareerStorageLayout::Folders,{},{}});career=actor.list().front();}const auto kept=root/L"retained-career";FolderTransactions::copy(career.folder,kept);
    game=open(pack,root,navigation);press(*game);choose(*game,2);press(*game);press(*game);choose(*game,1);press(*game);choose(*game,1);press(*game);work(*game);check(!game->activeCareer(),"Deleting a career retained active ownership");check(game->requestClose(),"Mixed deletion did not close");game.reset();FolderTransactions::copy(kept,career.folder);const auto careers=files(root/L"careers");
    game=open(pack,root,navigation);create(*game,8,1,0,true,true);const auto second=game->activeMultiplayer()->id;press(*game);back(*game);check(second!=first&&game->requestClose(),"Second named game reused the first identity");game.reset();
    const auto list=entries(pack,root);unsigned row=0;for(unsigned n=0;n<list.size();++n)if(list[n].id==first)row=n;
    game=open(pack,root,navigation);selectMode(*game);browse(*game,row);press(*game);press(*game);check(game->multiplayerHome()&&game->activeMultiplayer()->id==first&&progression(game->memory())==state,"Switching games or deleting/restoring a career changed multiplayer configuration/progress");press(*game);check(game->page()==NativeMenuPage::Matchup&&game->memory()[0x1c9ce]==3&&game->memory()[0x1c9d0]==11,"Other saved-game configuration leaked on Resume");check(game->requestClose()&&files(root/L"careers")==careers,"Multiplayer switching rewrote a restored career");
}
void startupRecovery(const fs::path& pack,const fs::path& root){
    const auto id=NativeCareerStore::newId();{NativeCareerStore actor(root);actor.setLastUsed(id);check(actor.flush(),"Cannot seed legacy last-used selection");actor.setLastUsed(id);check(actor.flush(),"Cannot seed last-used recovery");}
    {std::ofstream damaged(root/L"last.scr-index",std::ios::binary|std::ios::trunc);damaged<<"damaged";}
    NativeGameSession game(pack,{});game.enableCareers(root,root/L"hall");game.enableMultiplayerGames(root/L"settings/display.ini");check(!game.openingActive(),"Last-used recovery notice was omitted");press(game);check(game.openingActive(),"Recovery notice did not return to mode selection");selectMode(game);check(game.multiplayerSelection()&&game.requestClose(),"Recovered default-root startup failed native multiplayer initialization");
}
void finishAndChampionships(const fs::path& pack,const fs::path& root,NativeMenuNavigation navigation){
    const auto data=startNativeGame(pack);const auto base=baseline(pack);const auto layout=playerDataLayout(root);
    SharedHallArchive hall(layout.originalHall,{ghostTrackContentIdentity(data.payload),1});EnhancedHallArchive enhanced(layout.enhancedHall,{ghostTrackContentIdentity(data.payload),1,enhancedTimingMethodVersion});
    hall.commit({std::make_shared<const HallSubmission>(makeHallSubmission(scr::test::hall::attempt(1)))});enhanced.commit({std::make_shared<const EnhancedSubmission>(makeEnhancedSubmission(scr::test::enhanced::attempt(2)))});const auto records=files(root/L"hall-of-fame");
    auto game=open(pack,root,navigation);create(*game,2,0,0);press(*game);press(*game);press(*game);for(unsigned n=0;!game->raceEnded()&&n<4000;++n)game->update(.12,{});check(game->raceEnded(),"Pace car did not reach a natural named-game finish");image(*game,root/L"finish.ppm");back(*game);check(game->requestClose(),"Pending finish did not drain");game.reset();
    const auto entry=entries(pack,root).front();const auto head=entry.folder/L"game.scr-multiplayer";auto saved=decodeNativeMultiplayerSave(*base,fileBytes(head));auto finish=decodeNativeMultiplayerCheckpoint(*base,saved.checkpoint);check(finish.finishPending,"Autosave omitted pending finish");
    // A controlled display fixture adds notices without inventing live Hall attempts.
    finish.enhancedRecordNotice=EnhancedRecordNotice{{EnhancedRecordLine{"DRIVER1",123457}},{EnhancedRecordLine{"DRIVER2",234567}}};saved.checkpoint=encodeNativeMultiplayerCheckpoint(*base,finish);std::string error;check(career_files::atomicWrite(head,encodeNativeMultiplayerSave(*base,saved),error),"Cannot seed record notice");
    const auto committed=progression(*finish.memory);game=open(pack,root,navigation);continueLast(*game);check(progression(game->memory())==committed,"Continue rescored the stored finish");back(*game);back(*game);check(game->openingActive()&&game->openingPage()==NativeOpeningPage::Mode,"Pending finish leaked through the mode chooser");continueLast(*game);press(*game);check(game->raceEnded(),"Resume lost the pending finish");
    game->setTimingCategory(TimingCategory::Enhanced);image(*game,root/L"finish-restored.ppm");press(*game);check(game->page()==NativeMenuPage::Results&&progression(game->memory())==committed,"Finish acknowledgement repeated scoring");press(*game);check(game->page()==NativeMenuPage::Records,"Saved Enhanced notice was lost");image(*game,root/L"records.ppm");reopenReport(game,pack,root,navigation);game->setTimingCategory(TimingCategory::Enhanced);
    for(unsigned n=0;n<8&&game->page()!=NativeMenuPage::Matchup;++n)press(*game);check(game->page()==NativeMenuPage::Matchup&&game->memory()[0x5eb79]==10,"Saved terminal/report journey repeated a turn");
    for(unsigned championship=0;championship<4;++championship){
        if(championship){check(game->multiplayerHome()&&!game->hasSuspendedChampionship(),"Completed championship did not return to home");press(*game);}
        for(unsigned track=0;track<2;++track){key(*game,0x70,true);press(*game);key(*game,0x70,false);check(game->page()==NativeMenuPage::TrackResults,"F1 did not retain a track summary");reopenReport(game,pack,root,navigation);press(*game);check(game->page()==NativeMenuPage::Standings,"Track summary omitted standings");reopenReport(game,pack,root,navigation);press(*game);}
        check(game->multiplayerHome()&&!game->hasSuspendedChampionship()&&game->memory()[0x57c67]==(championship==3?0:championship+1),"Named-game championship counter advanced/reset twice");++completedChampionships;
    }
    for(unsigned n=0;n<48;++n)check(game->memory()[0x1c9de+n]==0,"Fourth named championship did not reset scores");check(game->flushRecords()&&files(root/L"hall-of-fame")==records,"Reopening game results changed or minted Hall attempts");check(game->requestClose(),"Completed championships did not close");
}
}
int main(int argc,char** argv)try{
    check(argc==3,"Usage: native_multiplayer_saved_session_tests pack isolated-output");const fs::path pack=argv[1],root=argv[2];
    startupRecovery(pack,root/L"startup-recovery");matrix(pack,root/L"matrix");liveFlow(pack,root/L"responsive",NativeMenuNavigation::Responsive);liveFlow(pack,root/L"original",NativeMenuNavigation::Original);
    for(auto navigation:{NativeMenuNavigation::Responsive,NativeMenuNavigation::Original}){const auto folder=root/(navigation==NativeMenuNavigation::Responsive?L"responsive-extra":L"original-extra");failures(pack,folder/L"failures",navigation);copyAndDelete(pack,folder/L"copies",navigation,navigation==NativeMenuNavigation::Responsive);independentGames(pack,folder/L"independent",navigation);finishAndChampionships(pack,folder/L"championships",navigation);}
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"setups\":"<<setups<<",\"resumes\":"<<resumes<<",\"reopened_reports\":"<<reports<<",\"interrupted_races\":"<<races<<",\"failure_lifecycles\":"<<failureCases<<",\"copy_delete_lifecycles\":"<<copyCases<<",\"championships\":"<<completedChampionships<<",\"both_navigation_modes\":true,\"unrestricted_division_league\":true,\"legacy_routes_excluded\":true,\"hall_isolation\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
