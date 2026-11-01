#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "persistence/shared_hall_store.h"
#include "platform/windows/shared_hall_storage.h"
#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

namespace scr::runtime {
namespace {
namespace fs=std::filesystem;
using namespace state::checkpoint;
constexpr std::uint32_t indexMagic=0x58444948,indexVersion=1;
constexpr std::size_t indexMaximumBytes=hallTableMaximumBytes+28;
class HallIoError:public std::runtime_error {public:using std::runtime_error::runtime_error;};
[[noreturn]]void failure(const char* message,DWORD code=GetLastError()){
    throw HallIoError(std::string(message)+" (Windows error "+std::to_string(code)+")");
}
struct File {HANDLE value{INVALID_HANDLE_VALUE};~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
HallGhostDigest digest(const Bytes& bytes){
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE h{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot open Hall ghost hash");
    auto status=BCryptCreateHash(algorithm,&h,nullptr,0,nullptr,0,0);HallGhostDigest result{};
    if(status>=0)status=BCryptHashData(h,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0);
    if(status>=0)status=BCryptFinishHash(h,result.data(),ULONG(result.size()),0);
    if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0)throw std::runtime_error("Cannot hash Hall ghost");return result;
}
std::optional<Bytes> readBytes(const fs::path& path,std::size_t maximum){
    File file;file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file.value==INVALID_HANDLE_VALUE){const auto code=GetLastError();if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)return {};failure("Cannot read shared Hall",code);}
    LARGE_INTEGER size{};if(!GetFileSizeEx(file.value,&size))failure("Cannot size shared Hall");
    if(size.QuadPart<0||std::uint64_t(size.QuadPart)>maximum)throw std::length_error("Shared Hall file exceeds its bound");
    Bytes bytes(std::size_t(size.QuadPart),0);DWORD count{};
    if(!ReadFile(file.value,bytes.data(),DWORD(bytes.size()),&count,nullptr)||count!=bytes.size())failure("Cannot read complete shared Hall");
    return bytes;
}
void atomicWrite(const fs::path& path,const Bytes& bytes,const HallWriteObserver& observer,HallWriteStage flushed,HallWriteStage published){
    const auto temporary=fs::path(path.wstring()+L".tmp");
    {File file;file.value=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file.value==INVALID_HANDLE_VALUE)failure("Cannot create shared Hall temporary");DWORD count{};
        if(!WriteFile(file.value,bytes.data(),DWORD(bytes.size()),&count,nullptr)||count!=bytes.size()||!FlushFileBuffers(file.value))failure("Cannot flush shared Hall temporary");}
    if(observer)observer(flushed);
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))failure("Cannot publish shared Hall");
    if(observer)observer(published);
}
Bytes envelope(const HallOfFame& hall,std::uint64_t generation){
    const auto table=encodeHallTable(hall);Writer w;w.u32(indexMagic);w.u32(indexVersion);w.u64(generation);w.u32(unsigned(table.size()));w.bytes(table.data(),table.size());
    w.u64(hash(w.data.data(),w.data.size()));return std::move(w.data);
}
struct Generation {HallArchiveState state{HallArchiveState::Missing};Bytes bytes;std::shared_ptr<const HallOfFame> hall;std::uint64_t number{};unsigned slot{};};
fs::path indexPath(const fs::path& root,unsigned slot){return root/(slot?L"index.scr-hall.bak"+std::to_wstring(slot):L"index.scr-hall");}
Generation readGeneration(const fs::path& root,unsigned slot,HallCatalog catalog){
    Generation g;g.slot=slot;
    try{
        auto bytes=readBytes(indexPath(root,slot),indexMaximumBytes);if(!bytes)return g;g.bytes=std::move(*bytes);
        Reader r{g.bytes};if(r.u32()!=indexMagic)throw std::runtime_error("Invalid Hall index magic");
        if(r.u32()!=indexVersion)throw UnsupportedHallTable("Unsupported Hall index version");
        g.number=r.u64();const auto size=r.u32();if(!g.number||size>hallTableMaximumBytes)throw std::runtime_error("Invalid Hall index header");
        r.require(size);Bytes table(g.bytes.begin()+r.position,g.bytes.begin()+r.position+size);r.position+=size;
        const auto end=r.position;if(r.u64()!=hash(g.bytes.data(),end))throw std::runtime_error("Hall index checksum mismatch");r.end();
        g.hall=std::make_shared<const HallOfFame>(decodeHallTable(table,catalog));g.state=HallArchiveState::Valid;
    }catch(const HallIoError&){throw;}
    catch(const UnsupportedHallTable&){g.state=HallArchiveState::Unsupported;}
    catch(const std::length_error&){g.state=HallArchiveState::Corrupt;}
    catch(const std::invalid_argument&){g.state=HallArchiveState::Corrupt;}
    // Format-reader failures are distinct from OS I/O failures above.
    catch(const std::runtime_error&){g.state=HallArchiveState::Corrupt;}
    return g;
}
struct Loaded {std::array<Generation,3> copies;HallArchiveRead result;std::vector<Generation*> valid;};
Loaded load(const fs::path& root,HallCatalog catalog){
    Loaded loaded;bool corrupt=false,unsupported=false;
    for(unsigned n=0;n<3;++n){auto& g=loaded.copies[n];g=readGeneration(root,n,catalog);
        corrupt|=g.state==HallArchiveState::Corrupt;unsupported|=g.state==HallArchiveState::Unsupported;}
    // Populate pointers only after returning Loaded (see select below).
    loaded.result.state=unsupported?HallArchiveState::Unsupported:corrupt?HallArchiveState::Corrupt:HallArchiveState::Missing;
    return loaded;
}
void select(Loaded& l,HallCatalog catalog){
    if(l.result.state==HallArchiveState::Unsupported)return;
    for(auto& g:l.copies)if(g.state==HallArchiveState::Valid)l.valid.push_back(&g);
    std::stable_sort(l.valid.begin(),l.valid.end(),[](const auto* a,const auto* b){return a->number>b->number;});
    for(unsigned n=1;n<l.valid.size();++n)if(l.valid[n-1]->number==l.valid[n]->number&&l.valid[n-1]->bytes!=l.valid[n]->bytes){l.result.state=HallArchiveState::Corrupt;l.valid.clear();return;}
    if(!l.valid.empty()){const auto& g=*l.valid.front();l.result={g.slot?HallArchiveState::Recovered:HallArchiveState::Valid,g.hall,g.number};}
    else if(l.result.state==HallArchiveState::Missing)l.result.hall=std::make_shared<const HallOfFame>(catalog);
}
void validateSubmission(const HallSubmission& s,HallCatalog c){
    HallOfFame single(c);single.offer(s.attempt);
    if(s.recording){
        if(!s.ghost.empty()||s.attempt.ghost)throw std::invalid_argument("Captured Hall recording already has encoded data");
        requireHallGhostBinding(*s.recording,s.attempt,c);
        const auto& g=*s.recording;
        if(g.samples.capacity()>maxGhostLapSamples||g.bridges.capacity()>maxGhostLapBridgeShapes)
            throw std::invalid_argument("Captured Hall recording exceeds allocation bound");
        for(const auto& b:g.bridges)if(b.cachedSections.capacity()>512)throw std::invalid_argument("Captured Hall bridge exceeds allocation bound");
        return; // Full codec validation and encoding run on the writer.
    }
    if(s.attempt.ghost.has_value()!=!s.ghost.empty())throw std::invalid_argument("Hall ghost reference and payload disagree");
    if(!s.ghost.empty()){
        if(s.ghost.size()>hallGhostMaximumBytes)throw std::invalid_argument("Hall ghost exceeds its bound");
        const auto ghost=decodeHallGhost(s.ghost);requireHallGhostBinding(ghost,s.attempt,c);
        if(digest(s.ghost)!=*s.attempt.ghost)throw std::invalid_argument("Hall ghost digest mismatch");
    }
}
bool same(const HallSubmission& a,const HallSubmission& b,HallCatalog c){
    HallOfFame h(c);h.offer(a.attempt);h.offer(b.attempt);return a.ghost==b.ghost&&a.recording==b.recording;
}
std::size_t submissionBytes(const HallSubmission& s){
    std::size_t bytes=128+s.ghost.size();if(!s.recording)return bytes;
    const auto& g=*s.recording;bytes+=sizeof(g)+g.samples.capacity()*sizeof(GhostLapSample)+g.bridges.capacity()*sizeof(GhostBridgeShape);
    for(const auto& b:g.bridges)bytes+=b.cachedSections.capacity()*sizeof(std::uint16_t);return bytes;
}
bool objectFilename(const std::string& name){
    if(name.size()!=43&&name.size()!=47)return false;
    if(name.substr(32)!=".scr-hghost"&&name.substr(32)!=".scr-hghost.tmp")return false;
    return std::all_of(name.begin(),name.begin()+32,[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
void collect(const fs::path& root,HallCatalog catalog){
    auto loaded=load(root,catalog);select(loaded,catalog);
    if(loaded.result.state==HallArchiveState::Unsupported||loaded.result.state==HallArchiveState::Corrupt)return;
    std::set<std::string> retained;
    for(const auto& g:loaded.copies){
        if(g.state==HallArchiveState::Corrupt)return; // Cannot inspect this generation's references safely.
        if(g.hall)for(const auto& board:g.hall->snapshot().boards)for(const auto& e:board)if(e.attempt.ghost)retained.insert(hallObjectName(e.attempt.id));
    }
    for(const auto& file:fs::directory_iterator(root/L"ghosts")){
        if(!file.is_regular_file())continue;const auto name=file.path().filename().string();
        if(objectFilename(name)&&!retained.count(name)&&!DeleteFileW(file.path().c_str()))failure("Cannot collect unreferenced Hall ghost");
    }
}
}
std::string hallObjectName(const HallAttemptId& id){
    std::ostringstream text;for(auto b:id)text<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return text.str()+".scr-hghost";
}
HallSubmission makeHallSubmission(HallAttempt attempt,const HallGhost* ghost){
    if(attempt.ghost)throw std::invalid_argument("Hall submission references must be derived from their recording");
HallSubmission result{std::move(attempt),{}, {}};result.attempt.name=normaliseHallName(result.attempt.name).display;
    if(ghost){requireHallGhostBinding(*ghost,result.attempt,ghost->catalog);result.ghost=encodeHallGhost(*ghost);result.attempt.ghost=digest(result.ghost);}
    return result;
}
HallSubmission captureHallSubmission(HallAttempt attempt,std::shared_ptr<const HallGhost> recording){
    HallSubmission s{std::move(attempt),{},std::move(recording)};s.attempt.name=normaliseHallName(s.attempt.name).display;
    if(s.attempt.ghost)throw std::invalid_argument("Live Hall capture must not supply a digest");
    return s;
}
SharedHallArchive::SharedHallArchive(fs::path root,HallCatalog catalog,HallWriteObserver observer)
    :root_(fs::absolute(std::move(root)).lexically_normal()),catalog_(catalog),observer_(std::move(observer)){
    (void)HallOfFame(catalog_);
}
HallArchiveRead SharedHallArchive::read()const{prepareSharedHallDirectory(root_);SharedHallLock lock(root_);auto l=load(root_,catalog_);select(l,catalog_);return l.result;}
HallGhostRead SharedHallArchive::readGhost(const HallAttempt& attempt)const{
    if(!attempt.ghost)return {};
    try{prepareSharedHallDirectory(root_);SharedHallLock lock(root_);auto bytes=readBytes(root_/L"ghosts"/hallObjectName(attempt.id),hallGhostMaximumBytes);if(!bytes)return {};
        if(digest(*bytes)!=*attempt.ghost)return {HallGhostState::Corrupt,{}};
        auto ghost=std::make_shared<const HallGhost>(decodeHallGhost(*bytes));requireHallGhostBinding(*ghost,attempt,catalog_);return {HallGhostState::Valid,std::move(ghost)};
    }catch(const HallIoError&){return {HallGhostState::Unavailable,{}};}
    catch(const UnsupportedHallGhost&){return {HallGhostState::Unsupported,{}};}
    catch(const std::length_error&){return {HallGhostState::Corrupt,{}};}
    catch(const std::exception&){return {HallGhostState::Corrupt,{}};}
}
std::uint64_t SharedHallArchive::commit(const std::vector<std::shared_ptr<const HallSubmission>>& submissions){
    std::vector<HallAttempt> attempts;attempts.reserve(submissions.size());
    for(const auto& s:submissions){if(!s)throw std::invalid_argument("Empty Hall submission");validateSubmission(*s,catalog_);
        attempts.push_back(s->recording?makeHallSubmission(s->attempt,s->recording.get()).attempt:s->attempt);}
    prepareSharedHallDirectory(root_);prepareSharedHallDirectory(root_/L"ghosts");
    SharedHallLock lock(root_);auto l=load(root_,catalog_);select(l,catalog_);
    if(!l.result.hall)throw std::runtime_error(l.result.state==HallArchiveState::Unsupported?"Shared Hall uses unsupported content, rules or format":"Shared Hall has no readable generation; existing files preserved");
    // Reclaim leftovers from an interrupted writer before creating more objects.
    // All surviving index generations are locked and protected by collect().
    collect(root_,catalog_);
    HallOfFame merged=*l.result.hall;bool changed=l.result.state==HallArchiveState::Recovered;
    for(const auto& a:attempts)changed|=merged.offer(a)==HallAdmission::Accepted;
    // Write only objects still referenced after this entire batch has merged.
    for(std::size_t n=0;n<submissions.size();++n){const auto& s=submissions[n];const auto& a=attempts[n];const auto& board=merged.entries(a.board);
        if(!a.ghost||std::none_of(board.begin(),board.end(),[&](const auto& e){return e.attempt.id==a.id;}))continue;
        const auto path=root_/L"ghosts"/hallObjectName(a.id);bool healthy=false;
        try{const auto old=readBytes(path,hallGhostMaximumBytes);healthy=old&&digest(*old)==*a.ghost;}catch(const std::length_error&){}
        if(!healthy){
            // Re-encode one retained capture at a time: bounded scratch instead
            // of holding a second full batch of encoded recordings in memory.
            const auto captured=s->recording?encodeHallGhost(*s->recording):HallBytes{};
            atomicWrite(path,s->recording?captured:s->ghost,observer_,HallWriteStage::GhostFlushed,HallWriteStage::GhostPublished);
        }
    }
    if(changed){
        if(l.result.generation==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Hall generation exhausted");
        // Never overwrite the sole newest recovery copy before it is duplicated.
        const auto backup1=[&]{if(!l.valid.empty())atomicWrite(indexPath(root_,1),l.valid.front()->bytes,observer_,HallWriteStage::BackupFlushed,HallWriteStage::BackupPublished);};
        const auto backup2=[&]{if(l.valid.empty())return;const auto* older=l.valid.front();
            for(const auto* g:l.valid)if(g->number<l.result.generation){older=g;break;}
            atomicWrite(indexPath(root_,2),older->bytes,observer_,HallWriteStage::BackupFlushed,HallWriteStage::BackupPublished);};
        if(!l.valid.empty()&&l.valid.front()->slot==2){backup1();backup2();}else{backup2();backup1();}
        atomicWrite(indexPath(root_,0),envelope(merged,l.result.generation+1),observer_,HallWriteStage::HeadFlushed,HallWriteStage::HeadPublished);
        ++l.result.generation;
    }
    collect(root_,catalog_);if(observer_)observer_(HallWriteStage::Collected);return l.result.generation;
}
SharedHallStore::SharedHallStore(fs::path root,HallCatalog catalog,HallWriteObserver observer,std::size_t limit)
    :archive_(std::move(root),catalog,std::move(observer)),catalog_(catalog),limit_(limit){
    if(!limit_||limit_>hallCapturePendingMaximumBytes)throw std::invalid_argument("Invalid Hall pending byte limit");
    worker_=std::thread([this]{run();});
}
SharedHallStore::~SharedHallStore(){
    flush();{std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}changed_.notify_one();worker_.join();
}
HallSubmitResult SharedHallStore::submit(HallSubmission submission){
    submission.attempt.name=normaliseHallName(submission.attempt.name).display;validateSubmission(submission,catalog_);
    std::lock_guard<std::mutex> lock(mutex_);
    for(const auto* list:{&pending_,&active_})for(const auto& s:*list)if(s->attempt.id==submission.attempt.id){
        if(!same(*s,submission,catalog_))throw std::invalid_argument("Hall retry changed its recording");return HallSubmitResult::Duplicate;}
    HallOfFame candidates(catalog_);for(const auto& s:pending_)candidates.offer(s->attempt);
    if(candidates.offer(submission.attempt)==HallAdmission::OutsideTable)return HallSubmitResult::OutsideTable;
    std::set<HallAttemptId> retained;for(const auto& board:candidates.snapshot().boards)for(const auto& e:board)retained.insert(e.attempt.id);
    std::size_t bytes=submissionBytes(submission);
    for(const auto& s:pending_)if(retained.count(s->attempt.id))bytes+=submissionBytes(*s);
    if(bytes>limit_)return HallSubmitResult::Full;
    pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[&](const auto& s){return !retained.count(s->attempt.id);}),pending_.end());
    pending_.push_back(std::make_shared<const HallSubmission>(std::move(submission)));changed_.notify_one();return HallSubmitResult::Queued;
}
void SharedHallStore::retry(){std::lock_guard<std::mutex> lock(mutex_);blocked_=false;error_.clear();checkRequested_=true;changed_.notify_one();}
bool SharedHallStore::flush(){std::unique_lock<std::mutex> lock(mutex_);idle_.wait(lock,[&]{return !writing_&&((pending_.empty()&&!checkRequested_)||blocked_);});return !blocked_&&pending_.empty()&&!checkRequested_;}
bool SharedHallStore::pending()const{std::lock_guard<std::mutex> lock(mutex_);return writing_||checkRequested_||!pending_.empty();}
bool SharedHallStore::failed()const{std::lock_guard<std::mutex> lock(mutex_);return blocked_;}
std::string SharedHallStore::error()const{std::lock_guard<std::mutex> lock(mutex_);return error_;}
std::uint64_t SharedHallStore::requestBrowser(HallBoardKey board,std::optional<HallAttemptId> challenge){
    (void)hallBoardIndex(board);std::lock_guard<std::mutex> lock(mutex_);
    browserRequest_=board;browserChallenge_=challenge;changed_.notify_one();return ++browserSerial_;
}
std::shared_ptr<const HallBrowserRead> SharedHallStore::browserRead()const{std::lock_guard<std::mutex> lock(mutex_);return browserRead_;}
void SharedHallStore::cancelBrowser(){std::lock_guard<std::mutex> lock(mutex_);browserRequest_.reset();++browserSerial_;}
void SharedHallStore::run(){
    for(;;){
        std::optional<HallBoardKey> board;std::optional<HallAttemptId> challenge;std::uint64_t request{};
        {std::unique_lock<std::mutex> lock(mutex_);changed_.wait(lock,[&]{return stopping_||browserRequest_||(!blocked_&&(!pending_.empty()||checkRequested_));});if(stopping_)return;
            // Publish pending attempts before reading a menu snapshot. A failed
            // write must not prevent browsing the last valid committed archive.
            if(!blocked_&&(!pending_.empty()||checkRequested_)){active_=pending_;checkRequested_=false;writing_=true;}
            else{board=browserRequest_;challenge=browserChallenge_;browserRequest_.reset();request=browserSerial_;}
        }
        if(board){
            auto result=std::make_shared<HallBrowserRead>();result->board=*board;result->request=request;result->challenge=challenge;
            try{result->archive=archive_.read();
                if(result->archive.hall){const auto& entries=result->archive.hall->entries(*board);
                    for(unsigned n=0;n<entries.size();++n){
                        {std::lock_guard<std::mutex> lock(mutex_);if(stopping_||request!=browserSerial_)break;}
                        const auto& attempt=entries[n].attempt;
                        const auto ghost=attempt.ghost?archive_.readGhost(attempt):HallGhostRead{};
                        result->ghosts[n]=ghost.state==HallGhostState::Valid;
                        if(challenge&&attempt.id==*challenge&&ghost.ghost)result->target=pinHallGhost(attempt,*ghost.ghost,catalog_);
                    }
                }
            }catch(const std::exception& e){result->error=e.what();}
            {std::lock_guard<std::mutex> lock(mutex_);if(request==browserSerial_)browserRead_=std::move(result);}
            continue;
        }
        std::string error;try{archive_.commit(active_);}catch(const std::exception& e){error=e.what();}
        {std::lock_guard<std::mutex> lock(mutex_);
            if(error.empty()){std::set<HallAttemptId> saved;for(const auto& s:active_)saved.insert(s->attempt.id);
                pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[&](const auto& s){return saved.count(s->attempt.id)!=0;}),pending_.end());}
            else{blocked_=true;error_=std::move(error);}
            active_.clear();writing_=false;idle_.notify_all();
        }
    }
}
} // namespace scr::runtime
