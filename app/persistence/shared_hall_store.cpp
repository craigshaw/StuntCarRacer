#include "persistence/shared_hall_store.h"
#include "persistence/hall_archive_engine.h"
#include <iomanip>
#include <sstream>

namespace scr::runtime {
namespace {
namespace fs=std::filesystem;
using namespace state::checkpoint;
using archive_detail::digest;
void validateOriginalSubmission(const HallSubmission& s,HallCatalog c){
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
struct OriginalArchiveTraits {
    using Model=HallOfFame;using Catalog=HallCatalog;using Attempt=HallAttempt;using Ghost=HallGhost;using Submission=HallSubmission;
    using UnsupportedTable=UnsupportedHallTable;using UnsupportedGhost=UnsupportedHallGhost;
    static constexpr auto accepted=HallAdmission::Accepted;
    static constexpr std::uint32_t indexMagic=0x58444948,indexVersion=1;
    static constexpr std::size_t tableMaximum=hallTableMaximumBytes,ghostMaximum=hallGhostMaximumBytes;
    static constexpr const wchar_t* indexName=L"index.scr-hall";
    static constexpr const char* ghostSuffix=".scr-hghost";
    static auto encodeTable(const Model& h){return encodeHallTable(h);}
    static auto decodeTable(const HallBytes& b,Catalog c){return decodeHallTable(b,c);}
    static auto encodeGhost(const Ghost& g){return encodeHallGhost(g);}
    static auto decodeGhost(const HallBytes& b){return decodeHallGhost(b);}
    static void requireBinding(const Ghost& g,const Attempt& a,Catalog c){requireHallGhostBinding(g,a,c);}
    static void validateSubmission(const Submission& s,Catalog c){validateOriginalSubmission(s,c);}
    static auto makeSubmission(Attempt a,const Ghost* g){return makeHallSubmission(std::move(a),g);}
    static auto objectName(const HallAttemptId& id){return hallObjectName(id);}
};
using OriginalEngine=archive_detail::HallArchiveEngine<OriginalArchiveTraits>;
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
HallArchiveRead SharedHallArchive::read()const{return OriginalEngine(root_,catalog_,observer_).read();}
HallGhostRead SharedHallArchive::readGhost(const HallAttempt& a)const{return OriginalEngine(root_,catalog_,observer_).readGhost(a);}
std::uint64_t SharedHallArchive::commit(const std::vector<std::shared_ptr<const HallSubmission>>& submissions){return OriginalEngine(root_,catalog_,observer_).commit(submissions);}
SharedHallStore::SharedHallStore(fs::path root,HallCatalog catalog,HallWriteObserver observer,std::size_t limit)
    :archive_(std::move(root),catalog,std::move(observer)),catalog_(catalog),limit_(limit){
    if(!limit_||limit_>hallCapturePendingMaximumBytes)throw std::invalid_argument("Invalid Hall pending byte limit");
    worker_=std::thread([this]{run();});
}
SharedHallStore::~SharedHallStore(){
    flush();{std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}changed_.notify_one();worker_.join();
}
HallSubmitResult SharedHallStore::submit(HallSubmission submission){
    submission.attempt.name=normaliseHallName(submission.attempt.name).display;validateOriginalSubmission(submission,catalog_);
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
