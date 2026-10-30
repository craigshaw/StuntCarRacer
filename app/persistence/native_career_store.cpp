#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "persistence/native_career_store.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <set>

namespace scr::runtime {
namespace {
using namespace state::checkpoint;
constexpr std::uint32_t magic=0x43524353,version=1;
constexpr std::size_t maximumFile=4*1024*1024;
bool validId(const std::string& id){return id.size()==32&&std::all_of(id.begin(),id.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::filesystem::path generationPath(const std::filesystem::path& path,unsigned n){return n?std::filesystem::path(path.wstring()+L".bak"+std::to_wstring(n)):path;}
CareerRead readOne(const std::filesystem::path& path,unsigned kind){
    std::error_code error;if(!std::filesystem::exists(path,error))return {error?CareerFileState::Corrupt:CareerFileState::Missing};
    const auto size=std::filesystem::file_size(path,error);if(error||size<32||size>maximumFile)return {CareerFileState::Corrupt};
    std::ifstream file(path,std::ios::binary);Bytes bytes(std::size_t(size),0);file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));
    if(!file)return {CareerFileState::Corrupt};
    try{
        Reader r{bytes};if(r.u32()!=magic)return {CareerFileState::Corrupt};
        const auto savedVersion=r.u32();if(savedVersion!=version)return {CareerFileState::Unsupported};
        if(r.u32()!=kind)return {CareerFileState::Corrupt};
        const auto length=r.u32();const auto sequence=r.u64(),checksum=r.u64();
        if(length!=bytes.size()-r.position||checksum!=hash(bytes.data()+r.position,length))return {CareerFileState::Corrupt};
        Bytes payload(bytes.begin()+r.position,bytes.end());
        if(kind==1)(void)decodeCareerInfo(payload);
        if(kind==2){if(payload.size()!=512)return {CareerFileState::Corrupt};NativeHallBanks hall{};std::copy(payload.begin(),payload.end(),hall.begin());if(!validNativeHall(hall))return {CareerFileState::Corrupt};}
        if(kind==3){Reader index{payload};const auto id=index.text(32);index.end();if(!id.empty()&&!validId(id))return {CareerFileState::Corrupt};}
        return {CareerFileState::Valid,std::move(payload),sequence};
    }catch(...){return {CareerFileState::Corrupt};}
}
bool atomicWrite(const std::filesystem::path& path,const Bytes& bytes,std::string& error){
    std::error_code code;std::filesystem::create_directories(path.parent_path(),code);
    if(code){error="Cannot create save folder: "+code.message();return false;}
    const auto temporary=std::filesystem::path(path.wstring()+L".tmp");
    const auto handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE){error="Cannot write save (Windows error "+std::to_string(GetLastError())+")";return false;}
    DWORD count{};bool okay=WriteFile(handle,bytes.data(),DWORD(bytes.size()),&count,nullptr)&&count==bytes.size()&&FlushFileBuffers(handle);
    auto failure=okay?ERROR_SUCCESS:GetLastError();CloseHandle(handle);
    if(okay){okay=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!okay)failure=GetLastError();}
    if(!okay){DeleteFileW(temporary.c_str());error="Cannot commit save (Windows error "+std::to_string(failure)+")";}return okay;
}
Bytes envelope(unsigned kind,std::uint64_t sequence,const Bytes& payload){
    Writer w;w.u32(magic);w.u32(version);w.u32(kind);w.u32(std::uint32_t(payload.size()));w.u64(sequence);w.u64(hash(payload.data(),payload.size()));w.bytes(payload.data(),payload.size());return std::move(w.data);
}
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
NativeCareerStore::NativeCareerStore(std::filesystem::path root,Validator validator):root_(std::filesystem::absolute(std::move(root)).lexically_normal()),validator_(std::move(validator)){worker_=std::thread([this]{run();});}
NativeCareerStore::~NativeCareerStore(){flush();{std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}changed_.notify_one();worker_.join();}
std::string NativeCareerStore::newId(){
    std::array<unsigned char,16> bytes{};if(BCryptGenRandom(nullptr,bytes.data(),ULONG(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot create career identity");
    std::ostringstream value;value<<std::hex<<std::setfill('0');for(auto b:bytes)value<<std::setw(2)<<unsigned(b);return value.str();
}
std::filesystem::path NativeCareerStore::careerPath(const std::string& id)const{if(!validId(id))throw std::invalid_argument("Invalid career identity");return root_/L"careers"/(id+".scr-career");}
CareerRead NativeCareerStore::readFile(const std::filesystem::path& path,unsigned kind,const Validator& validator){
    const auto read=[&](const std::filesystem::path& file){auto value=readOne(file,kind);if(value.state==CareerFileState::Valid&&validator)value.state=validator(value.data);return value;};
    auto current=read(path);if(current.state==CareerFileState::Valid||current.state==CareerFileState::Unsupported)return current;
    auto best=current;
    for(unsigned n=1;n<=2;++n){auto next=read(generationPath(path,n));if(next.state==CareerFileState::Unsupported)return next;
        if(next.state==CareerFileState::Corrupt&&best.state==CareerFileState::Missing)best.state=CareerFileState::Corrupt;
        if(next.state==CareerFileState::Valid&&(best.state!=CareerFileState::Recovered||next.generation>best.generation)){best=std::move(next);best.state=CareerFileState::Recovered;}}
    return best;
}
CareerRead NativeCareerStore::readCareer(const std::string& id)const{return readFile(careerPath(id),1,validator_);}
CareerRead NativeCareerStore::readHall()const{return readFile(root_/L"hall.scr-hall",2);}
std::string NativeCareerStore::lastUsed()const{
    const auto saved=readFile(root_/L"last.scr-index",3);if(saved.state!=CareerFileState::Valid&&saved.state!=CareerFileState::Recovered)return {};
    Reader r{saved.data};return r.text(32);
}
CareerFileState NativeCareerStore::lastUsedState()const{return readFile(root_/L"last.scr-index",3).state;}
std::vector<NativeCareerInfo> NativeCareerStore::list()const{
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
void NativeCareerStore::submit(std::filesystem::path path,unsigned kind,CareerBytes bytes){
    std::lock_guard<std::mutex> lock(mutex_);Job job{std::move(path),kind,std::move(bytes),++serial_};
    latest_[job.path]=job;
    jobs_.erase(std::remove_if(jobs_.begin(),jobs_.end(),[&](const auto& old){return old.path==job.path;}),jobs_.end());jobs_.push_back(std::move(job));changed_.notify_one();
}
void NativeCareerStore::saveCareer(const NativeCareerInfo& info,const CareerBytes& checkpoint){submit(careerPath(info.id),1,encodeCareerInfo(info,checkpoint));}
void NativeCareerStore::saveHall(const NativeHallBanks& hall){if(!validNativeHall(hall))throw std::invalid_argument("Invalid Hall records");submit(root_/L"hall.scr-hall",2,Bytes(hall.begin(),hall.end()));}
void NativeCareerStore::setLastUsed(const std::string& id){if(!id.empty()&&!validId(id))throw std::invalid_argument("Invalid last career");Writer w;w.text(id);submit(root_/L"last.scr-index",3,std::move(w.data));}
bool NativeCareerStore::writeFile(const Job& job,std::string& error,const Validator& validator){
    try{
        const auto read=[&](const std::filesystem::path& path){auto value=readOne(path,job.kind);if(value.state==CareerFileState::Valid&&job.kind==1&&validator)value.state=validator(value.data);return value;};
        const auto current=read(job.path);if(current.state==CareerFileState::Unsupported){error="Save belongs to an unsupported version";return false;}
        auto previous=read(generationPath(job.path,1)),older=read(generationPath(job.path,2));
        if(previous.state==CareerFileState::Unsupported||older.state==CareerFileState::Unsupported){error="A recovery copy belongs to an unsupported version";return false;}
        std::uint64_t sequence=std::max({current.generation,previous.generation,older.generation})+1;
        if(current.state==CareerFileState::Valid&&current.generation!=previous.generation){
            if(previous.state==CareerFileState::Valid&&!atomicWrite(generationPath(job.path,2),envelope(job.kind,previous.generation,previous.data),error))return false;
            if(!atomicWrite(generationPath(job.path,1),envelope(job.kind,current.generation,current.data),error))return false;
        }
        return atomicWrite(job.path,envelope(job.kind,sequence,job.data),error);
    }catch(const std::exception& failure){error=failure.what();return false;}
}
void NativeCareerStore::run(){
    for(;;){Job job;{std::unique_lock<std::mutex> lock(mutex_);changed_.wait(lock,[&]{return stopping_||!jobs_.empty();});if(stopping_&&jobs_.empty())return;job=std::move(jobs_.front());jobs_.pop_front();writing_=true;}
        std::string error;const bool okay=writeFile(job,error,validator_);
        {std::lock_guard<std::mutex> lock(mutex_);if(okay)errors_.erase(job.path);else errors_[job.path]=std::move(error);writing_=false;if(jobs_.empty())idle_.notify_all();}
    }
}
bool NativeCareerStore::flush(){std::unique_lock<std::mutex> lock(mutex_);idle_.wait(lock,[&]{return !writing_&&jobs_.empty();});return errors_.empty();}
bool NativeCareerStore::pending()const{std::lock_guard<std::mutex> lock(mutex_);return writing_||!jobs_.empty();}
bool NativeCareerStore::failed()const{std::lock_guard<std::mutex> lock(mutex_);return !errors_.empty();}
std::string NativeCareerStore::error()const{std::lock_guard<std::mutex> lock(mutex_);return errors_.empty()?std::string{}:errors_.begin()->second;}
std::string NativeCareerStore::failureSummary()const{
    std::lock_guard<std::mutex> lock(mutex_);bool hall=false,career=false;
    for(const auto& item:errors_){if(item.first.filename()==L"hall.scr-hall")hall=true;else career=true;}
    return hall?(career?"Career and Hall not saved":"Hall records not saved"):"Career progress not saved";
}
void NativeCareerStore::retry(){std::lock_guard<std::mutex> lock(mutex_);for(const auto& error:errors_)if(const auto job=latest_.find(error.first);job!=latest_.end())jobs_.push_back(job->second);changed_.notify_one();}
void NativeCareerStore::acknowledgeFailures(){std::lock_guard<std::mutex> lock(mutex_);errors_.clear();}
bool NativeCareerStore::removeCareer(const std::string& id){
    flush();const auto path=careerPath(id);std::error_code error;
    {std::lock_guard<std::mutex> lock(mutex_);latest_.erase(path);errors_.erase(path);}
    for(unsigned n=0;n<3;++n){std::filesystem::remove(generationPath(path,n),error);if(error)return false;}
    std::filesystem::remove(path.wstring()+L".tmp",error);if(error)return false;
    if(lastUsed()==id){setLastUsed({});return flush();}return true;
}
bool NativeCareerStore::resetHall(const NativeHallBanks& hall){
    flush();const auto path=root_/L"hall.scr-hall";std::error_code error;
    for(unsigned n=0;n<3;++n)if(readOne(generationPath(path,n),2).state==CareerFileState::Unsupported)return false;
    const auto stamp=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    for(unsigned n=0;n<3;++n){const auto old=generationPath(path,n);if(std::filesystem::exists(old,error)){std::filesystem::copy_file(old,old.wstring()+L".damaged-"+std::wstring(stamp.begin(),stamp.end()),error);if(error)return false;std::filesystem::remove(old,error);if(error)return false;}}
    saveHall(hall);return flush();
}
} // namespace scr::runtime
