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
std::unique_ptr<NativeCareerStore> open(const fs::path& root){return std::make_unique<NativeCareerStore>(root,NativeCareerStore::Validator{},CareerStorageOptions{CareerStorageLayout::Folders,{}});}
NativeCareerInfo seed(NativeCareerStore& store,const std::string& name="Craig",bool recordings=true){
    NativeCareerInfo info{NativeCareerStore::newId(),name,0,0,1};
    for(unsigned generation=1;generation<=3;++generation){info.lastPlayed=generation;store.saveCareer(info,{2,std::uint8_t(generation)});
        if(recordings)for(unsigned track=0;track<8;++track)for(bool super:{false,true}){
            auto lap=scr::test::ghost::lap(info.id,track,super);lap.completedTime.subseconds=std::uint8_t(0x23-generation);lap.hundredths=ghostLapHundredths(lap.completedTime);store.saveGhost(lap);
            auto best=fixture::best(120000-1000*generation,true,track);best.key.career=info.id;best.key.superLeague=super;store.saveEnhancedBest(best);
        }
        check(store.flush(),"Self-contained career/PBs did not save");
    }
    store.setLastUsed(store.list().front().key());check(store.flush(),"Last-used preference did not save");
    for(const auto& entry:store.list())if(entry.id==info.id)return entry;throw std::runtime_error("New career was not discovered");
}
NativeCareerInfo entry(const NativeCareerStore& store,const fs::path& folder){for(const auto& value:store.list())if(value.folder==folder)return value;throw std::runtime_error("Copied folder was not discovered");}
void discovery(const fs::path& root){
    auto store=open(root);const auto first=seed(*store,"A/B:C.",true);store.reset();
    const auto original=snapshot(first.folder);const auto renamed=root/L"careers/renamed-\u8eca";fs::rename(first.folder,renamed);
    fs::create_directories(root/L"careers/empty");write(root/L"careers/empty/career.scr-career.tmp",{7});
    const auto copy=root/L"careers/copy";fs::copy(renamed,copy,fs::copy_options::recursive);
    fs::create_directories(copy/L"recovery/nested");write(copy/L"recovery/nested/career.scr-career",bytes(copy/L"career.scr-career"));
    store=open(root);check(store->list().size()==2,"Convention discovery requires a catalogue or includes incomplete/nested saves");
    for(const auto& item:store->list())check(item.id==first.id&&item.state==CareerFileState::Valid,"Copies were marked as conflicts or rekeyed");
    check(snapshot(renamed)==original,"Discovery changed a renamed source");
    auto chosen=entry(*store,copy);store->selectCareer(chosen);
    const GhostLapKey key{first.id,0,false};auto best=fixture::best(100000,true,0);best.key.career=first.id;
    check(store->readGhost(key,scr::test::ghost::lap(first.id,0,false).context.trackContentIdentity).state==CareerFileState::Valid,"Copied PB ownership changed");
    chosen.lastPlayed=99;store->saveCareer(chosen,{9,9});store->saveEnhancedBest(best);store->setLastUsed(chosen.key());check(store->flush(),"Selected copy did not save");
    check(snapshot(renamed)==original&&decodeCareerInfo(store->readCareer(chosen.key()).data).id==first.id,"Saving a copy changed its sibling or native ID");
    const auto catalog=best.catalog;check(decodeEnhancedBest(store->readEnhancedBest(best.key,catalog).data).time.elapsedMicroseconds==100000,"Copied Enhanced PB did not save");
    store->selectCareer(entry(*store,renamed));check(decodeEnhancedBest(store->readEnhancedBest(best.key,catalog).data).time.elapsedMicroseconds==117000,"PBs leaked between matching internal IDs");
    store.reset();store=open(root);check(store->lastUsed()==chosen.key(),"Continue did not remember the selected folder");
    fs::remove(copy/L"career.scr-career");store.reset();store=open(root);check(store->readCareer(chosen.key()).state==CareerFileState::Recovered,"Missing head did not use local backup");
    auto future=bytes(renamed/L"career.scr-career");future[4]=99;write(renamed/L"career.scr-career",future);store.reset();store=open(root);
    check(entry(*store,renamed).state==CareerFileState::Unsupported,"Unsupported save was hidden");store->saveCareer(entry(*store,renamed),{8});check(!store->flush()&&bytes(renamed/L"career.scr-career")==future,"Unsupported save was overwritten");
    check(!fs::exists(root/L".transactions")&&!fs::exists(root/L"migration.scr-import")&&!fs::exists(root/L"last.scr-index"),"Discovery created import/catalogue machinery");
    const auto recipient=root.parent_path()/L"recipient";fs::create_directories(recipient/L"careers");fs::copy(copy,recipient/L"careers/anything",fs::copy_options::recursive);
    auto moved=open(recipient);check(moved->list().size()==1&&moved->readCareer("anything").state==CareerFileState::Recovered,"Folder copy needs source settings or installation paths");
}
void preferences(const fs::path& root){
    const auto path=root/L"settings.ini";const std::string first="Driver=one-\xe8\xbb\x8a",second="Friday night";
    NativeDisplayPreferences display;display.fullscreen=true;check(saveDisplayPreferences(path,display),"Cannot save display preferences");
    check(savePlayerSelection(path,first)&&savePlayerSelection(path,second,true),"Cannot save folder selections");display.interpolate=true;
    check(saveDisplayPreferences(path,display)&&loadPlayerSelections(path).career==first&&loadPlayerSelections(path).multiplayer==second,"Display save erased folder selections");
    check(savePlayerSelection(path,"Other")&&loadDisplayPreferences(path)==display,"Selection save erased display choices");
    check(!savePlayerSelection(path,"../outside")&&!savePlayerSelection(path,"folder\\child"),"Selection escaped the conventional category");
}
}
int main(int argc,char** argv)try{
    check(argc==2,"Usage: career_folder_tests isolated-output");const auto root=fs::absolute(argv[1]);discovery(root/L"discovery");preferences(root/L"preferences");
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"independent_copies\":true,\"personal_best_isolation\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
