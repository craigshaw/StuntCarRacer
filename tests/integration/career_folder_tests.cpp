#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "persistence/native_career_store.h"
#include "persistence/career_file_io.h"
#include "persistence/ghost_lap_codec.h"
#include "persistence/player_preferences.h"
#include "launcher/native_display_settings.h"
#include "ghost_lap_fixture.h"
#include "enhanced_record_fixture.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

namespace {
using namespace scr::runtime;namespace fs=std::filesystem;namespace fixture=scr::test::enhanced;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
CareerBytes bytes(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void write(const fs::path& path,const CareerBytes& data){std::string error;check(career_files::atomicWrite(path,data,error),"Cannot write synthetic fixture");}
std::map<fs::path,CareerBytes> snapshot(const fs::path& root){std::map<fs::path,CareerBytes> result;if(fs::exists(root))for(const auto& e:fs::recursive_directory_iterator(root))if(e.is_regular_file())result[e.path().lexically_relative(root)]=bytes(e.path());return result;}
std::unique_ptr<NativeCareerStore> open(const fs::path& root){return std::make_unique<NativeCareerStore>(root,NativeCareerStore::Validator{},CareerStorageOptions{CareerStorageLayout::Folders,{},{}});}
NativeCareerInfo seed(NativeCareerStore& store,const std::string& name="Craig",bool recordings=true){
    NativeCareerInfo info{NativeCareerStore::newId(),name,0,0,1};
    for(unsigned generation=1;generation<=3;++generation){info.lastPlayed=generation;store.saveCareer(info,{2,std::uint8_t(generation)});
        if(recordings)for(unsigned track=0;track<8;++track)for(bool super:{false,true}){
            auto lap=scr::test::ghost::lap(info.id,track,super);lap.completedTime.subseconds=std::uint8_t(0x23-generation);lap.hundredths=ghostLapHundredths(lap.completedTime);store.saveGhost(lap);
            auto best=fixture::best(120000-1000*generation,true,track);best.key.career=info.id;best.key.superLeague=super;store.saveEnhancedBest(best);
        }
        check(store.flush(),"Self-contained career/PBs did not save");
    }
    store.setLastUsed(info.id);check(store.flush(),"Last-used preference did not save");
    for(const auto& entry:store.list())if(entry.id==info.id)return entry;throw std::runtime_error("New career was not discovered");
}
NativeCareerInfo entry(const NativeCareerStore& store,const fs::path& folder){for(const auto& value:store.list())if(value.folder==folder)return value;throw std::runtime_error("Copied folder was not discovered");}
void discovery(const fs::path& root){
    auto store=open(root);auto first=seed(*store,"A/B:C.",false);auto second=seed(*store,"A/B:C.",false);
    check(first.folder!=second.folder&&first.folder.filename().wstring().find_first_of(L"/:*")==std::wstring::npos,"Folder labels were not safe/independent");
    check(!fs::exists(root/L"last.scr-index")&&loadPlayerSelections(root/L"settings.ini").career==second.id,"Discovery still needs a central index");
    const auto original=bytes(first.folder/L"career.scr-career");store.reset();
    const auto renamed=root/L"careers/renamed-\u8eca";fs::rename(first.folder,renamed);
    fs::create_directories(root/L"careers/empty");write(root/L"careers/empty/career.scr-career.tmp",{7});
    fs::create_directories(renamed/L"recovery/nested");write(renamed/L"recovery/nested/career.scr-career",original);
    store=open(root);check(store->list().size()==2&&entry(*store,renamed).id==first.id,"Renamed folder or nested recovery changed discovery");
    check(store->readCareer(first.id).data==career_files::readGeneration(renamed/L"career.scr-career",1).data,"Renaming changed checkpoint bytes");
    store.reset();fs::remove(renamed/L"career.scr-career");store=open(root);
    check(store->readCareer(first.id).state==CareerFileState::Recovered&&entry(*store,renamed).state==CareerFileState::Recovered,"Missing head hid a recoverable folder");
    store.reset();write(renamed/L"career.scr-career.bak1",{1});write(renamed/L"career.scr-career.bak2",{2});
    store=open(root);check(store->list().size()==2&&entry(*store,renamed).state==CareerFileState::Corrupt,"Damaged career disappeared from browsing");
    check(store->removeEntry(entry(*store,renamed))&&!fs::exists(renamed),"Damaged folder could not be deleted by location");
    store.reset();auto future=bytes(second.folder/L"career.scr-career");future[4]=99;write(second.folder/L"career.scr-career",future);store=open(root);
    check(entry(*store,second.folder).state==CareerFileState::Unsupported,"Unsupported career was silently dropped");
    store->saveCareer(second,{2,9});check(!store->flush()&&bytes(second.folder/L"career.scr-career")==future,"Unsupported career was overwritten");
}
void keepBoth(const fs::path& root){
    auto store=open(root);const auto first=seed(*store);const auto original=store->readCareer(first.id).data;store.reset();
    const auto incoming=root/L"careers/incoming";FolderTransactions::copy(first.folder,incoming);
    const auto hall=root/L"hall-of-fame/original/sentinel";write(hall,{9,8,7});const auto hallBefore=snapshot(root/L"hall-of-fame");
    store=open(root);check(store->list().size()==2&&store->readCareer(first.id).state==CareerFileState::Conflict,"Duplicate identity was loadable");
    const auto before=snapshot(root/L"careers");bool rejected=false;try{store->saveCareer(first,{2,8});}catch(const std::exception&){rejected=true;}
    check(rejected&&before==snapshot(root/L"careers"),"Conflicted identity accepted an autosave");
    const auto result=store->resolveCopy(entry(*store,incoming),CareerCopyChoice::KeepBoth).get();check(result.error.empty()&&result.id!=first.id&&store->flush(),"Keep Both failed");
    check(store->list().size()==2&&store->lastUsed()==first.id&&!fs::exists(incoming),"Keep Both lost a version or changed last-used identity");
    CareerBytes checkpoint,previous;auto info=decodeCareerInfo(store->readCareer(result.id).data,&checkpoint);(void)decodeCareerInfo(original,&previous);
    check(info.id==result.id&&checkpoint==previous,"Keep Both changed progression bytes");
    NativeCareerInfo clone;for(const auto& value:store->list())if(value.id==result.id)clone=value;
    for(unsigned track=0;track<8;++track)for(bool super:{false,true}){
        const auto stem=std::to_string(track)+(super?"-super":"-normal");
        for(unsigned generation=0;generation<3;++generation){
            const auto oldGhost=career_files::readGeneration(career_files::generationPath(first.folder/L"personal-bests/original"/(stem+".scr-ghost"),generation),4);
            const auto newGhost=career_files::readGeneration(career_files::generationPath(clone.folder/L"personal-bests/original"/(stem+".scr-ghost"),generation),4);
            check(newGhost.state==CareerFileState::Valid&&newGhost.generation==oldGhost.generation,"Original recovery generation lost during rekey");
            auto lap=decodeGhostLap(newGhost.data);check(lap.context.key.career==result.id,"Ghost retained the old owner");lap.context.key.career=first.id;
            check(encodeGhostLap(lap)==oldGhost.data,"Original ghost movement/timing changed during rekey");
            const auto oldBest=career_files::readGeneration(career_files::generationPath(first.folder/L"personal-bests/enhanced"/(stem+".scr-ebest"),generation),5);
            const auto newBest=career_files::readGeneration(career_files::generationPath(clone.folder/L"personal-bests/enhanced"/(stem+".scr-ebest"),generation),5);
            check(newBest.state==CareerFileState::Valid&&newBest.generation==oldBest.generation,"Enhanced recovery generation lost during rekey");
            auto best=decodeEnhancedBest(newBest.data);check(best.key.career==result.id,"Enhanced PB retained the old owner");best.key.career=first.id;
            check(encodeEnhancedBest(best)==oldBest.data,"Enhanced recording/time changed during rekey");
        }
    }
    check(hallBefore==snapshot(root/L"hall-of-fame"),"Keep Both changed the shared Hall");
    const auto freshRoot=root.parent_path()/L"recipient";FolderTransactions::copy(clone.folder,freshRoot/L"careers/custom-name");
    auto recipient=open(freshRoot);check(recipient->list().size()==1&&recipient->lastUsed().empty()&&recipient->readCareer(result.id).state==CareerFileState::Valid,"A copied career needed its source catalogue/settings");
    check(recipient->readGhost({result.id,5,true},1234).state==CareerFileState::Valid&&recipient->readEnhancedBest({result.id,5,true},fixture::catalog).state==CareerFileState::Valid,"Copied Draw Bridge/Super targets were unavailable");
    check(store->clearGhost({result.id,0,false})&&store->readEnhancedBest({result.id,0,false},fixture::catalog).state==CareerFileState::Valid,"Clearing a category crossed PB ownership");
    check(store->removeCareer(result.id)&&store->readCareer(result.id).state==CareerFileState::Missing,"Deleted folder recovered itself");
    store->retry();check(store->flush()&&!fs::exists(clone.folder)&&hallBefore==snapshot(root/L"hall-of-fame"),"Pending jobs resurrected a deleted folder or changed Hall");
}
void replaceAndUnsupported(const fs::path& root){
    auto store=open(root);const auto first=seed(*store,"SAME",false);store.reset();const auto incoming=root/L"careers/incoming";
    FolderTransactions::copy(first.folder,incoming);CareerBytes checkpoint;auto chosen=decodeCareerInfo(career_files::readGeneration(incoming/L"career.scr-career",1).data,&checkpoint);
    checkpoint.back()=77;chosen.lastPlayed=0;write(incoming/L"career.scr-career",career_files::envelope(1,8,encodeCareerInfo(chosen,checkpoint)));
    store=open(root);const auto result=store->resolveCopy(entry(*store,incoming),CareerCopyChoice::Replace).get();check(result.error.empty()&&store->flush(),"Replace failed");
    CareerBytes selected;(void)decodeCareerInfo(store->readCareer(first.id).data,&selected);
    check(store->list().size()==1&&selected.back()==77&&!fs::exists(first.folder),"Replace chose by date or merged divergent progression");
    bool archived=false;for(const auto& file:fs::recursive_directory_iterator(incoming/L"recovery/replaced"))if(file.path().filename()==L"career.scr-career"){
        CareerBytes old;(void)decodeCareerInfo(career_files::readGeneration(file.path(),1).data,&old);archived|=old.back()==3;}
    check(archived,"Replace discarded the previous complete folder");
    store.reset();const auto duplicate=root/L"careers/duplicate";FolderTransactions::copy(incoming,duplicate);
    const auto future=duplicate/L"personal-bests/enhanced/0-normal.scr-ebest.bak2";
    auto pb=fixture::best();pb.key.career=first.id;auto unsupported=career_files::envelope(5,1,encodeEnhancedBest(pb));unsupported[4]=99;write(future,unsupported);
    const auto before=snapshot(root/L"careers");store=open(root);
    const auto failed=store->resolveCopy(entry(*store,duplicate),CareerCopyChoice::KeepBoth).get();check(!failed.error.empty()&&store->flush(),"Unsupported recovery was rekeyed");
    check(before==snapshot(root/L"careers")&&store->list().size()==2,"Failed rekey changed either source or exposed staging");
}
void preferences(const fs::path& root){
    const auto path=root/L"settings.ini";const auto first=NativeCareerStore::newId(),second=NativeCareerStore::newId();
    NativeDisplayPreferences initial;initial.fullscreen=true;check(saveDisplayPreferences(path,initial),"Cannot seed display preferences");
    check(savePlayerSelection(path,first)&&savePlayerSelection(path,second,true),"Cannot save game selections");
    auto delayed=initial;delayed.interpolate=true;check(saveDisplayPreferences(path,delayed),"Delayed display save failed");
    check(loadPlayerSelections(path).career==first&&loadPlayerSelections(path).multiplayer==second,"Display save erased game selections");
    const auto next=NativeCareerStore::newId();check(savePlayerSelection(path,next)&&loadDisplayPreferences(path)==delayed,"Selection save erased display choices");
    std::atomic<bool> success{true};std::thread selection([&]{for(unsigned n=0;n<30;++n)if(!savePlayerSelection(path,first))success=false;});
    std::thread display([&]{for(unsigned n=0;n<30;++n)if(!saveDisplayPreferences(path,delayed))success=false;});selection.join();display.join();
    check(success&&loadPlayerSelections(path).career==first&&loadPlayerSelections(path).multiplayer==second&&loadDisplayPreferences(path)==delayed,"Concurrent preference writers lost fields");
    const auto before=bytes(path);fs::create_directory(path.wstring()+L".tmp");check(!savePlayerSelection(path,next)&&bytes(path)==before,"Failed preference write damaged the previous document");fs::remove(path.wstring()+L".tmp");
}
}
int main(int argc,char** argv)try{
    check(argc==2,"Usage: career_folder_tests isolated-output");const auto root=fs::absolute(argv[1]);
    discovery(root/L"discovery");keepBoth(root/L"copies");replaceAndUnsupported(root/L"replacement");preferences(root/L"preferences");
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"all_tracks_leagues\":true,\"recovery_generations_rekeyed\":true,\"hall_isolation\":true,\"coordinated_preferences\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
