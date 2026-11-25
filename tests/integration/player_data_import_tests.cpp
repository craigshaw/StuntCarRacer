#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "persistence/player_data_import.h"
#include "persistence/career_file_io.h"
#include "persistence/player_preferences.h"
#include "persistence/windows_file_path.h"
#include "persistence/shared_hall_store.h"
#include "persistence/enhanced_hall_store.h"
#include "persistence/native_career_checkpoint.h"
#include "session/native_game_session.h"
#include "launcher/native_launcher.h"
#include "platform/windows/launcher.h"
#include "platform/windows/player_data_ownership.h"
#include "../support/ghost_lap_fixture.h"
#include "../support/hall_ghost_fixture.h"
#include "../support/enhanced_record_fixture.h"
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace scr::runtime;namespace fs=std::filesystem;
unsigned checks{},crashes{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,message);}
CareerBytes bytes(const fs::path& p){std::ifstream in(windowsFilePath(p),std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void write(const fs::path& p,const CareerBytes& b){std::string error;check(career_files::atomicWrite(p,b,error),"Cannot write fixture");}
std::map<fs::path,CareerBytes> snapshot(const fs::path& root){std::map<fs::path,CareerBytes> result;if(fs::exists(root))for(const auto& e:fs::recursive_directory_iterator(root))if(e.is_regular_file())result[e.path().lexically_relative(root)]=bytes(e.path());return result;}
NativeGameInput input(unsigned controls=0,unsigned row=0,bool back=false){NativeGameInput i;i.menu={std::uint8_t(controls),std::uint8_t(row),back};return i;}
void tap(NativeGameSession& g,unsigned controls=16){g.update(0,{});g.update(0,input(controls));g.update(0,{});}
void choose(NativeGameSession& g,unsigned row){g.update(0,{});g.update(0,input(0,1u<<row));g.update(0,{});}
void text(NativeGameSession& g,const std::string& value){for(unsigned char c:value){NativeGameInput i;i.rawKeys.push_back({*nativeAmigaRawKey(c,0,false),true});g.update(0,i);i.rawKeys[0].pressed=false;g.update(0,i);}}
std::unique_ptr<NativeMemory> baseline(const fs::path& pack){auto data=startNativeGame(pack);initializeNativeOpeningSession(*data.memory);NativeOpeningFlow flow(*data.memory);return std::move(data.memory);}
std::string fixture(const fs::path& pack,const fs::path& source){
    fs::create_directories(source/L"assets");fs::copy_file(pack,source/L"assets/stunt-car-racer.scr-assets");std::string id;
    {NativeGameSession g(pack,{});g.enableCareers(source);g.setNavigation(NativeMenuNavigation::Responsive);g.update(0,{});tap(g);tap(g);text(g,"CRAIG");tap(g);tap(g);check(g.activeCareer().has_value(),"Cannot create legacy career");id=g.activeCareer()->id;
        choose(g,2);tap(g);check(g.page()==NativeMenuPage::Matchup,"Fixture omitted suspended season");g.update(0,input(0,0,true));check(g.requestClose(),"Cannot close legacy career");}
    const auto content=ghostTrackContentIdentity(startNativeGame(pack).payload);
    {NativeCareerStore store(source);for(unsigned track=0;track<8;++track)for(bool super:{false,true}){
        auto lap=scr::test::ghost::lap(id,track,super);lap.context.trackContentIdentity=content;
        for(unsigned n=0;n<3;++n){auto best=scr::test::enhanced::best(240000-60000*n,true,track);best.catalog={content,1,enhancedTimingMethodVersion};best.key={id,track,super};store.saveGhost(lap);store.saveEnhancedBest(best);check(store.flush(),"Cannot seed PB generations");}}
        CareerBytes checkpoint;auto info=decodeCareerInfo(store.readCareer(id).data,&checkpoint);for(unsigned n=0;n<2;++n){store.saveCareer(info,checkpoint);check(store.flush(),"Cannot seed career recovery");}}
    NativeDisplayPreferences p;p.controls=DriveControlPreset::Modern;p.view=NativeView::Ultrawide;p.interpolate=true;check(saveDisplayPreferences(source/L"settings/display.ini",p),"Cannot seed legacy settings");
    {NativeGameSession g(pack,{});g.enableCareers(source);g.enableMultiplayerGames(source/L"settings/display.ini");g.setNavigation(NativeMenuNavigation::Responsive);g.update(0,{});choose(g,1);tap(g);tap(g);g.update(0,{});text(g,"FRIDAY");tap(g);choose(g,2);tap(g);text(g,"ONE");tap(g);tap(g);text(g,"TWO");tap(g);choose(g,1);tap(g);tap(g);check(g.multiplayerHome(),"Cannot seed named game");tap(g);check(g.requestClose(),"Cannot close multiplayer fixture");}
    SharedHallArchive original({source/L"shared-hall",HallDirectoryAccess::Inherited},{content,1});EnhancedHallArchive enhanced({source/L"shared-hall-enhanced-v1",HallDirectoryAccess::Inherited},{content,1,enhancedTimingMethodVersion});
    for(unsigned n=1;n<=3;++n){auto a=scr::test::hall::attempt(n);auto ghost=scr::test::hall::ghost(a);ghost.catalog={content,1};original.commit({std::make_shared<const HallSubmission>(makeHallSubmission(a,&ghost))});
        auto e=scr::test::enhanced::attempt(n,240000);auto eg=scr::test::enhanced::ghost(e);eg.catalog={content,1,enhancedTimingMethodVersion};enhanced.commit({std::make_shared<const EnhancedSubmission>(makeEnhancedSubmission(e,&eg))});}
    write(source/L"saves/stunt-car-racer.scr-save",{'l','e','g','a','c','y'});write(source/L"hall.scr-hall",{'o','l','d'});return id;
}
void migration(const fs::path& pack,const fs::path& source,const fs::path& root,const std::string& id){
    const auto before=snapshot(source);PlayerDataImport importer(root);check(!importer.decided(),"New collection was already decided");const auto report=importer.import(playerImportSource(source));
    check(report.careers==1&&report.multiplayer==1&&report.assets&&report.settings&&report.originalHall&&report.enhancedHall,"Recognised collection did not migrate completely");check(before==snapshot(source),"Migration changed a source file or created a source lock");check(importer.decided(),"Import decision was not durable");
    check(!fs::exists(root/L"saves")&&!fs::exists(root/L"hall.scr-hall"),"Migration imported excluded legacy data");
    const auto base=baseline(pack);NativeCareerStore store(root,{}, {CareerStorageLayout::Folders,{},{}});NativeMultiplayerStore multiplayer(store,*base);
    check(store.list().size()==1&&multiplayer.list().size()==1&&store.lastUsed()==id,"Imported selections or discovery needed a legacy catalogue");const auto career=store.list().front();
    for(unsigned n=0;n<3;++n)check(bytes(career_files::generationPath(career.folder/L"career.scr-career",n))==bytes(career_files::generationPath(source/L"careers"/(id+".scr-career"),n)),"Import rewrote a checkpoint generation");
    const auto sourceMultiplayer=fs::directory_iterator(source/L"multiplayer")->path();check(snapshot(sourceMultiplayer)==snapshot(multiplayer.list().front().folder),"Native multiplayer import changed folder contents or generations");
    for(unsigned track=0;track<8;++track)for(bool super:{false,true})for(unsigned n=0;n<3;++n){const auto key=std::to_string(track)+(super?"-super":"-normal");
        check(fs::exists(career_files::generationPath(source/L"ghosts"/id/(key+".scr-ghost"),n))&&fs::exists(career_files::generationPath(source/L"ghosts-enhanced-v1"/id/(key+".scr-ebest"),n)),"PB fixture omitted a recovery generation");
        check(bytes(career_files::generationPath(career.folder/L"personal-bests/original"/(key+".scr-ghost"),n))==bytes(career_files::generationPath(source/L"ghosts"/id/(key+".scr-ghost"),n)),"Original PB recovery bytes changed");
        check(bytes(career_files::generationPath(career.folder/L"personal-bests/enhanced"/(key+".scr-ebest"),n))==bytes(career_files::generationPath(source/L"ghosts-enhanced-v1"/id/(key+".scr-ebest"),n)),"Enhanced PB recovery bytes changed");}
    check(snapshot(source/L"shared-hall")==snapshot(root/L"hall-of-fame/original")&&snapshot(source/L"shared-hall-enhanced-v1")==snapshot(root/L"hall-of-fame/enhanced"),"Hall indexes/objects changed or were seeded as new attempts");
    check(loadDisplayPreferences(root/L"settings.ini")==loadDisplayPreferences(source/L"settings/display.ini"),"Settings migration changed supported choices");
    const auto complete=snapshot(root);const auto again=importer.import(playerImportSource(source));check(!again.careers&&!again.multiplayer&&again.skipped>=4&&snapshot(root)==complete,"Repeated import was not idempotent");
    NativeGameSession g(pack,{});g.enableCareers(root,playerDataLayout(root).originalHall,playerDataLayout(root).enhancedHall);g.enableMultiplayerGames();g.setNavigation(NativeMenuNavigation::Responsive);g.update(0,{});tap(g);tap(g);check(g.hasSuspendedSeason(),"Imported career lost its season continuation");choose(g,2);tap(g);CareerBytes body;decodeCareerInfo(store.readCareer(id).data,&body);const auto c=decodeNativeCheckpoint(*base,body);const auto season=decodeNativeCheckpoint(*base,c.suspendedSeason);check(g.memory()==*season.memory,"Imported Resume changed native memory or RNG");
    const auto controlRoot=root.parent_path()/L"control-source";FolderTransactions::copy(source,controlRoot);auto control=std::make_unique<NativeGameSession>(pack,fs::path{});control->enableCareers(controlRoot);control->setNavigation(NativeMenuNavigation::Responsive);control->update(0,{});tap(*control);tap(*control);choose(*control,2);tap(*control);check(control->memory()==g.memory(),"Original-layout continuation differs before driving");
    tap(g);tap(*control);tap(g);tap(*control);check(g.page()==NativeMenuPage::Race&&control->page()==NativeMenuPage::Race,"Continuation comparison did not reach driving");NativeGameInput drive;drive.driveControls=5;
    for(unsigned n=0;n<16;++n){g.update(.12,drive);control->update(.12,drive);check(g.memory()==control->memory(),"Imported continuation diverged in subsequent simulation/RNG updates");}
    check(g.requestClose()&&control->requestClose(),"Imported continuation did not close");check(before==snapshot(source),"Continuation comparison changed the real import source");
}
void conflicts(const fs::path& pack,const fs::path& source,const fs::path& root,const std::string& id){
    PlayerDataImport importer(root);importer.import(playerImportSource(source));const auto divergent=root.parent_path()/L"divergent";FolderTransactions::copy(source,divergent);
    {NativeCareerStore old(divergent);CareerBytes body;auto info=decodeCareerInfo(old.readCareer(id).data,&body);info.lastPlayed=0;old.saveCareer(info,body);check(old.flush(),"Cannot seed older incoming copy");}
    const auto hall=snapshot(root/L"hall-of-fame");const auto changed=importer.import(playerImportSource(divergent));check(changed.careers==1&&changed.conflicts==1,"Diverged career was merged or chosen by date");
    NativeCareerStore store(root,{}, {CareerStorageLayout::Folders,{},{}});check(store.list().size()==2&&store.readCareer(id).state==CareerFileState::Conflict,"Imported duplicate bypassed conflict service");
    auto selected=store.list().back();const auto result=store.resolveCopy(selected,CareerCopyChoice::KeepBoth).get();check(result.error.empty()&&result.id!=id,"Imported copy could not use Keep Both");check(snapshot(root/L"hall-of-fame")==hall,"Copy resolution changed the imported Hall");
    // Only explicit archive replacement can change an existing Hall; previous
    // complete archives survive in migration recovery.
    {const auto content=ghostTrackContentIdentity(startNativeGame(pack).payload);SharedHallArchive extra({divergent/L"shared-hall",HallDirectoryAccess::Inherited},{content,1});extra.commit({std::make_shared<const HallSubmission>(makeHallSubmission(scr::test::hall::attempt(999)))});}
    importer.import(playerImportSource(divergent));check(snapshot(root/L"hall-of-fame")==hall,"Import silently replaced the existing Hall");
    const auto replaced=importer.import(playerImportSource(divergent),{false,true,false});check(replaced.originalHall&&!replaced.enhancedHall&&fs::exists(root/L"migration-recovery"),"Explicit Hall replacement did not retain recovery contents");
}
void failureAndChoice(const fs::path& pack,const fs::path& source,const fs::path& root){
    const auto layout=playerDataLayout(root/L"fresh");NativeLauncher launcher({layout.pack,layout.settings,{},layout.root});launcher.configureDataImport(false);check(launcher.importPage()==LauncherImportPage::None&&PlayerDataImport(layout.root).decided(),"Explicit root imported real player data implicitly");
    launcher.openDataImport();check(launcher.importPage()==LauncherImportPage::Review&&!launcher.importRequired(),"Explicit import action missing");launcher.importFolder(source);launcher.applyDataImport();check(launcher.importPage()==LauncherImportPage::Working&&!launcher.saveForPlay(),"Import did not block Play");
    for(unsigned n=0;n<20000&&launcher.importPage()==LauncherImportPage::Working;++n){launcher.pollDataImport();Sleep(1);}check(launcher.importPage()==LauncherImportPage::Report&&!launcher.importRequired()&&launcher.ready(),"Off-thread launcher import failed");launcher.closeDataImport();check(launcher.saveForPlay(),"Completed import did not return to launcher");
    {NativeLauncher hallOnly({layout.pack,layout.settings,{},layout.root});hallOnly.configureDataImport(false);hallOnly.openDataImport();hallOnly.importFolder(source/L"shared-hall",true);check(!hallOnly.importSource().settings.empty(),"Hall-only folder choice omitted the source context");hallOnly.applyDataImport();
        for(unsigned n=0;n<20000&&hallOnly.importPage()==LauncherImportPage::Working;++n){hallOnly.pollDataImport();Sleep(1);}check(hallOnly.importPage()==LauncherImportPage::Report&&hallOnly.importMessage().find(L"could not complete")==std::wstring::npos,"Hall-only import failed");}
    const auto pending=playerDataLayout(root/L"pending");NativeLauncher choice({pending.pack,pending.settings,{},pending.root});choice.configureDataImport(false,playerImportSource(source));check(choice.importRequired()&&choice.importPage()==LauncherImportPage::Review,"First-use import choice was implicit");choice.closeDataImport();check(choice.importRequired(),"Cancelling selected fresh start implicitly");choice.freshData();check(!choice.importRequired()&&!fs::exists(pending.pack),"Fresh start silently copied old assets");
    NativeLauncher reopened({pending.pack,pending.settings,{},pending.root});reopened.configureDataImport(true);check(reopened.importPage()==LauncherImportPage::None&&!reopened.ready(),"A later user/launch repopulated the fresh collection");
    const auto unavailable=root/L"unavailable-source";FolderTransactions::copy(source,unavailable);const auto file=fs::directory_iterator(unavailable/L"careers")->path();write(file,{'b','a','d'});const auto bytesBefore=snapshot(unavailable);PlayerDataImport recovery(root/L"unavailable");const auto report=recovery.import(playerImportSource(unavailable));check(!report.warnings.empty()&&snapshot(unavailable)==bytesBefore,"Damaged generation was lost or unreported");
    const auto blocked=root/L"blocked";fs::create_directories(blocked);write(blocked/L"careers",{'b'});const auto before=snapshot(blocked);rejected([&]{PlayerDataImport(blocked).import(playerImportSource(source));},"Blocked destination silently reset progress");check(before==snapshot(blocked),"Failed staging changed live destination");
    (void)pack;
}
void unavailableFormats(const fs::path& pack,const fs::path& source,const fs::path& root,const std::string& id){
    const auto incoming=root/L"incoming";FolderTransactions::copy(source,incoming);
    auto future=bytes(incoming/L"careers"/(id+".scr-career"));future[4]=99;write(incoming/L"careers"/(id+".scr-career"),future);
    auto hall=bytes(incoming/L"shared-hall/index.scr-hall");hall[4]=99;write(incoming/L"shared-hall/index.scr-hall",hall);
    auto ghost=bytes(incoming/L"ghosts"/id/L"0-normal.scr-ghost.bak1");ghost[4]=99;write(incoming/L"ghosts"/id/L"0-normal.scr-ghost.bak1",ghost);
    write(incoming/L"settings/display.ini",{'f','u','t','u','r','e'});write(incoming/L"careers/bad-name.scr-career",{'b','a','d'});write(incoming/L"ghosts/orphan/keep.scr-ghost",{'o','l','d'});
    const auto before=snapshot(incoming);const auto target=root/L"data";PlayerDataImport importer(target);const auto report=importer.import(playerImportSource(incoming));
    check(!report.originalHall&&report.enhancedHall&&report.warnings.size()>=5,"Unavailable formats were accepted silently or affected another category");check(before==snapshot(incoming),"Unavailable import rewrote an original file");
    NativeCareerStore store(target,{}, {CareerStorageLayout::Folders,{},{}});check(store.readCareer(id).state==CareerFileState::Unsupported,"Future career was discarded or treated as compatible");check(!fs::exists(target/L"hall-of-fame/original"),"Future Hall index became live");
    bool retainedHall=false,retainedSettings=false,retainedOrphan=false;for(const auto& e:fs::recursive_directory_iterator(target/L"migration-recovery"))if(e.is_regular_file()){
        retainedHall|=bytes(e.path())==hall;retainedSettings|=bytes(e.path())==CareerBytes({'f','u','t','u','r','e'});retainedOrphan|=e.path().filename()==L"keep.scr-ghost";}
    check(retainedHall&&retainedSettings&&retainedOrphan,"Unavailable source/recovery evidence was lost");
    const auto recovered=root/L"recoverable";FolderTransactions::copy(source,recovered);write(recovered/L"careers"/(id+".scr-career"),{'b','a','d'});const auto recoveryTarget=root/L"recovery-data";PlayerDataImport(recoveryTarget).import(playerImportSource(recovered));
    NativeCareerStore recovery(recoveryTarget,{}, {CareerStorageLayout::Folders,{},{}});check(recovery.readCareer(id).state==CareerFileState::Recovered,"A recoverable imported career lost its backup");
    const auto noSettings=root/L"no-settings";FolderTransactions::copy(source,noSettings);fs::remove(noSettings/L"settings/display.ini");PlayerDataImport(root/L"no-settings-data").import(playerImportSource(noSettings));check(loadPlayerSelections(root/L"no-settings-data/settings.ini").career==id,"Legacy selection depended on a settings document");
    // A category junction cannot turn a selected source into an external scan.
    const auto linked=root/L"linked";fs::create_directories(linked);const auto junction=linked/L"careers";
    const auto command=L"cmd /d /c mklink /J \""+fs::absolute(junction).make_preferred().wstring()+L"\" \""+fs::absolute(source/L"careers").make_preferred().wstring()+L"\"";
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};auto mutableCommand=command;
    check(CreateProcessW(nullptr,mutableCommand.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=0,"Cannot create junction fixture");WaitForSingleObject(pi.hProcess,10000);DWORD code{};GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);check(code==0,"Junction fixture failed");
    rejected([&]{PlayerDataImport(root/L"linked-data").import(playerImportSource(linked));},"Import followed an external category junction");fs::remove(junction);
    (void)pack;
}
void sourceRecovery(const fs::path& data,const fs::path& root,const std::string& id){
    for(bool collection:{false,true}){const auto source=root/(collection?L"collection-source":L"game-source");FolderTransactions::copy(data,source);
        fs::path career;for(const auto& e:fs::directory_iterator(source/L"careers"))if(e.is_directory()){career=e.path();break;}check(!career.empty(),"Source recovery omitted career fixture");
        FolderTransactions interrupted(source,[&](auto stage){if(stage==(collection?FolderTransactionStage::Published:FolderTransactionStage::Parked))throw std::runtime_error("Expected source interruption");});
        rejected([&]{if(!collection)interrupted.publish(career,{career},[&](const auto& stage){FolderTransactions::copy(career,stage);});
            else interrupted.publishCollection([&](const auto& stage){for(const auto* name:{L"assets",L"careers",L"multiplayer",L"hall-of-fame",L"migration-recovery",L"settings.ini",L"migration.scr-import"}){const auto from=source/name;if(!fs::exists(from))continue;if(fs::is_directory(from))FolderTransactions::copy(from,stage/name);else fs::copy_file(from,stage/name);}
                const auto head=stage/career.lexically_relative(source)/L"career.scr-career";const auto saved=career_files::readGeneration(head,1);CareerBytes body;auto info=decodeCareerInfo(saved.data,&body);info.lastPlayed=777;write(head,career_files::envelope(1,saved.generation,encodeCareerInfo(info,body)));});},"Source interruption did not occur");
        const auto before=snapshot(source);const auto target=root/(collection?L"collection-data":L"game-data");const auto report=PlayerDataImport(target).import(playerImportSource(source));check(report.careers==1&&report.multiplayer==1,"Import missed progress parked in a source transaction");check(before==snapshot(source),"Source recovery modified the original transaction");
        NativeCareerStore store(target,{}, {CareerStorageLayout::Folders,{},{}});const auto saved=store.readCareer(id);check(saved.state==CareerFileState::Valid,"Recovered source career was unavailable");if(collection)check(decodeCareerInfo(saved.data).lastPlayed==777,"Import used the old source head instead of its staged collection");
    }
}
std::wstring executable(){std::wstring value(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,value.data(),DWORD(value.size()));check(n&&n<value.size(),"Cannot find crash fixture");value.resize(n);return value;}
void crash(const fs::path& source,const fs::path& root,unsigned at,unsigned occurrence){
    const auto commandStart=L"\""+executable()+L"\" --interrupt \""+source.wstring()+L"\" \""+root.wstring()+L"\" "+std::to_wstring(at)+L" "+std::to_wstring(occurrence);auto command=commandStart;STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};const auto log=CreateFileW((root/L"child.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,0,nullptr);check(log!=INVALID_HANDLE_VALUE,"Cannot capture interrupted import output");startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdOutput=startup.hStdError=log;
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0,"Cannot launch interrupted import");const auto wait=WaitForSingleObject(process.hProcess,30000);if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,99);WaitForSingleObject(process.hProcess,5000);}DWORD code{};GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(log);if(wait!=WAIT_OBJECT_0||code!=37){const auto output=bytes(root/L"child.log");throw std::runtime_error("Import boundary "+std::to_string(at)+"/"+std::to_string(occurrence)+" exited "+std::to_string(code)+": "+std::string(output.begin(),output.end()));}++crashes;
}
void interruptions(const fs::path& source,const fs::path& root){
    const auto before=snapshot(source);
    for(unsigned at=0;at<4;++at)for(unsigned n=1;n<=(at==1?6:at==2?7:1);++n){const auto data=root/(std::to_string(at)+"-"+std::to_string(n));
        for(const auto* name:{L"assets",L"careers",L"multiplayer",L"hall-of-fame",L"migration-recovery"})write(data/name/L"sentinel.txt",{'o','l','d'});write(data/L"settings.ini",{'o','l','d'});
        crash(source,data,at,n);PlayerDataImport recovered(data);check(recovered.decided(),"Interrupted import did not recover before discovery");check(!FolderTransactions(data).recover(),"Import recovery was not idempotent");check(fs::exists(data/L"careers/sentinel.txt")&&fs::exists(data/L"multiplayer/sentinel.txt"),"Import recovery lost unrelated contents");}
    check(before==snapshot(source),"Process interruption changed original data");
}
void playerPortability(const fs::path& program,const fs::path& data,const fs::path& root){
    const auto install=root/L"install-a";fs::create_directories(install);FolderTransactions::copy(data,install/L"data");const auto player=install/L"Renamed Racer.exe";fs::copy_file(program,player);const auto cwd=root/L"another-working-directory";fs::create_directories(cwd);
    const auto run=[&](const fs::path& exe,const fs::path& folder){
        auto command=L"\""+exe.wstring()+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
        check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,cwd.c_str(),&startup,&process)!=0,"Cannot launch portable player");
        struct Lifetime {PROCESS_INFORMATION& p;~Lifetime(){if(WaitForSingleObject(p.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(p.hProcess,99);WaitForSingleObject(p.hProcess,5000);}CloseHandle(p.hThread);CloseHandle(p.hProcess);}} lifetime{process};
        HWND window{};for(unsigned n=0;n<1000&&!window;++n){Sleep(10);PlayerDataOwnership busy(folder/L"data");if(!busy.acquired())window=FindWindowW(busy.windowClassName().c_str(),nullptr);}
        check(window!=nullptr,"Normal launch did not own executable-relative data");HWND play{};for(unsigned n=0;n<1000;++n){play=GetDlgItem(window,LauncherPlay);if(play&&IsWindowEnabled(play))break;Sleep(10);}check(play&&IsWindowEnabled(play),"Moved player still required an ADF or legacy paths");
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(LauncherPlay,BN_CLICKED),reinterpret_cast<LPARAM>(play));
        for(unsigned n=0;n<1000&&(GetWindowLongPtrW(play,GWL_STYLE)&WS_VISIBLE);++n)Sleep(10);check(!(GetWindowLongPtrW(play,GWL_STYLE)&WS_VISIBLE),"Portable player failed to start native sessions");PostMessageW(window,WM_CLOSE,0,0);
        const auto wait=WaitForSingleObject(process.hProcess,10000);DWORD code{};GetExitCodeProcess(process.hProcess,&code);check(wait==WAIT_OBJECT_0&&code==0,"Portable player did not drain and close");
    };
    run(player,install);const auto moved=root/L"install-b";fs::rename(install,moved);const auto renamed=moved/L"My Racer.exe";fs::rename(moved/L"Renamed Racer.exe",renamed);run(renamed,moved);
    const auto copied=root/L"install-c";FolderTransactions::copy(moved,copied);const auto original=snapshot(moved/L"data/careers");
    {NativeCareerStore store(copied/L"data",{}, {CareerStorageLayout::Folders,{},{}});check(!store.list().empty()&&store.eraseEntry(store.list().front()).get().error.empty(),"Copied collection could not change independently");}
    check(snapshot(moved/L"data/careers")==original&&!fs::exists(cwd/L"data"),"Copied/moved player shared another root or used its working directory");
}
}
int wmain(int argc,wchar_t** argv)try{
    if(argc==6&&std::wstring(argv[1])==L"--interrupt"){const auto at=unsigned(std::stoul(argv[4])),occurrence=unsigned(std::stoul(argv[5]));unsigned seen=0;PlayerDataImport importer(argv[3],[&](auto stage){if(unsigned(stage)==at&&++seen==occurrence)ExitProcess(37);});importer.import(playerImportSource(argv[2]),{true,true,true});return 1;}
    check(argc==4,"Usage: player_data_import_tests asset-pack isolated-output player");const fs::path pack=argv[1],root=argv[2];const auto source=root/L"source";const auto id=fixture(pack,source);
    migration(pack,source,root/L"migrated",id);conflicts(pack,source,root/L"conflicts",id);failureAndChoice(pack,source,root/L"choices");unavailableFormats(pack,source,root/L"unavailable",id);sourceRecovery(root/L"migrated",root/L"source-recovery",id);interruptions(source,root/L"interruptions");playerPortability(argv[3],root/L"migrated",root/L"portability");
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"process_interruptions\":"<<crashes<<",\"all_pb_recovery_generations\":true,\"source_unchanged\":true,\"exact_checkpoint_memory_rng\":true,\"hall_no_new_attempts\":true,\"repeat_idempotent\":true,\"explicit_fresh_start\":true,\"real_player_move_rename_cwd\":true,\"independent_copy\":true,\"unsupported_preserved\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
