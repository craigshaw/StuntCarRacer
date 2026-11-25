#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "persistence/native_career_store.h"
#include "persistence/ghost_lap_codec.h"
#include "persistence/career_file_io.h"
#include "persistence/windows_file_path.h"
#include "persistence/career_folders.h"
#include "persistence/player_preferences.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <set>
#include <limits>

namespace scr::runtime {
namespace {
using namespace state::checkpoint;
constexpr std::uint32_t magic=0x43524353,version=1;
constexpr std::size_t maximumFile=4*1024*1024;
static_assert(enhancedBestMaximumBytes+32<=maximumFile,"Enhanced PB must fit the durable envelope");
bool validId(const std::string& id){return id.size()==32&&std::all_of(id.begin(),id.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::filesystem::path generationPath(const std::filesystem::path& path,unsigned n){return n?std::filesystem::path(path.wstring()+L".bak"+std::to_wstring(n)):path;}
CareerRead readOne(const std::filesystem::path& path,unsigned kind){
    std::error_code error;if(!std::filesystem::exists(path,error))return {error?CareerFileState::Corrupt:CareerFileState::Missing};
    const auto size=std::filesystem::file_size(path,error);if(error||size<32||size>maximumFile)return {CareerFileState::Corrupt};
    std::ifstream file(windowsFilePath(path),std::ios::binary);Bytes bytes(std::size_t(size),0);file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));
    if(!file)return {CareerFileState::Corrupt};
    try{
        Reader r{bytes};if(r.u32()!=magic)return {CareerFileState::Corrupt};
        const auto savedVersion=r.u32();if(savedVersion!=version)return {CareerFileState::Unsupported};
        if(r.u32()!=kind)return {CareerFileState::Corrupt};
        const auto length=r.u32();const auto sequence=r.u64(),checksum=r.u64();
        if(kind==5&&!sequence)return {CareerFileState::Corrupt};
        if(length!=bytes.size()-r.position||checksum!=hash(bytes.data()+r.position,length))return {CareerFileState::Corrupt};
        Bytes payload(bytes.begin()+r.position,bytes.end());
        if(kind==1)(void)decodeCareerInfo(payload);
        if(kind==2){if(payload.size()!=512)return {CareerFileState::Corrupt};NativeHallBanks hall{};std::copy(payload.begin(),payload.end(),hall.begin());if(!validNativeHall(hall))return {CareerFileState::Corrupt};}
        if(kind==3){Reader index{payload};const auto id=index.text(32);index.end();if(!id.empty()&&!validId(id))return {CareerFileState::Corrupt};}
        if(kind==4)(void)decodeGhostLap(payload);
        if(kind==5)(void)decodeEnhancedBest(payload);
        return {CareerFileState::Valid,std::move(payload),sequence};
    }catch(const UnsupportedGhostLap&){return {CareerFileState::Unsupported};}catch(const UnsupportedEnhancedRecords&){return {CareerFileState::Unsupported};}catch(...){return {CareerFileState::Corrupt};}
}
bool atomicWrite(const std::filesystem::path& path,const Bytes& bytes,std::string& error){
    std::error_code code;std::filesystem::create_directories(path.parent_path(),code);
    if(code){error="Cannot create save folder: "+code.message();return false;}
    const auto temporary=std::filesystem::path(path.wstring()+L".tmp");
    const auto handle=CreateFileW(windowsFilePath(temporary).c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE){error="Cannot write save (Windows error "+std::to_string(GetLastError())+")";return false;}
    DWORD count{};bool okay=WriteFile(handle,bytes.data(),DWORD(bytes.size()),&count,nullptr)&&count==bytes.size()&&FlushFileBuffers(handle);
    auto failure=okay?ERROR_SUCCESS:GetLastError();CloseHandle(handle);
    if(okay){okay=MoveFileExW(windowsFilePath(temporary).c_str(),windowsFilePath(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!okay)failure=GetLastError();}
    if(!okay){DeleteFileW(windowsFilePath(temporary).c_str());error="Cannot commit save (Windows error "+std::to_string(failure)+")";}return okay;
}
Bytes envelope(unsigned kind,std::uint64_t sequence,const Bytes& payload){
    Writer w;w.u32(magic);w.u32(version);w.u32(kind);w.u32(std::uint32_t(payload.size()));w.u64(sequence);w.u64(hash(payload.data(),payload.size()));w.bytes(payload.data(),payload.size());return std::move(w.data);
}
}
namespace career_files {
CareerRead readGeneration(const std::filesystem::path& path,unsigned kind){return readOne(path,kind);}
CareerBytes envelope(unsigned kind,std::uint64_t generation,const CareerBytes& bytes){return scr::runtime::envelope(kind,generation,bytes);}
bool atomicWrite(const std::filesystem::path& path,const CareerBytes& bytes,std::string& error){return scr::runtime::atomicWrite(path,bytes,error);}
std::filesystem::path generationPath(const std::filesystem::path& path,unsigned n){return scr::runtime::generationPath(path,n);}
}
CareerBytes encodeCareerInfo(const NativeCareerInfo& info,const CareerBytes& checkpoint){
    if(!validId(info.id)||info.name.empty()||info.name.size()>12||info.division>3||(info.league!=0&&info.league!=11)||checkpoint.size()>2*1024*1024)throw std::invalid_argument("Invalid career metadata");
    Writer w;w.text(info.id);w.text(info.name);w.u32(info.division);w.u32(info.league);w.u64(info.lastPlayed);w.u32(std::uint32_t(checkpoint.size()));w.bytes(checkpoint.data(),checkpoint.size());return std::move(w.data);
}
NativeCareerInfo decodeCareerInfo(const CareerBytes& data,CareerBytes* checkpoint){
    Reader r{data};NativeCareerInfo info;info.id=r.text(32);info.name=r.text(12);info.division=r.u32();info.league=r.u32();info.lastPlayed=r.u64();const auto count=r.u32();
    if(!validId(info.id)||info.name.empty()||!std::all_of(info.name.begin(),info.name.end(),[](unsigned char c){return c>=32&&c<127;})||info.division>3||(info.league!=0&&info.league!=11)||count>2*1024*1024)throw std::runtime_error("Invalid career data");
    r.require(count);if(checkpoint)checkpoint->assign(data.begin()+r.position,data.begin()+r.position+count);r.position+=count;r.end();return info;
}
bool validNativeHall(const NativeHallBanks& data)noexcept{
    for(unsigned n=0;n<512;n+=16){
        for(unsigned i=0;i<12;++i)if(data[n+i]<32||data[n+i]>126)return false;
        const auto seconds=data[n+13],hundredths=data[n+14];
        if(data[n+12]>9||seconds>=0x60||(seconds&15)>9||(hundredths&15)>9||(hundredths>>4)>9)return false;
    }return true;
}
bool mergeNativeHall(NativeHallBanks& target,const NativeHallBanks& source){
    if(!validNativeHall(source))throw std::invalid_argument("Invalid record data");bool changed=false;
    for(unsigned entry=0;entry<512;entry+=16){
        if(std::lexicographical_compare(source.begin()+entry+12,source.begin()+entry+15,target.begin()+entry+12,target.begin()+entry+15)){
            std::copy_n(source.begin()+entry,16,target.begin()+entry);changed=true;
        }
    }return changed;
}
NativeCareerStore::NativeCareerStore(std::filesystem::path root,Validator validator,CareerStorageOptions options):
    root_(std::filesystem::absolute(std::move(root)).lexically_normal()),validator_(std::move(validator)),
    settings_(options.settings.empty()?root_/L"settings.ini":std::filesystem::absolute(options.settings).lexically_normal()){
    if(options.layout==CareerStorageLayout::Folders)folders_=std::make_unique<CareerFolders>(root_,validator_,std::move(options.observer));
    worker_=std::thread([this]{run();});
}
NativeCareerStore::~NativeCareerStore(){flush();{std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}changed_.notify_one();worker_.join();}
std::string NativeCareerStore::newId(){
    std::array<unsigned char,16> bytes{};if(BCryptGenRandom(nullptr,bytes.data(),ULONG(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot create career identity");
    std::ostringstream value;value<<std::hex<<std::setfill('0');for(auto b:bytes)value<<std::setw(2)<<unsigned(b);return value.str();
}
std::filesystem::path NativeCareerStore::careerPath(const std::string& id)const{
    if(!validId(id))throw std::invalid_argument("Invalid career identity");
    return folders_?folders_->directory(id)/L"career.scr-career":root_/L"careers"/(id+".scr-career");
}
std::filesystem::path NativeCareerStore::ghostPath(const GhostLapKey& key)const{
    if(!validId(key.career)||key.track>=8)throw std::invalid_argument("Invalid ghost identity");
    const auto folder=folders_?folders_->directory(key.career)/L"personal-bests/original":root_/L"ghosts"/key.career;
    return folder/(std::to_string(key.track)+(key.superLeague?"-super":"-normal")+".scr-ghost");
}
std::filesystem::path NativeCareerStore::enhancedBestPath(const EnhancedBestKey& key)const{
    validateEnhancedBestKey(key);
    const auto folder=folders_?folders_->directory(key.career)/L"personal-bests/enhanced":root_/L"ghosts-enhanced-v1"/key.career;
    return folder/(std::to_string(key.track)+(key.superLeague?"-super":"-normal")+".scr-ebest");
}
CareerRead NativeCareerStore::readFile(const std::filesystem::path& path,unsigned kind,const Validator& validator){
    const auto read=[&](const std::filesystem::path& file){auto value=readOne(file,kind);if(value.state==CareerFileState::Valid&&validator)value.state=validator(value.data);return value;};
    auto current=read(path);if(current.state==CareerFileState::Valid||current.state==CareerFileState::Unsupported)return current;
    auto best=current;
    for(unsigned n=1;n<=2;++n){auto next=read(generationPath(path,n));if(next.state==CareerFileState::Unsupported)return next;
        if(next.state==CareerFileState::Corrupt&&best.state==CareerFileState::Missing)best.state=CareerFileState::Corrupt;
        if(next.state==CareerFileState::Valid&&(best.state!=CareerFileState::Recovered||next.generation>best.generation)){best=std::move(next);best.state=CareerFileState::Recovered;}}
    return best;
}
CareerRead NativeCareerStore::readCareer(const std::string& id)const{
    if(folders_){unsigned count=0;for(const auto& entry:folders_->list())if(entry.id==id)++count;if(count>1)return {CareerFileState::Conflict};}
    try{return readFile(careerPath(id),1,validator_);}catch(const std::out_of_range&){return {CareerFileState::Missing};}
}
CareerRead NativeCareerStore::readHall()const{return readFile(root_/L"hall.scr-hall",2);}
CareerRead NativeCareerStore::readGhost(const GhostLapKey& key,std::uint64_t content)const{
    try{return readFile(ghostPath(key),4,[key,content](const auto& data){try{(void)decodeGhostLap(data,key,content);return CareerFileState::Valid;}
        catch(const UnsupportedGhostLap&){return CareerFileState::Unsupported;}catch(...){return CareerFileState::Corrupt;}});}
    catch(const std::out_of_range&){return {CareerFileState::Missing};}
}
CareerRead NativeCareerStore::readEnhancedBest(const EnhancedBestKey& key,EnhancedCatalog catalog)const{
    validateEnhancedCatalog(catalog);
    std::filesystem::path path;try{path=enhancedBestPath(key);}catch(const std::out_of_range&){return {CareerFileState::Missing};}
    CareerRead result;bool corrupt=false;
    for(unsigned n=0;n<3;++n){auto candidate=readOne(generationPath(path,n),5);
        if(candidate.state==CareerFileState::Valid)try{(void)decodeEnhancedBest(candidate.data,key,catalog);}
            catch(const UnsupportedEnhancedRecords&){candidate.state=CareerFileState::Unsupported;}catch(...){candidate.state=CareerFileState::Corrupt;}
        if(candidate.state==CareerFileState::Unsupported)return candidate;
        corrupt|=candidate.state==CareerFileState::Corrupt;
        if(candidate.state==CareerFileState::Valid&&(result.state==CareerFileState::Missing||
            decodeEnhancedBest(candidate.data).time.milliseconds()<decodeEnhancedBest(result.data).time.milliseconds())){
            result=std::move(candidate);if(n)result.state=CareerFileState::Recovered;
        }
    }
    if(result.state==CareerFileState::Missing&&corrupt)result.state=CareerFileState::Corrupt;return result;
}
std::string NativeCareerStore::lastUsed()const{
    if(folders_)return loadPlayerSelections(settings_).career;
    const auto saved=readFile(root_/L"last.scr-index",3);if(saved.state!=CareerFileState::Valid&&saved.state!=CareerFileState::Recovered)return {};
    Reader r{saved.data};return r.text(32);
}
CareerFileState NativeCareerStore::lastUsedState()const{return folders_?(lastUsed().empty()?CareerFileState::Missing:CareerFileState::Valid):readFile(root_/L"last.scr-index",3).state;}
std::vector<NativeCareerInfo> NativeCareerStore::list()const{
    if(folders_)return folders_->list();
    std::vector<NativeCareerInfo> result;std::error_code error;
    const auto directory=root_/L"careers";
    if(!std::filesystem::exists(directory,error))return result;
    std::set<std::string> identities;
    for(std::filesystem::directory_iterator it(directory,error),end;!error&&it!=end;it.increment(error)){
        if(!it->is_regular_file())continue;
        const auto filename=it->path().filename().string();
        if(filename.size()<43||filename.substr(32,11)!=".scr-career")continue;
        const auto suffix=filename.substr(43);if(!suffix.empty()&&suffix!=".bak1"&&suffix!=".bak2")continue;
        const auto id=filename.substr(0,32);if(validId(id))identities.insert(id);
    }
    for(const auto& id:identities){
        const auto saved=readCareer(id);NativeCareerInfo info;
        if(saved.state==CareerFileState::Valid||saved.state==CareerFileState::Recovered){info=decodeCareerInfo(saved.data);if(info.id!=id){info={};info.id=id;info.name="Unreadable";info.state=CareerFileState::Corrupt;}else info.state=saved.state;}
        else{info.id=id;info.name="Unreadable";info.state=saved.state;}
        result.push_back(std::move(info));
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.lastPlayed!=b.lastPlayed?a.lastPlayed>b.lastPlayed:a.id<b.id;});return result;
}
void NativeCareerStore::submit(std::filesystem::path path,unsigned kind,CareerBytes bytes,Validator validator){
    std::lock_guard<std::mutex> lock(mutex_);
    if(kind==5)if(const auto old=latest_.find(path);old!=latest_.end()){
        const auto candidate=decodeEnhancedBest(bytes),held=decodeEnhancedBest(old->second.data);
        if(!sameEnhancedCatalog(candidate.catalog,held.catalog))throw UnsupportedEnhancedRecords("Pending Enhanced PB compatibility differs");
        // Coalescing and retries must retain the fastest unsaved candidate too.
        // Still enqueue it so a missing or damaged head can be repaired.
        if(held.time.milliseconds()<=candidate.time.milliseconds()){bytes=old->second.data;validator=old->second.validator;}
    }
    Job job{std::move(path),kind,std::move(bytes),++serial_,std::move(validator)};
    latest_[job.path]=job;
    jobs_.erase(std::remove_if(jobs_.begin(),jobs_.end(),[&](const auto& old){return old.path==job.path;}),jobs_.end());jobs_.push_back(std::move(job));changed_.notify_one();
}
void NativeCareerStore::saveCareer(const NativeCareerInfo& info,const CareerBytes& checkpoint){
    const auto bytes=encodeCareerInfo(info,checkpoint);
    submit(folders_?folders_->reserve(info)/L"career.scr-career":careerPath(info.id),1,bytes);
}
void NativeCareerStore::submitSavedGame(std::filesystem::path path,CareerBytes bytes,CustomWriter writer){
    std::lock_guard<std::mutex> lock(mutex_);Job job{std::move(path),7,std::move(bytes),++serial_};job.custom=std::move(writer);
    latest_[job.path]=job;jobs_.erase(std::remove_if(jobs_.begin(),jobs_.end(),[&](const auto& old){return old.path==job.path;}),jobs_.end());jobs_.push_back(std::move(job));changed_.notify_one();
}
std::future<SavedGameCopyResult> NativeCareerStore::executeSavedGame(std::function<SavedGameCopyResult()> operation){
    auto promise=std::make_shared<std::promise<SavedGameCopyResult>>();auto future=promise->get_future();Job job;
    job.task=[operation=std::move(operation),promise]{try{promise->set_value(operation());}catch(const std::exception& error){promise->set_value({{},error.what()});}};
    {std::lock_guard<std::mutex> lock(mutex_);jobs_.push_back(std::move(job));changed_.notify_one();}return future;
}
void NativeCareerStore::forgetWrites(const std::filesystem::path& folder){
    std::lock_guard<std::mutex> lock(mutex_);for(auto it=latest_.begin();it!=latest_.end();){const auto rel=it->first.lexically_relative(folder);
        if(!rel.empty()&&*rel.begin()!=L".."){errors_.erase(it->first);it=latest_.erase(it);}else ++it;}
}
void NativeCareerStore::saveHall(const NativeHallBanks& hall){if(!validNativeHall(hall))throw std::invalid_argument("Invalid Hall records");submit(root_/L"hall.scr-hall",2,Bytes(hall.begin(),hall.end()));}
void NativeCareerStore::setLastUsed(const std::string& id){if(!id.empty()&&!validId(id))throw std::invalid_argument("Invalid last career");Writer w;w.text(id);submit(folders_?settings_:root_/L"last.scr-index",folders_?6u:3u,std::move(w.data));}
void NativeCareerStore::saveGhost(const GhostLap& lap){
    const auto key=lap.context.key;const auto content=lap.context.trackContentIdentity;
    submit(ghostPath(key),4,encodeGhostLap(lap),[key,content](const auto& data){try{(void)decodeGhostLap(data,key,content);return CareerFileState::Valid;}
        catch(const UnsupportedGhostLap&){return CareerFileState::Unsupported;}catch(...){return CareerFileState::Corrupt;}});
}
void NativeCareerStore::saveEnhancedBest(const EnhancedBest& best){
    const auto key=best.key;const auto catalog=best.catalog;
    submit(enhancedBestPath(key),5,encodeEnhancedBest(best),[key,catalog](const auto& data){try{(void)decodeEnhancedBest(data,key,catalog);return CareerFileState::Valid;}
        catch(const UnsupportedEnhancedRecords&){return CareerFileState::Unsupported;}catch(...){return CareerFileState::Corrupt;}});
}
bool NativeCareerStore::writeFile(const Job& job,std::string& error,const Validator& validator){
    try{
        if(job.kind==6){Reader reader{job.data};const auto id=reader.text(32);reader.end();
            if(!savePlayerSelection(job.path,id)){error="Cannot save last-used career preference";return false;}return true;}
        if(job.kind==1&&validator&&validator(job.data)!=CareerFileState::Valid){error="Invalid career checkpoint";return false;}
        const auto read=[&](const std::filesystem::path& path){auto value=readOne(path,job.kind);
            const auto& validate=(job.kind==4||job.kind==5)?job.validator:validator;
            if(value.state==CareerFileState::Valid&&(job.kind==1||job.kind==4||job.kind==5)&&validate)value.state=validate(value.data);return value;};
        const auto current=read(job.path);if(current.state==CareerFileState::Unsupported){error="Save belongs to an unsupported version";return false;}
        auto previous=read(generationPath(job.path,1)),older=read(generationPath(job.path,2));
        if(previous.state==CareerFileState::Unsupported||older.state==CareerFileState::Unsupported){error="A recovery copy belongs to an unsupported version";return false;}
        const auto* payload=&job.data;
        const CareerRead* enhancedBest=nullptr;
        if(job.kind==5){
            const CareerRead* copies[]{&current,&previous,&older};
            const CareerRead* best=nullptr;for(const auto* copy:copies)if(copy->state==CareerFileState::Valid&&
                (!best||decodeEnhancedBest(copy->data).time.milliseconds()<decodeEnhancedBest(best->data).time.milliseconds()))best=copy;
            enhancedBest=best;
            if(best&&decodeEnhancedBest(best->data).time.milliseconds()<=decodeEnhancedBest(*payload).time.milliseconds()){
                if(current.state==CareerFileState::Valid&&current.data==best->data)return true;
                payload=&best->data;
            }
        }
        if(job.kind==4||job.kind==5){
            const CareerRead* copies[]{&current,&previous,&older};
            const auto stamp=std::to_wstring(std::chrono::system_clock::now().time_since_epoch().count())+L"-"+std::to_wstring(job.serial);
            for(unsigned n=0;n<3;++n)if(copies[n]->state==CareerFileState::Corrupt){
                const auto path=generationPath(job.path,n),preserved=std::filesystem::path(path.wstring()+L".damaged-"+stamp);
                if(!MoveFileExW(path.c_str(),preserved.c_str(),MOVEFILE_WRITE_THROUGH)){error="Cannot preserve unreadable ghost";return false;}
            }
        }
        const auto newest=std::max({current.generation,previous.generation,older.generation});
        if(job.kind==5&&newest==std::numeric_limits<std::uint64_t>::max()){error="Enhanced PB generation exhausted";return false;}
        const std::uint64_t sequence=newest+1;
        if(job.kind==5&&enhancedBest){
            const CareerRead* history=enhancedBest;const CareerRead* copies[]{&current,&previous,&older};
            for(const auto* copy:copies)if(copy!=enhancedBest&&copy->state==CareerFileState::Valid&&(history==enhancedBest||copy->generation>history->generation))history=copy;
            const auto backupBest=[&]{return atomicWrite(generationPath(job.path,1),envelope(job.kind,enhancedBest->generation,enhancedBest->data),error);};
            const auto backupHistory=[&]{return atomicWrite(generationPath(job.path,2),envelope(job.kind,history->generation,history->data),error);};
            if(enhancedBest==&older){if(!backupBest()||!backupHistory())return false;}
            else if(!backupHistory()||!backupBest())return false;
        }else if(current.state==CareerFileState::Valid&&current.generation!=previous.generation){
            if(previous.state==CareerFileState::Valid&&!atomicWrite(generationPath(job.path,2),envelope(job.kind,previous.generation,previous.data),error))return false;
            if(!atomicWrite(generationPath(job.path,1),envelope(job.kind,current.generation,current.data),error))return false;
        }
        return atomicWrite(job.path,envelope(job.kind,sequence,*payload),error);
    }catch(const std::exception& failure){error=failure.what();return false;}
}
void NativeCareerStore::run(){
    for(;;){Job job;{std::unique_lock<std::mutex> lock(mutex_);changed_.wait(lock,[&]{return stopping_||!jobs_.empty();});if(stopping_&&jobs_.empty())return;job=std::move(jobs_.front());jobs_.pop_front();writing_=true;}
        if(job.task){job.task();std::lock_guard<std::mutex> lock(mutex_);writing_=false;if(jobs_.empty())idle_.notify_all();continue;}
        std::string error;bool okay=false;
        try{
            if(job.custom)okay=job.custom(job.data,error);
            else{
            if(folders_)folders_->recover();
            if(folders_&&(job.kind==1||job.kind==4||job.kind==5)){
                const auto expected=job.kind==1?careerPath(decodeCareerInfo(job.data).id):job.kind==4?ghostPath(decodeGhostLap(job.data).context.key):enhancedBestPath(decodeEnhancedBest(job.data).key);
                if(expected!=job.path)throw std::runtime_error("Career location changed; queued write preserved in memory");
            }
            if(folders_&&job.kind==1&&!folders_->known(job.path.parent_path())){
                folders_->create(job.path.parent_path(),[&](const auto& stage){auto staged=job;staged.path=stage/L"career.scr-career";
                    if(!writeFile(staged,error,validator_))throw std::runtime_error(error);});okay=true;
            }else okay=writeFile(job,error,validator_);
            if(okay&&folders_&&job.kind==1)folders_->update(decodeCareerInfo(job.data),job.path.parent_path());
            }
        }catch(const std::exception& failure){error=failure.what();}
        {std::lock_guard<std::mutex> lock(mutex_);if(okay){errors_.erase(job.path);
                if(job.kind==5||job.custom)if(const auto latest=latest_.find(job.path);latest!=latest_.end()&&latest->second.serial==job.serial)latest_.erase(latest);
            }else errors_[job.path]=std::move(error);writing_=false;if(jobs_.empty())idle_.notify_all();}
    }
}
bool NativeCareerStore::flush(){std::unique_lock<std::mutex> lock(mutex_);idle_.wait(lock,[&]{return !writing_&&jobs_.empty();});return errors_.empty();}
bool NativeCareerStore::pending()const{std::lock_guard<std::mutex> lock(mutex_);return writing_||!jobs_.empty();}
bool NativeCareerStore::failed()const{std::lock_guard<std::mutex> lock(mutex_);return !errors_.empty();}
bool NativeCareerStore::ghostFailed()const{std::lock_guard<std::mutex> lock(mutex_);return std::any_of(errors_.begin(),errors_.end(),[](const auto& e){return e.first.extension()==L".scr-ghost";});}
bool NativeCareerStore::enhancedBestFailed()const{std::lock_guard<std::mutex> lock(mutex_);return std::any_of(errors_.begin(),errors_.end(),[](const auto& e){return e.first.extension()==L".scr-ebest";});}
std::string NativeCareerStore::error()const{std::lock_guard<std::mutex> lock(mutex_);return errors_.empty()?std::string{}:errors_.begin()->second;}
std::string NativeCareerStore::failureSummary()const{
    std::lock_guard<std::mutex> lock(mutex_);bool hall=false,career=false,ghost=false,enhanced=false,multiplayer=false;
    for(const auto& item:errors_){if(item.first.filename()==L"hall.scr-hall")hall=true;else if(item.first.extension()==L".scr-ghost")ghost=true;else if(item.first.extension()==L".scr-ebest")enhanced=true;else if(item.first.extension()==L".scr-multiplayer"||item.first.filename()==L".multiplayer-preference")multiplayer=true;else career=true;}
    if(multiplayer)return hall?"Progress and Hall not saved":career||ghost||enhanced?"Saved games not saved":"Multiplayer game not saved";
    if(enhanced)return hall||career||ghost?"Progress and records not saved":"Enhanced PB not saved";
    if(ghost)return hall?(career?"Career, Hall and ghosts not saved":"Hall records and ghosts not saved"):(career?"Career and ghosts not saved":"Ghost not saved");
    return hall?(career?"Career and Hall not saved":"Hall records not saved"):"Career progress not saved";
}
void NativeCareerStore::retry(){std::lock_guard<std::mutex> lock(mutex_);for(const auto& error:errors_)if(const auto job=latest_.find(error.first);job!=latest_.end())jobs_.push_back(job->second);changed_.notify_one();}
void NativeCareerStore::acknowledgeFailures(){std::lock_guard<std::mutex> lock(mutex_);errors_.clear();}
bool NativeCareerStore::removeCareer(const std::string& id){
    if(folders_){for(const auto& entry:folders_->list())if(entry.id==id){
        if(entry.state==CareerFileState::Conflict)return false;return removeEntry(entry);}
        flush();try{const auto folder=folders_->directory(id);std::lock_guard<std::mutex> lock(mutex_);
            for(auto it=latest_.begin();it!=latest_.end();){const auto rel=it->first.lexically_relative(folder);if(!rel.empty()&&*rel.begin()!=L".."){errors_.erase(it->first);it=latest_.erase(it);}else ++it;}
        }catch(const std::out_of_range&){}folders_->forget(id);return true;}
    flush();const auto path=careerPath(id);std::error_code error;
    {std::lock_guard<std::mutex> lock(mutex_);latest_.erase(path);errors_.erase(path);}
    for(unsigned n=0;n<3;++n){std::filesystem::remove(generationPath(path,n),error);if(error)return false;}
    std::filesystem::remove(path.wstring()+L".tmp",error);if(error)return false;
    for(bool super:{false,true})for(unsigned track=0;track<8;++track)if(!clearGhost({id,track,super}))return false;
    for(bool super:{false,true})for(unsigned track=0;track<8;++track)if(!clearEnhancedBest({id,track,super}))return false;
    if(lastUsed()==id){setLastUsed({});return flush();}return true;
}
bool NativeCareerStore::removeEntry(const NativeCareerInfo& info){
    if(!folders_)return removeCareer(info.id);
    flush();return removeEntryFiles(info);
}
bool NativeCareerStore::removeEntryFiles(const NativeCareerInfo& info){
    try{
        {std::lock_guard<std::mutex> lock(mutex_);for(auto it=latest_.begin();it!=latest_.end();){
            const auto relative=it->first.lexically_relative(info.folder);if(!relative.empty()&&*relative.begin()!=L".."){errors_.erase(it->first);it=latest_.erase(it);}else ++it;}}
        folders_->erase(info);
        bool remains=false;for(const auto& entry:folders_->list())remains|=!info.id.empty()&&entry.id==info.id;
        if(!remains&&!info.id.empty()&&lastUsed()==info.id)return savePlayerSelection(settings_,{});return true;
    }catch(const std::exception&){return false;}
}
std::future<CareerCopyResult> NativeCareerStore::eraseEntry(const NativeCareerInfo& info){
    if(!folders_)throw std::logic_error("Folder deletion needs folder storage");flush();
    auto promise=std::make_shared<std::promise<CareerCopyResult>>();auto result=promise->get_future();Job job;
    job.task=[this,info,promise]{promise->set_value({{},removeEntryFiles(info)?std::string{}:std::string("Could not finish deleting career")});};
    {std::lock_guard<std::mutex> lock(mutex_);jobs_.push_back(std::move(job));changed_.notify_one();}return result;
}
std::future<CareerCopyResult> NativeCareerStore::resolveCopy(const NativeCareerInfo& info,CareerCopyChoice choice){
    if(!folders_)throw std::logic_error("Career copy resolution needs folder storage");
    if(!flush())throw std::runtime_error("Save pending progress before resolving a copy");
    auto promise=std::make_shared<std::promise<CareerCopyResult>>();auto future=promise->get_future();const auto fresh=newId();
    Job job;job.task=[this,info,choice,fresh,promise]{
        try{const auto id=folders_->resolve(info,choice,fresh);
            {std::lock_guard<std::mutex> lock(mutex_);latest_.clear();}promise->set_value({id,{}});
        }catch(const std::exception& error){
            const std::string message=error.what();try{folders_->recover();folders_->refresh();}catch(const std::exception&){}
            promise->set_value({{},message});
        }
    };
    {std::lock_guard<std::mutex> lock(mutex_);jobs_.push_back(std::move(job));changed_.notify_one();}return future;
}
bool NativeCareerStore::clearGhost(const GhostLapKey& key){
    return clearRecord(ghostPath(key));
}
bool NativeCareerStore::clearEnhancedBest(const EnhancedBestKey& key){return clearRecord(enhancedBestPath(key));}
bool NativeCareerStore::clearRecord(const std::filesystem::path& path){
    flush();
    {std::lock_guard<std::mutex> lock(mutex_);latest_.erase(path);errors_.erase(path);}
    std::error_code error;
    for(unsigned n=0;n<4;++n){
        const auto file=n==3?std::filesystem::path(path.wstring()+L".tmp"):generationPath(path,n);
        std::filesystem::remove(file,error);
        if(error){std::lock_guard<std::mutex> lock(mutex_);errors_[path]="Could not clear ghost: "+error.message();return false;}
    }
    // Confirmed clear also removes quarantined copies of this one target.
    if(std::filesystem::exists(path.parent_path(),error))for(const auto& entry:std::filesystem::directory_iterator(path.parent_path(),error)){
        const auto name=entry.path().filename().wstring(),base=path.filename().wstring();
        if(name.find(base+L".damaged-")==0||name.find(base+L".bak1.damaged-")==0||name.find(base+L".bak2.damaged-")==0){
            if(!entry.is_directory(error))std::filesystem::remove(entry.path(),error);if(error)break;
        }
    }
    if(error){std::lock_guard<std::mutex> lock(mutex_);errors_[path]="Could not clear ghost: "+error.message();return false;}
    return true;
}
bool NativeCareerStore::resetHall(const NativeHallBanks& hall){
    flush();const auto path=root_/L"hall.scr-hall";std::error_code error;
    for(unsigned n=0;n<3;++n)if(readOne(generationPath(path,n),2).state==CareerFileState::Unsupported)return false;
    const auto stamp=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    for(unsigned n=0;n<3;++n){const auto old=generationPath(path,n);if(std::filesystem::exists(old,error)){std::filesystem::copy_file(old,old.wstring()+L".damaged-"+std::wstring(stamp.begin(),stamp.end()),error);if(error)return false;std::filesystem::remove(old,error);if(error)return false;}}
    saveHall(hall);return flush();
}
} // namespace scr::runtime
