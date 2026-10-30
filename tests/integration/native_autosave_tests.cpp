#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "session/native_game_session.h"
#include "state/race_timing_adapter.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>

namespace {
using namespace scr::runtime;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
NativeGameInput input(unsigned controls=0,bool back=false,unsigned number=0){NativeGameInput i;i.menu={std::uint8_t(controls),std::uint8_t(number),back};return i;}
void tap(NativeGameSession& game,unsigned controls=16,bool back=false,unsigned number=0){game.update(0,{});game.update(0,input(controls,back,number));game.update(0,{});}
void choose(NativeGameSession& game,unsigned row){tap(game,0,false,1u<<row);}
void back(NativeGameSession& game){tap(game,0,true);}
void resume(NativeGameSession& game){
    require(game.page()==NativeMenuPage::Main&&game.hasSuspendedSeason(),"Resume Season was unavailable at the profile menu");choose(game,2);tap(game);
}
void continueLast(NativeGameSession& game,bool season=false){
    tap(game);require(game.careerHome(),"Single Player skipped career selection");tap(game);
    require(game.page()==NativeMenuPage::Main&&game.menuState().count==2,"Continue did not open the three-row single-player menu");
    if(season)resume(game);
}
void text(NativeGameSession& game,const std::string& name){for(unsigned char c:name){NativeGameInput i;i.rawKeys.push_back({*nativeAmigaRawKey(c,0,false),true});game.update(0,i);i.rawKeys[0].pressed=false;game.update(0,i);}}
std::unique_ptr<NativeGameSession> start(const std::filesystem::path& pack,const std::filesystem::path& root){
    auto game=std::make_unique<NativeGameSession>(pack,std::filesystem::path{});game->enableCareers(root);game->setNavigation(NativeMenuNavigation::Responsive);game->update(0,{});return game;
}
std::vector<std::uint8_t> progress(const NativeMemory& m){
    std::vector<std::uint8_t> result(m.begin()+0x1c9c2,m.begin()+0x1ca44);
    for(auto range:{std::pair<unsigned,unsigned>{0x625a2,5},{0x636b8,4}})result.insert(result.end(),m.begin()+range.first,m.begin()+range.first+range.second);
    return result;
}
void image(NativeGameSession& game,const std::filesystem::path& path){
    std::ofstream out(path,std::ios::binary);out<<"P6\n320 200\n255\n";for(auto pixel:game.render())for(unsigned shift:{16u,8u,0u})out.put(char(pixel>>shift));
}
void corrupt(const std::filesystem::path& path){std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<"damaged";}
NativeHallBanks hallTemplate(){NativeHallBanks hall;for(unsigned n=0;n<512;n+=16){std::fill_n(hall.begin()+n,12,' ');hall[n+12]=9;hall[n+13]=0x59;hall[n+14]=0x99;hall[n+15]=0;}return hall;}
void storage(const std::filesystem::path& root){
    std::filesystem::create_directories(root);const auto id=NativeCareerStore::newId();NativeCareerInfo info{id,"duplicate",0,0,1};
    NativeCareerStore store(root);for(unsigned n=1;n<=4;++n){info.lastPlayed=n;store.saveCareer(info,{std::uint8_t(n)});require(store.flush(),"Career write failed");}
    const auto path=root/"careers"/(id+".scr-career");
    require(store.readCareer(id).generation==4,"Career generation did not advance");
    corrupt(path);auto saved=store.readCareer(id);require(saved.state==CareerFileState::Recovered&&saved.generation==3,"Newest valid recovery copy was not selected");
    corrupt(path.string()+".bak1");saved=store.readCareer(id);require(saved.state==CareerFileState::Recovered&&saved.generation==2,"Second recovery copy unavailable");
    std::filesystem::remove(path);require(store.list().size()==1,"Missing head hid recoverable career");
    require(store.removeCareer(id)&&store.list().empty(),"Deleted career resurrected from backups");
    auto hall=hallTemplate();store.saveHall(hall);require(store.flush(),"Hall write failed");
    auto better=hall;better[0]='A';better[12]=0;better[13]=1;require(mergeNativeHall(hall,better),"Better record not merged");
    better[0]='B';require(!mergeNativeHall(hall,better)&&hall[0]=='A',"Tied record replaced its holder");
    store.saveHall(hall);require(store.flush(),"Improved Hall write failed");corrupt(root/"hall.scr-hall");require(store.readHall().state==CareerFileState::Recovered,"Hall recovery failed");
    corrupt(root/"hall.scr-hall.bak1");require(store.resetHall(hallTemplate()),"Explicit fresh Hall failed");
    bool preserved=false;for(const auto& entry:std::filesystem::directory_iterator(root))preserved|=entry.path().string().find("damaged-")!=std::string::npos;require(preserved,"Damaged Hall evidence was removed");
    const auto locked=CreateFileW((root/"hall.scr-hall.tmp").c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(locked!=INVALID_HANDLE_VALUE,"Cannot inject save failure");store.saveHall(hall);require(!store.flush()&&store.failed(),"Write failure was hidden");
    require(store.readHall().state==CareerFileState::Valid,"Failure damaged last good head");CloseHandle(locked);store.retry();require(store.flush(),"Save retry failed");
    {std::fstream file(root/"hall.scr-hall",std::ios::binary|std::ios::in|std::ios::out);file.seekp(4);file.put(99);}
    require(store.readHall().state==CareerFileState::Unsupported,"Unknown envelope was accepted");store.saveHall(hall);require(!store.flush(),"Unsupported Hall was overwritten");
}
void create(NativeGameSession& game,const std::string& name){
    tap(game);require(game.careerHome()&&game.menuState().count==0&&!game.enteringName(),"Empty Single Player entry did not show only New Career");tap(game);
    require(game.enteringName(),"New Career did not open name entry");text(game,name);tap(game);require(game.openingPage()==NativeOpeningPage::Roster,"Career roster omitted");tap(game);
    require(game.activeCareer().has_value()&&game.page()==NativeMenuPage::Main&&game.menuState().count==2&&!game.hasSuspendedSeason(),"First career was not durably registered at its three-row menu");
}
void practiseFromMenu(NativeGameSession& game,bool finish=true){
    require(game.page()==NativeMenuPage::Main,"Practice did not begin at the profile menu");choose(game,1);tap(game);
    if(game.page()==NativeMenuPage::Division)tap(game);
    require(game.page()==NativeMenuPage::Track,"Practice track selection missing");choose(game,0);tap(game);tap(game);
    require(game.page()==NativeMenuPage::Race,"Practice race missing");for(unsigned n=0;n<24;++n)game.update(.12,{});
    if(!finish)return;
    NativeGameInput quit;quit.quit=true;
    for(unsigned n=0;n<600&&game.page()==NativeMenuPage::Race;++n){game.update(.12,quit);if(game.raceEnded()&&!game.transitioning())tap(game);}
    require(game.page()==NativeMenuPage::Track,"Practice did not return to track selection");back(game);back(game);
    require(game.page()==NativeMenuPage::Main,"Practice Back did not return to the profile menu");
}
std::unique_ptr<NativeGameSession> reload(std::unique_ptr<NativeGameSession> game,const std::filesystem::path& pack,const std::filesystem::path& root){
    const auto page=game->page();const auto state=progress(game->memory());const auto pixels=game->render();require(game->requestClose(),"Healthy close did not flush");game.reset();
    game=start(pack,root);continueLast(*game,page!=NativeMenuPage::Main);
    require(game->page()==page,"Resume Season restored the wrong stage");require(progress(game->memory())==state,"Opening a career changed career, fixture, damage or RNG");
    require(game->render()==pixels,"Resume Season changed the pending report image");return game;
}
void careerFlow(const std::filesystem::path& pack,const std::filesystem::path& root){
    auto game=start(pack,root);create(*game,"ALPHA");image(*game,root/"career.ppm");const auto first=game->activeCareer()->id;
    choose(*game,2);tap(*game);require(game->page()==NativeMenuPage::Matchup,"Season did not start");
    game=reload(std::move(game),pack,root);const auto fixture=progress(game->memory());
    game->update(0,input(0,true));game->update(.5,input(0,true));require(game->page()==NativeMenuPage::Main&&game->hasSuspendedSeason(),"Held Back crossed the profile's main menu");game->update(0,{});
    image(*game,root/"resume-menu.ppm");back(*game);require(game->careerHome(),"Main-menu Back did not reach career selection");image(*game,root/"home.ppm");tap(*game);
    require(game->page()==NativeMenuPage::Main,"In-memory Continue bypassed the profile menu");
    choose(*game,3);require(game->menuState().selected<=2,"Removed fourth main-menu row was still selectable");tap(*game,2);require(game->menuState().selected==2,"Main menu did not clamp at Resume Season");
    practiseFromMenu(*game);require(game->hasSuspendedSeason(),"Practice discarded the suspended matchup");
    game=reload(std::move(game),pack,root);require(game->hasSuspendedSeason(),"Reopening profile lost its suspended season");resume(*game);
    require(game->page()==NativeMenuPage::Matchup&&progress(game->memory())==fixture,"In-memory Resume Season altered fixture");
    tap(*game);const auto prepared=progress(game->memory());back(*game);tap(*game);require(game->page()==NativeMenuPage::Preview&&progress(game->memory())==prepared,"Preview Back repeated setup RNG");
    tap(*game);const auto raceStart=progress(game->memory());for(unsigned n=0;n<16;++n)game->update(.12,{});require(game->race().liveTicks()==16,"Autosave changed physics timing");
    require(game->requestClose(),"Race close did not flush");game.reset();game=start(pack,root);continueLast(*game,true);
    require(game->page()==NativeMenuPage::Matchup,"Interrupted race did not resume at matchup");
    tap(*game);tap(*game);require(progress(game->memory())==raceStart,"Interrupted fixture repeated setup or lost pre-race state");
    require(game->render(false,false,NativeView::Widescreen).size()==432*200,"Career race did not use widescreen presentation");
    require(game->render(false,false,NativeView::Ultrawide).size()==864*200,"Career race did not use ultrawide presentation");
    for(unsigned n=0;n<2400&&!game->raceEnded();++n){if(n%100==0)game->render(false,false,NativeView::Ultrawide);game->update(.12,{});}
    require(game->raceEnded(),"Opponent did not finish fixture");const auto finishImage=game->render();
    require(finishImage.size()==320*200,"Ultrawide changed the persisted career finish image codec");
    require(game->requestClose(),"Finish checkpoint did not flush");
    const auto pending=root/"pending-finish";CareerBytes finishCheckpoint;NativeCareerInfo finishInfo;
    {NativeCareerStore source(root),target(pending);finishInfo=decodeCareerInfo(source.readCareer(first).data,&finishCheckpoint);target.saveCareer(finishInfo,finishCheckpoint);target.setLastUsed(first);const auto hall=source.readHall();NativeHallBanks banks;std::copy(hall.data.begin(),hall.data.end(),banks.begin());target.saveHall(banks);require(target.flush(),"Cannot retain pending-finish comparison fixture");}
    tap(*game);const auto scored=progress(game->memory());const auto scoredImage=game->render();
    {auto restored=start(pack,pending);continueLast(*restored,true);require(restored->raceEnded(),"Comparison fixture lost terminal stage");tap(*restored);
        require(progress(restored->memory())==scored&&restored->render()==scoredImage,"Restored finish differed from uninterrupted exit/scoring");}
    // Return the retained pre-acknowledgement checkpoint to this test's store.
    game.reset();{NativeCareerStore target(root);target.saveCareer(finishInfo,finishCheckpoint);require(target.flush(),"Cannot restore pending finish fixture");}
    game=start(pack,root);continueLast(*game,true);
    require(game->raceEnded(),"Finish stage was not durable before acknowledgement");require(game->render()==finishImage,"Restored finish message pixels differ");
    back(*game);require(game->page()==NativeMenuPage::Main&&game->hasSuspendedSeason(),"Finish Back did not reach the profile menu");
    practiseFromMenu(*game,false);require(game->requestClose(),"Interrupted Practice did not flush the saved season");game.reset();game=start(pack,root);continueLast(*game);
    require(game->hasSuspendedSeason(),"Interrupted Practice lost the pending finish");resume(*game);require(game->raceEnded()&&game->render()==finishImage,"Practice altered or acknowledged the pending finish message");tap(*game);require(game->page()==NativeMenuPage::Results,"Restored finish did not lead to results");
    for(unsigned n=0;n<12&&game->page()!=NativeMenuPage::Matchup;++n){game=reload(std::move(game),pack,root);const auto phase=game->page();const auto state=progress(game->memory());back(*game);resume(*game);require(game->page()==phase&&progress(game->memory())==state,"Back advanced a report");tap(*game);}
    require(game->page()==NativeMenuPage::Matchup,"Reports did not reach the next fixture");
    back(*game);back(*game);choose(*game,1);tap(*game);require(game->enteringName(),"New Career did not open setup");text(*game,"ALPHA");tap(*game);tap(*game);
    require(game->activeCareer()->id!=first,"Duplicate driver name overwrote a career");const auto second=game->activeCareer()->id;
    back(*game);choose(*game,2);tap(*game);tap(*game,2);tap(*game);tap(*game);
    require(game->activeCareer()->id==first&&game->page()==NativeMenuPage::Main&&game->hasSuspendedSeason(),"Browse bypassed the main menu or lost another career's season");
    resume(*game);require(game->page()==NativeMenuPage::Matchup,"Browsing another career lost its saved fixture");back(*game);back(*game);
    choose(*game,2);tap(*game);tap(*game,2);tap(*game);tap(*game);
    require(game->activeCareer()->id==second&&game->page()==NativeMenuPage::Main&&!game->hasSuspendedSeason(),"Season state leaked between career identities");
    back(*game);choose(*game,2);tap(*game);image(*game,root/"list.ppm");
    tap(*game);choose(*game,1);tap(*game);image(*game,root/"delete.ppm");choose(*game,1);tap(*game);back(*game);
    require(game->careerHome()&&game->menuState().count==1,"Deleting last-used did not hide Continue");
    require(game->requestClose(),"Career management close failed");game.reset();NativeCareerStore store(root);require(store.list().size()==1&&store.list()[0].id==first&&store.lastUsed().empty(),"Delete removed wrong identity or retained pointer");
}
void failures(const std::filesystem::path& pack,const std::filesystem::path& root){
    auto game=start(pack,root);tap(*game);tap(*game);text(*game,"RECOVER");tap(*game);
    require(game->openingPage()==NativeOpeningPage::Roster,"Failure fixture missed roster");
    {std::ofstream block(root/"careers");block<<"blocked folder";}tap(*game);
    require(!game->activeCareer()&&game->page()==NativeMenuPage::Opening,"First-save failure allowed an unregistered career");image(*game,root/"first-save-error.ppm");
    std::filesystem::remove(root/"careers");tap(*game);require(game->activeCareer().has_value(),"Retry did not register first durable career");
    const auto path=root/"careers"/(game->activeCareer()->id+".scr-career.tmp");
    const auto lock=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);require(lock!=INVALID_HANDLE_VALUE,"Cannot lock checkpoint temporary file");
    choose(*game,2);tap(*game);const auto fixture=progress(game->memory());require(!game->requestClose(),"Failed write permitted silent close");
    require(game->closePending(),"Failed close did not expose its pending save decision");
    require(!game->requestClose(),"Repeated close bypassed the pending failure choice");
    image(*game,root/"departure-error.ppm");choose(*game,2);tap(*game);game->update(0,{}); // cancel close, then acknowledge the safe-menu warning
    require(!game->closePending()&&!game->closeReady(),"Cancelled close still appeared pending or ready");
    if(game->page()==NativeMenuPage::Opening){choose(*game,1);tap(*game);}
    require(game->page()==NativeMenuPage::Matchup,"Cancel close discarded RAM progress");back(*game);back(*game);tap(*game);resume(*game);
    require(game->page()==NativeMenuPage::Matchup&&progress(game->memory())==fixture&&game->saveStatus()=="NOT SAVED","Continue used stale disk state after write failure");
    CloseHandle(lock);back(*game);resume(*game);require(game->requestClose(),"Checkpoint did not recover after storage became writable");game.reset();
    game=start(pack,root);continueLast(*game,true);require(game->page()==NativeMenuPage::Matchup&&progress(game->memory())==fixture,"Retry did not persist retained RAM state");
}
void practicePolicies(const std::filesystem::path& pack,const std::filesystem::path& root){
    for(auto policy:{NativeMenuNavigation::Original,NativeMenuNavigation::Responsive})for(auto preset:{DriveControlPreset::Original,DriveControlPreset::Modern}){
        const auto folder=root/(std::to_string(unsigned(policy))+"-"+std::to_string(unsigned(preset)));auto game=start(pack,folder);create(*game,"PRACTICE");
        const std::vector<std::uint8_t> before(game->memory().begin()+0x1c9c2,game->memory().begin()+0x1c9d1);game->setNavigation(policy);
        const auto settle=[&](){for(unsigned n=0;game->transitioning()&&n<20;++n)game->update(1,{});require(!game->transitioning(),"Policy transition did not settle");};
        const auto ready=[&](){settle();game->update(1,{});game->update(1,{});};
        const auto select=[&](unsigned row){ready();game->update(0,input(0,false,1u<<row));ready();};
        const auto confirm=[&](){ready();game->update(0,input(16));if(policy==NativeMenuNavigation::Original){game->update(1,{});if(game->page()!=NativeMenuPage::Race)game->update(1,{});}settle();};
        select(1);confirm();require(game->page()==NativeMenuPage::Division,"Practice division missing with navigation policy");confirm();confirm();
        require(game->page()==NativeMenuPage::Preview,"Practice preview missing with navigation policy");confirm();
        require(game->page()==NativeMenuPage::Race,"Practice did not start with navigation policy");
        NativeGameInput driving;driving.drivePreset=preset;
        for(unsigned n=0;n<48;++n)game->update(.12,driving);
        driving.quit=true;for(unsigned n=0;n<600&&game->page()==NativeMenuPage::Race;++n){game->update(.12,driving);if(game->raceEnded()&&!game->transitioning())tap(*game);}
        settle();require(game->page()==NativeMenuPage::Track,"Practice exit lost original track return");
        require(std::equal(before.begin(),before.end(),game->memory().begin()+0x1c9c2),"Practice altered career order, damage, division or league");
        const auto after=progress(game->memory());require(game->requestClose(),"Practice checkpoint flush failed");game.reset();game=start(pack,folder);continueLast(*game);
        require(game->page()==NativeMenuPage::Main&&progress(game->memory())==after,"Practice Continue lost resulting RNG or did not return to career menu");
        game->setNavigation(policy);back(*game);require(game->careerHome(),"Career Back failed under a navigation policy");tap(*game);
        select(2);confirm();require(game->page()==NativeMenuPage::Matchup,"Policy season fixture missing");const auto state=progress(game->memory());back(*game);require(game->page()==NativeMenuPage::Main&&game->hasSuspendedSeason(),"Matchup Back failed under a navigation policy");select(2);confirm();
        require(game->page()==NativeMenuPage::Matchup&&progress(game->memory())==state,"Policy Continue changed prepared season");
    }
}
void leaveRacePersistence(const std::filesystem::path& pack,const std::filesystem::path& root){
    for(auto policy:{NativeMenuNavigation::Original,NativeMenuNavigation::Responsive})for(unsigned crash=0;crash<2;++crash){
        const auto folder=root/(std::to_string(unsigned(policy))+"-"+std::to_string(crash));auto game=start(pack,folder);create(*game,"LEAVER");
        choose(*game,2);tap(*game);require(game->page()==NativeMenuPage::Matchup,"Leave Race career fixture missed matchup");
        tap(*game);tap(*game);require(game->page()==NativeMenuPage::Race,"Leave Race career fixture missed race");game->setNavigation(policy);
        const auto crashed=[&](){const auto& frame=game->race().frames().back();return !frame.nearInputs.drawing.raceStartTimer&&(frame.nearInputs.progress.offTrackFlags&0x80);};
        NativeGameInput driving;driving.driveControls=21;
        for(unsigned n=0;crash&&!crashed()&&n<1200&&!game->raceEnded();++n)game->update(.12,driving);
        require(!crash||crashed(),"Leave Race career fixture missed crash");
        const auto ticks=game->race().liveTicks(),damage=unsigned(game->race().frames().back().damage);
        game->render(true,false,NativeView::Ultrawide);NativeGameInput leave;leave.leaveRace=true;game->update(0,leave);
        for(unsigned n=0;game->transitioning()&&n<20;++n)game->update(1,{});
        require(game->page()==NativeMenuPage::Results&&game->race().liveTicks()==ticks,"Career Leave Race waited for recovery or skipped results");
        require(game->memory()[0x1bbb4]==0xc0&&game->memory()[0x1bb55]==damage,"Career Leave Race lost the abandoned outcome or damage");
        const auto state=progress(game->memory());const auto pixels=game->render();require(game->requestClose(),"Leave Race results did not flush");
        game.reset();game=start(pack,folder);continueLast(*game,true);
        require(game->page()==NativeMenuPage::Results&&progress(game->memory())==state&&game->render()==pixels,"Leave Race results were lost or scored again on Continue");
    }
}
void paging(const std::filesystem::path& pack,const std::filesystem::path& root){
    auto game=start(pack,root);create(*game,"MANY");require(game->requestClose(),"List fixture did not flush");const auto first=game->activeCareer()->id;game.reset();
    {NativeCareerStore store(root);CareerBytes bytes;auto info=decodeCareerInfo(store.readCareer(first).data,&bytes);
        for(unsigned n=0;n<35;++n){info.id=NativeCareerStore::newId();info.name="CAREER "+std::to_string(n);info.lastPlayed+=n+1;store.saveCareer(info,bytes);}require(store.flush()&&store.list().size()==36,"Careers retained the old slot limit");}
    game=start(pack,root);tap(*game);choose(*game,2);tap(*game);tap(*game,8);tap(*game,2);image(*game,root/"scrolled-list.ppm");tap(*game);tap(*game);
    require(game->activeCareer()&&game->activeCareer()->id!=first&&game->page()==NativeMenuPage::Main,"Scrolled career could not be explicitly selected");
}
void draftsAndRecovery(const std::filesystem::path& pack,const std::filesystem::path& root){
    auto game=start(pack,root);tap(*game);tap(*game);text(*game,"DRAFT");back(*game);require(game->careerHome()&&!game->activeCareer(),"Cancelling setup did not return to its parent or registered a career");
    tap(*game);require(game->enteringName(),"New Career did not resume its draft");tap(*game);tap(*game);
    require(game->activeCareer()&&game->activeCareer()->name=="draft","Cancelled setup lost its draft name");
    const auto id=game->activeCareer()->id;const auto state=progress(game->memory());require(game->requestClose(),"Recovery fixture did not flush");game.reset();
    {NativeCareerStore store(root);CareerBytes bytes;auto info=decodeCareerInfo(store.readCareer(id).data,&bytes);store.saveCareer(info,bytes);require(store.flush(),"Recovery fixture generation failed");store.saveCareer(info,{1,2,3});require(store.flush(),"Cannot inject semantically damaged checkpoint");}
    game=start(pack,root);tap(*game);tap(*game);require(game->page()==NativeMenuPage::Opening&&!game->careerHome(),"Recovered native checkpoint body did not notify the player");tap(*game);
    require(game->page()==NativeMenuPage::Main&&progress(game->memory())==state,"Checkpoint body recovery changed native state");require(game->requestClose(),"Recovered checkpoint could not be saved");game.reset();
    const auto path=root/"careers"/(id+".scr-career");for(unsigned n=0;n<3;++n)corrupt(n?std::filesystem::path(path.string()+".bak"+std::to_string(n)):path);
    game=start(pack,root);tap(*game);require(game->careerHome()&&game->menuState().count==1&&!game->activeCareer(),"Unavailable last-used career did not hide Continue and retain New/Browse");game.reset();
    require(std::filesystem::file_size(path)==7,"Unreadable career evidence was overwritten");
}
void seasonEndings(const std::filesystem::path& pack,const std::filesystem::path& root){
    for(unsigned league:{0u,11u})for(unsigned division=0;division<4;++division){
        const auto folder=root/(std::to_string(league)+"-"+std::to_string(division));auto game=start(pack,folder);create(*game,"SEASON");
        auto& memory=const_cast<NativeMemory&>(game->memory());const unsigned offset=memory[0x5ab42+division];std::swap(memory[0x1c9c2+11],memory[0x1c9c2+offset+1]);
        memory[0x1c9ce]=std::uint8_t(division);memory[0x1c9d0]=std::uint8_t(league);memory[0x1c9cf]=7;
        choose(*game,2);tap(*game);require(game->page()==NativeMenuPage::Matchup,"Context season fixture missing");
        NativeGameInput f1;f1.rawKeys.push_back({0x50,true});game->update(0,f1);tap(*game);
        require(game->page()==NativeMenuPage::SeasonEnd,"Context forfeit did not finish season");
        for(unsigned n=0;n<4&&game->page()!=NativeMenuPage::Main;++n){game=reload(std::move(game),pack,folder);tap(*game);}
        require(game->page()==NativeMenuPage::Main&&game->memory()[0x1ca35]==0,"Restored award/roster did not perform final reset once");
        for(unsigned n=0;n<48;++n)require(game->memory()[0x1c9de+n]==0,"Restored season ending left prior scores");
        game=reload(std::move(game),pack,folder);choose(*game,2);tap(*game);require(game->page()==NativeMenuPage::Matchup,"Next season did not continue after restored ending");
    }
}
void earnedRecords(const std::filesystem::path& pack,const std::filesystem::path& root){
    namespace timing=scr::state::timing;
    for(unsigned league:{0u,11u})for(bool multiplayer:{false,true}){
        const auto folder=root/(std::to_string(league)+(multiplayer?"-multi":"-single"));auto game=start(pack,folder);
        if(multiplayer){choose(*game,1);tap(*game);text(*game,"FIRST");tap(*game);tap(*game);text(*game,"SECOND");tap(*game);choose(*game,1);tap(*game);tap(*game);}
        else create(*game,"RECORD");
        require(game->page()==NativeMenuPage::Main,"Record fixture missed main menu");
        const_cast<NativeMemory&>(game->memory())[0x1c9d0]=std::uint8_t(league);choose(*game,2);tap(*game);tap(*game);tap(*game);
        require(game->page()==NativeMenuPage::Race,"Record fixture missed competitive race");
        auto data=startNativeGame(pack);auto memory=std::make_unique<NativeMemory>(game->memory());auto in=timing::readInputs(memory->data());auto state=timing::readState(memory->data());
        in.segmentIndex={1,3};in.trackProgress={0x100,0x100};in.lapLineSegment=1;in.finishLineSegment=0;in.numberOfTrackSegments=44;
        in.maximumLaps=4;in.raceStartTimer=0;in.wheelMovementActive=0x80;in.offTrackStateFlags=0;
        state.crossingArmed={0,0};state.lapCounter={1,1};state.times[0]={0,0x20,0x10};state.times[14]={9,0x59,0x99};state.raceComplete=0;
        // This fixture earns a human lap through the unchanged timing routine.
        // The observer receives its native state while the race is unfinished.
        (void)scr::updateRaceTiming(in,state);timing::writeInputs(memory->data(),in);timing::writeState(memory->data(),state);
        const auto human=in.currentPlayerId;const auto expected=state.lapRecordName;const unsigned ordinal=(*memory)[0x648c2+(*memory)[0x1ca33]]+(league?8:0);
        const_cast<RaceScene&>(game->race()).loadNativeState(*memory,data.payload,{});game->update(.12,{});
        require(!game->raceEnded(),"Lap persistence fixture unexpectedly finished its race");require(game->requestClose(),"Earned record did not flush");
        // The third completed lap also updates the three-lap record through
        // the same unchanged routine, independently of career checkpointing.
        state.lapCounter[0]=3;state.crossingArmed[0]=0;state.times[0]={0,0x20,0x10};state.times[human+12]={0,0x40,0};state.times[15]={9,0x59,0x99};
        (void)scr::updateRaceTiming(in,state);timing::writeInputs(memory->data(),in);timing::writeState(memory->data(),state);
        const_cast<RaceScene&>(game->race()).loadNativeState(*memory,data.payload,{});game->update(.12,{});require(game->requestClose(),"Three-lap record did not flush");game.reset();
        NativeCareerStore store(folder);const auto saved=store.readHall();require(saved.state==CareerFileState::Valid,"Earned Hall record missing on disk");
        require(std::equal(expected.begin(),expected.end(),saved.data.begin()+ordinal*16)&&saved.data[ordinal*16+12]==0&&saved.data[ordinal*16+13]==0x20,"Unfinished race lost its eligible human lap record");
        require(std::equal(expected.begin(),expected.end(),saved.data.begin()+256+ordinal*16)&&saved.data[256+ordinal*16+12]==1&&saved.data[256+ordinal*16+13]==0,"Three-lap record was not persisted");
        if(!multiplayer){const auto careers=store.list();require(careers.size()==1&&store.removeCareer(careers[0].id),"Record career deletion failed");require(store.readHall().data==saved.data,"Deleting a career removed shared records");}
        require(human<=11,"Record holder is not a human driver");
    }
}
void checkpointContexts(const std::filesystem::path& pack){
    auto base=startNativeGame(pack);initializeNativeOpeningSession(*base.memory);NativeOpeningFlow opening(*base.memory);
    for(unsigned league:{0u,11u})for(unsigned division=0;division<4;++division){
        NativeCareerCheckpoint c;c.memory=std::make_unique<NativeMemory>(*base.memory);initializeNativeMenuSession(*c.memory);
        (*c.memory)[0x1c9ce]=std::uint8_t(division);(*c.memory)[0x1c9d0]=std::uint8_t(league);
        for(auto page:{NativeMenuPage::Main,NativeMenuPage::Matchup,NativeMenuPage::Results,NativeMenuPage::Records,NativeMenuPage::RoundResults,NativeMenuPage::Standings,NativeMenuPage::SeasonEnd,NativeMenuPage::SeasonAward,NativeMenuPage::Roster}){
            c.menu.page=page;c.menu.competitive=page!=NativeMenuPage::Main;c.menu.division=division;c.menu.context=league;
            const auto saved=encodeNativeCheckpoint(*base.memory,c);auto restored=decodeNativeCheckpoint(*base.memory,saved);
            require(*restored.memory==*c.memory&&restored.menu.page==page,"Checkpoint roundtrip lost native bytes or stage");
            auto prior=saved;prior[0]=1;prior.resize(prior.size()-4);const auto legacyNative=decodeNativeCheckpoint(*base.memory,prior);
            require(*legacyNative.memory==*c.memory,"Existing native career checkpoint was not retained");
            if(c.menu.competitive){
                NativeCareerCheckpoint home;home.memory=std::make_unique<NativeMemory>(*base.memory);initializeNativeMenuSession(*home.memory);home.suspendedSeason=saved;
                const auto profile=encodeNativeCheckpoint(*base.memory,home);const auto restoredHome=decodeNativeCheckpoint(*base.memory,profile);
                require(restoredHome.suspendedSeason==saved&&restoredHome.menu.page==NativeMenuPage::Main,"Profile checkpoint lost its independent season");
                auto invalid=profile;invalid[invalid.size()-saved.size()]=99;bool rejected=false;try{(void)decodeNativeCheckpoint(*base.memory,invalid);}catch(...){rejected=true;}require(rejected,"Unsupported nested season checkpoint was accepted");
            }
            auto truncated=saved;truncated.pop_back();bool rejected=false;try{(void)decodeNativeCheckpoint(*base.memory,truncated);}catch(...){rejected=true;}require(rejected,"Truncated checkpoint accepted");
        }
    }
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::invalid_argument("native_autosave_tests pack output");const std::filesystem::path root=argv[2];
    std::filesystem::create_directories(root);storage(root/"storage");checkpointContexts(argv[1]);careerFlow(argv[1],root/"flow");failures(argv[1],root/"failures");practicePolicies(argv[1],root/"policies");leaveRacePersistence(argv[1],root/"leave-race");paging(argv[1],root/"paging");draftsAndRecovery(argv[1],root/"recovery");seasonEndings(argv[1],root/"endings");earnedRecords(argv[1],root/"records");
    std::cout<<"{\"status\":\"passed\",\"checkpoint_contexts\":8,\"checkpoint_phases\":9,\"season_ending_contexts\":8,\"practice_policy_cases\":4,\"listed_careers\":36,\"record_mode_league_cases\":4,\"finish_before_acknowledgement\":true,\"finish_scoring_matches_uninterrupted\":true,\"interrupted_fixture\":true,\"duplicate_names\":true,\"delete\":true,\"recovery\":true,\"write_retry\":true,\"record_merge\":true,\"three_lap_records\":true,\"setup_draft\":true,\"conditional_entry_rows\":true,\"three_row_main_menu\":true,\"continue_opens_main_menu\":true,\"resume_season\":true,\"practice_preserves_suspended_season\":true,\"existing_native_careers\":true,\"career_switch_isolation\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
