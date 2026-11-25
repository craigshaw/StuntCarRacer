#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "persistence/native_multiplayer_store.h"
#include "persistence/career_file_io.h"
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace scr::runtime;namespace fs=std::filesystem;
unsigned checks{},faults{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F action,const char* message){bool caught=false;try{action();}catch(const std::exception&){caught=true;}check(caught,message);}
MultiplayerBytes bytes(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void write(const fs::path& path,const MultiplayerBytes& data){std::string error;check(career_files::atomicWrite(path,data,error),"Cannot write storage fixture");}
std::map<fs::path,MultiplayerBytes> snapshot(const fs::path& path){std::map<fs::path,MultiplayerBytes> result;if(fs::exists(path))for(const auto& file:fs::recursive_directory_iterator(path))if(file.is_regular_file())result[file.path().lexically_relative(path)]=bytes(file.path());return result;}
MultiplayerBytes checkpoint(const NativeMemory& baseline,unsigned marker){
    NativeMultiplayerCheckpoint c;c.memory=std::make_unique<NativeMemory>(baseline);c.menu.page=NativeMenuPage::Main;auto& m=*c.memory;m[0x5eb76]=1;m[0x57c62]=0x10;m[0x1234]=std::uint8_t(marker);
    for(unsigned n=0;n<12;++n){m[0x1c9c2+n]=m[0x1ca0e+n]=std::uint8_t(n);m[0x1ca36+n]=10;std::fill_n(m.begin()+0x1ecab+16*n,12,'D');}
    return encodeNativeMultiplayerCheckpoint(baseline,c);
}
struct Library {
    NativeCareerStore io;NativeMultiplayerStore games;
    Library(const fs::path& root,const NativeMemory& baseline,FolderTransactionObserver observer={}):io(root,{}, {CareerStorageLayout::Folders,{},{}}),games(io,baseline,{},std::move(observer)){}
};
SavedGameEntry entry(const NativeMultiplayerStore& games,const std::string& id){for(const auto& item:games.list())if(item.id==id)return item;throw std::runtime_error("Fixture game missing");}
void storage(const NativeMemory& baseline,const fs::path& root){
    auto library=std::make_unique<Library>(root,baseline);NativeMultiplayerInfo info{NativeCareerStore::newId(),"CON<>:same",1};
    const auto career=NativeCareerInfo{NativeCareerStore::newId(),"CAREER",0,0,1};library->io.saveCareer(career,{1,2,3});check(library->io.flush(),"Cannot seed independent career");
    write(root/L"hall-of-fame/original/keep.txt",{1,2});write(root/L"hall-of-fame/enhanced/keep.txt",{3,4});const auto hall=snapshot(root/L"hall-of-fame"),careers=snapshot(root/L"careers");
    for(unsigned n=1;n<=3;++n){info.lastPlayed=n;library->games.save(info,checkpoint(baseline,n));check(library->io.flush(),"MP generation did not save");}
    auto first=entry(library->games,info.id);const auto head=first.folder/L"game.scr-multiplayer";
    check(first.folder.parent_path()==root/L"multiplayer"&&library->games.read(info.id).generation==3,"MP layout/generation is incorrect");
    for(unsigned n=0;n<3;++n){const auto saved=decodeNativeMultiplayerSave(baseline,bytes(career_files::generationPath(head,n)));check(saved.generation==3-n&&(*decodeNativeMultiplayerCheckpoint(baseline,saved.checkpoint).memory)[0x1234]==3-n,"Recovery generations lost their native state");}
    library->io.setLastUsed(career.id);library->games.setLastUsed(info.id);check(library->io.flush(),"Selections did not drain");
    check(library->io.lastUsed()==career.id&&library->games.lastUsed()==info.id&&!fs::exists(root/L".multiplayer-preference"),"Preferences were confused with a catalogue");
    library.reset();const auto renamed=root/L"multiplayer"/fs::u8path("renamed \xe6\x97\xa5\xe6\x9c\xac");fs::rename(first.folder,renamed);library=std::make_unique<Library>(root,baseline);first=entry(library->games,info.id);
    check(first.folder==renamed&&library->games.read(info.id).state==SavedGameFileState::Valid,"Renaming a game changed its identity");
    const auto renamedHead=renamed/L"game.scr-multiplayer";write(renamedHead,{1,2,3});check(library->games.read(info.id).state==SavedGameFileState::Recovered,"Damaged head did not recover");fs::remove(renamedHead);
    library.reset();library=std::make_unique<Library>(root,baseline);check(entry(library->games,info.id).state==SavedGameFileState::Recovered,"Missing head hid a game with recovery files");
    auto future=bytes(career_files::generationPath(renamedHead,1));future[4]=99;write(renamedHead,future);library.reset();library=std::make_unique<Library>(root,baseline);
    check(entry(library->games,info.id).state==SavedGameFileState::Unsupported,"Future head was not visible/unavailable");library->games.save(info,checkpoint(baseline,9));check(!library->io.flush()&&bytes(renamedHead)==future,"A future save was overwritten");
    fs::remove(renamedHead);library->io.retry();check(library->io.flush(),"MP retry did not retain the failed RAM candidate");
    check((*decodeNativeMultiplayerCheckpoint(baseline,decodeNativeMultiplayerSave(baseline,library->games.read(info.id).data).checkpoint).memory)[0x1234]==9,"Retry wrote stale progress");
    check(hall==snapshot(root/L"hall-of-fame")&&careers==snapshot(root/L"careers"),"MP storage changed Hall/career ownership");
    library.reset();fs::remove(root/L"settings.ini");library=std::make_unique<Library>(root,baseline);check(library->games.list().size()==1&&library->games.lastUsed().empty(),"Discovery needed settings/catalogue");
}
void copies(const NativeMemory& baseline,const fs::path& root){
    auto library=std::make_unique<Library>(root,baseline);NativeMultiplayerInfo info{NativeCareerStore::newId(),"DUPLICATE",3};
    for(unsigned n=1;n<=3;++n){library->games.save(info,checkpoint(baseline,n));check(library->io.flush(),"Cannot seed copy generations");}
    auto original=entry(library->games,info.id);library->games.setLastUsed(info.id);check(library->io.flush(),"Cannot seed MP selection");library.reset();
    const auto incoming=root/L"multiplayer/incoming";FolderTransactions::copy(original.folder,incoming);const auto before=snapshot(root/L"multiplayer");library=std::make_unique<Library>(root,baseline);
    check(library->games.list().size()==2&&library->games.read(info.id).state==SavedGameFileState::Conflict,"Duplicate identity was silently chosen");
    rejected([&]{library->games.save(info,checkpoint(baseline,50));},"Conflicted game accepted autosave");check(before==snapshot(root/L"multiplayer"),"Cancel/read/conflict rejection changed a source");
    auto incomingEntry=original;incomingEntry.folder=incoming;const auto kept=library->games.resolveCopy(incomingEntry,SavedGameCopyChoice::KeepBoth).get();check(kept.error.empty()&&kept.id!=info.id&&library->io.flush(),"Keep Both failed");
    const auto clone=entry(library->games,kept.id);check(library->games.list().size()==2&&library->games.lastUsed()==info.id,"Keep Both changed original selection");
    for(unsigned n=0;n<3;++n){const auto a=decodeNativeMultiplayerSave(baseline,bytes(career_files::generationPath(original.folder/L"game.scr-multiplayer",n))),b=decodeNativeMultiplayerSave(baseline,bytes(career_files::generationPath(clone.folder/L"game.scr-multiplayer",n)));
        check(a.generation==b.generation&&a.checkpoint==b.checkpoint&&b.info.id==kept.id,"Keep Both changed native state or missed a recovery owner");}
    const auto recipient=root.parent_path()/L"recipient";FolderTransactions::copy(clone.folder,recipient/L"multiplayer/anything");{Library copy(recipient,baseline);check(copy.games.list().size()==1&&copy.games.read(kept.id).state==SavedGameFileState::Valid,"Copied game needed source settings/assets paths");}
    library.reset();const auto replacement=root/L"multiplayer/selected";FolderTransactions::copy(original.folder,replacement);auto selected=decodeNativeMultiplayerSave(baseline,bytes(replacement/L"game.scr-multiplayer"));selected.info.lastPlayed=0;selected.checkpoint=checkpoint(baseline,77);write(replacement/L"game.scr-multiplayer",encodeNativeMultiplayerSave(baseline,selected));
    library=std::make_unique<Library>(root,baseline);auto replaceEntry=original;replaceEntry.folder=replacement;const auto replaced=library->games.resolveCopy(replaceEntry,SavedGameCopyChoice::Replace).get();check(replaced.error.empty()&&library->io.flush(),"Replace failed");
    check((*decodeNativeMultiplayerCheckpoint(baseline,decodeNativeMultiplayerSave(baseline,library->games.read(info.id).data).checkpoint).memory)[0x1234]==77&&fs::exists(replacement/L"recovery/replaced"),"Replace guessed by date or discarded previous data");
    library.reset();const auto unsupported=root/L"multiplayer/unsupported";FolderTransactions::copy(replacement,unsupported);auto bad=bytes(unsupported/L"game.scr-multiplayer.bak2");bad[4]=99;write(unsupported/L"game.scr-multiplayer.bak2",bad);
    const auto sources=snapshot(root/L"multiplayer");library=std::make_unique<Library>(root,baseline);replaceEntry.folder=unsupported;const auto failed=library->games.resolveCopy(replaceEntry,SavedGameCopyChoice::KeepBoth).get();check(!failed.error.empty()&&sources==snapshot(root/L"multiplayer"),"Unsupported recovery was rekeyed or sources changed");
}
void deletionAndFaults(const NativeMemory& baseline,const fs::path& root){
    {Library library(root/L"delete",baseline);const NativeMultiplayerInfo info{NativeCareerStore::newId(),"DELETE",1};library.games.save(info,checkpoint(baseline,1));check(library.io.flush(),"Cannot seed deletion");const auto game=entry(library.games,info.id);
        fs::create_directory(game.folder/L"game.scr-multiplayer.tmp");library.games.save(info,checkpoint(baseline,2));check(!library.io.flush(),"Write failure injection was hidden");
        const auto held=CreateFileW(game.folder.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);check(held!=INVALID_HANDLE_VALUE,"Cannot pin deletion fixture");
        const auto failure=library.games.erase(game).get();check(!failure.error.empty()&&fs::exists(game.folder),"Blocked deletion lost the last good game");CloseHandle(held);
        check(library.games.erase(game).get().error.empty(),"Prepared deletion did not recover idempotently");library.io.retry();check(library.io.flush()&&!fs::exists(game.folder)&&library.games.list().empty(),"A queued retry resurrected a deleted game");}
    for(const auto at:{FolderTransactionStage::Prepared,FolderTransactionStage::Published,FolderTransactionStage::Committed}){
        const auto folder=root/("create-"+std::to_string(unsigned(at)));const NativeMultiplayerInfo info{NativeCareerStore::newId(),"INTERRUPTED",1};
        {Library library(folder,baseline,[at](auto stage){if(stage==at)throw std::runtime_error("Injected publication interruption");});library.games.save(info,checkpoint(baseline,3));check(!library.io.flush(),"Publication interruption was hidden");}
        {Library recovered(folder,baseline);check(recovered.games.read(info.id).state==SavedGameFileState::Valid,"Creation did not recover before discovery");}++faults;
    }
}
}
int main(int argc,char** argv)try{
    check(argc==2,"Usage: native_multiplayer_store_tests isolated-output");const auto root=fs::absolute(argv[1]);const auto baseline=std::make_unique<NativeMemory>();
    storage(*baseline,root/L"storage");copies(*baseline,root/L"copies");deletionAndFaults(*baseline,root/L"faults");
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"publication_faults\":"<<faults<<",\"all_recovery_generations_rekeyed\":true,\"shared_actor\":true,\"career_hall_isolation\":true,\"failed_delete_retry_no_resurrection\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
