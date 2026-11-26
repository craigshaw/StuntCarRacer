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
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
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
    Library(const fs::path& root,const NativeMemory& baseline):io(root,{}, {}),games(io,baseline){}
};
SavedGameEntry entry(const NativeMultiplayerStore& games,const std::string& id){for(const auto& item:games.list())if(item.id==id)return item;throw std::runtime_error("Fixture game missing");}
void storage(const NativeMemory& baseline,const fs::path& root){
    auto library=std::make_unique<Library>(root,baseline);NativeMultiplayerInfo info{NativeCareerStore::newId(),"FRIDAY",1};fs::path folder;
    for(unsigned n=1;n<=3;++n){info.lastPlayed=n;folder=library->games.save(info,checkpoint(baseline,n),folder);check(library->io.flush(),"MP generation did not save");}
    auto first=entry(library->games,info.id);const auto head=folder/L"game.scr-multiplayer";
    check(library->games.read(first.key()).generation==3,"MP generation differs");
    for(unsigned n=0;n<3;++n){const auto saved=decodeNativeMultiplayerSave(baseline,bytes(career_files::generationPath(head,n)));check(saved.generation==3-n,"Recovery generations changed");}
    library->games.setLastUsed(first.key());check(library->io.flush(),"MP selection did not save");library.reset();
    const auto renamed=root/L"multiplayer/renamed-\u8eca";fs::rename(folder,renamed);const auto copy=root/L"multiplayer/copy";fs::copy(renamed,copy,fs::copy_options::recursive);const auto original=snapshot(renamed);
    library=std::make_unique<Library>(root,baseline);check(library->games.list().size()==2,"Copied folders were not independently discovered");
    for(const auto& game:library->games.list())check(game.id==info.id&&game.state==SavedGameFileState::Valid,"Copies were rekeyed or treated as a conflict");
    library->games.save(info,checkpoint(baseline,77),copy);library->games.setLastUsed("copy");check(library->io.flush(),"Copied game did not save");
    check(snapshot(renamed)==original&&decodeNativeMultiplayerSave(baseline,library->games.read("copy").data).info.id==info.id,"Selected copy rewrote its sibling or identity");
    library.reset();library=std::make_unique<Library>(root,baseline);check(library->games.lastUsed()=="copy","Continue forgot selected folder");
    const auto copiedHead=copy/L"game.scr-multiplayer";write(copiedHead,{1,2,3});check(library->games.read("copy").state==SavedGameFileState::Recovered,"Damaged head did not recover");
    auto future=bytes(renamed/L"game.scr-multiplayer");future[4]=99;write(renamed/L"game.scr-multiplayer",future);
    library->games.save(info,checkpoint(baseline,9),renamed);check(!library->io.flush()&&bytes(renamed/L"game.scr-multiplayer")==future,"Future save was overwritten");
    fs::remove(renamed/L"game.scr-multiplayer");library->io.retry();check(library->io.flush(),"Retry lost retained RAM progress");
    const auto repaired=decodeNativeMultiplayerSave(baseline,library->games.read(renamed.filename().u8string()).data);check((*decodeNativeMultiplayerCheckpoint(baseline,repaired.checkpoint).memory)[0x1234]==9,"Retry wrote stale progress");
    fs::create_directory(copiedHead.wstring()+L".tmp");library->games.save(info,checkpoint(baseline,55),copy);check(!library->io.flush(),"Write failure was hidden");fs::remove(copiedHead.wstring()+L".tmp");library->io.retry();check(library->io.flush(),"Blocked save did not retry");
    const auto copied=snapshot(copy);library.reset();const auto recipient=root.parent_path()/L"recipient";fs::create_directories(recipient/L"multiplayer");fs::copy(copy,recipient/L"multiplayer/anything",fs::copy_options::recursive);Library moved(recipient,baseline);
    check(moved.games.list().size()==1&&moved.games.read("anything").state==SavedGameFileState::Valid&&snapshot(copy)==copied,"Moved game requires original settings or rewrites its source");
    check(!fs::exists(root/L".transactions")&&!fs::exists(root/L"migration.scr-import"),"Save created conversion machinery");
}
}
int main(int argc,char** argv)try{
    check(argc==2,"Usage: native_multiplayer_store_tests isolated-output");const auto baseline=std::make_unique<NativeMemory>();storage(*baseline,fs::absolute(argv[1])/L"storage");
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"independent_copies\":true,\"bounded_recovery\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
