#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "persistence/native_career_store.h"
#include "persistence/ghost_lap_codec.h"
#include "ghost_lap_fixture.h"
#include <fstream>
#include <iostream>

namespace {
using namespace scr::runtime;using namespace scr::state::checkpoint;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void corrupt(const std::filesystem::path& path){std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<"damaged";}
Bytes read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
}
int main(int argc,char** argv)try{
    require(argc==2,"Usage: native_ghost_store_tests output");const std::filesystem::path root=argv[1];NativeCareerStore store(root);
    const auto id=NativeCareerStore::newId();auto lap=scr::test::ghost::lap(id);const auto key=lap.context.key;
    const auto file=root/"ghosts"/id/"0-normal.scr-ghost";
    for(unsigned n=0;n<4;++n){lap.completedTime.subseconds=std::uint8_t(0x23+n);lap.hundredths=ghostLapHundredths(lap.completedTime);store.saveGhost(lap);require(store.flush(),"Ghost replacement failed");}
    require(store.readGhost(key,1234).generation==4,"Ghost generations did not advance");
    corrupt(file);auto saved=store.readGhost(key,1234);require(saved.state==CareerFileState::Recovered&&saved.generation==3,"Newest valid ghost recovery was not selected");
    corrupt(file.string()+".bak1");saved=store.readGhost(key,1234);require(saved.state==CareerFileState::Recovered&&saved.generation==2,"Second ghost recovery was not selected");
    store.saveGhost(lap);require(store.flush()&&store.readGhost(key,1234).state==CareerFileState::Valid,"Recovered ghost could not be replaced safely");
    unsigned preserved=0;for(const auto& entry:std::filesystem::directory_iterator(file.parent_path()))preserved+=entry.path().string().find(".damaged-")!=std::string::npos;
    require(preserved==2,"Unreadable ghost files were discarded");
    const auto locked=CreateFileW((file.wstring()+L".tmp").c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(locked!=INVALID_HANDLE_VALUE,"Cannot inject ghost write failure");store.saveGhost(lap);require(!store.flush()&&store.ghostFailed(),"Ghost write failure was not reported");
    require(store.readGhost(key,1234).state==CareerFileState::Valid,"Failed ghost write damaged the last good file");CloseHandle(locked);store.retry();require(store.flush(),"Ghost write retry failed");
    const auto raw=read(file);require(raw.size()>40,"Ghost envelope missing");
    auto future=encodeGhostLap(lap);future[4]=2;Writer envelope;envelope.u32(0x43524353);envelope.u32(1);envelope.u32(4);envelope.u32(unsigned(future.size()));envelope.u64(99);envelope.u64(hash(future.data(),future.size()));envelope.bytes(future.data(),future.size());
    {std::ofstream out(file,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(envelope.data.data()),std::streamsize(envelope.data.size()));}
    require(store.readGhost(key,1234).state==CareerFileState::Unsupported,"Future ghost payload was not preserved as unsupported");
    const auto unknown=read(file);store.saveGhost(lap);require(!store.flush()&&read(file)==unknown,"Unsupported ghost was overwritten");
    require(store.clearGhost(key),"Explicit ghost clear failed");store.retry();require(store.flush()&&store.readGhost(key,1234).state==CareerFileState::Missing,"Retry resurrected an explicitly cleared ghost");
    store.saveGhost(lap);require(store.clearGhost(key),"Clear did not drain an in-flight ghost replacement");store.retry();require(store.flush()&&store.readGhost(key,1234).state==CareerFileState::Missing,"Pending write restored a cleared target");
    const auto otherId=NativeCareerStore::newId();auto other=scr::test::ghost::lap(otherId,5,true);store.saveGhost(other);store.saveGhost(lap);
    store.saveCareer({id,"TEST",0,0,1},{1});require(store.flush()&&store.removeCareer(id),"Career deletion did not remove its ghosts");
    require(store.readGhost(key,1234).state==CareerFileState::Missing&&store.readGhost(other.context.key,1234).state==CareerFileState::Valid,"Ghost deletion crossed career boundaries");
    require(store.readGhost(other.context.key,5678).state==CareerFileState::Unsupported,"Track content compatibility was ignored");
    std::cout<<"Ghost storage recovery, failure, deletion and unsupported-format checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
