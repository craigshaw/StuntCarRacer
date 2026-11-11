#include "persistence/enhanced_hall_store.h"
#include "persistence/hall_archive_engine.h"
#include <iomanip>
#include <sstream>

namespace scr::runtime {
namespace {
using archive_detail::digest;
void validateEnhancedSubmission(const EnhancedSubmission& s,EnhancedCatalog catalog){
    EnhancedHallOfFame single(catalog);if(single.offer(s.attempt)==EnhancedAdmission::Ineligible)throw std::invalid_argument("Unrankable Enhanced submission");
    if(s.recording){
        if(!s.ghost.empty()||s.attempt.ghost)throw std::invalid_argument("Enhanced capture already has encoded data");
        requireEnhancedGhostBinding(*s.recording,s.attempt,catalog);const auto& g=s.recording->recording;
        if(g.samples.capacity()>maxGhostLapSamples||g.bridges.capacity()>maxGhostLapBridgeShapes||g.recoveryOffsets.capacity()>maxGhostLapSamples)throw std::invalid_argument("Enhanced capture exceeds allocation bounds");
        for(const auto& b:g.bridges)if(b.cachedSections.capacity()>512)throw std::invalid_argument("Enhanced bridge exceeds allocation bound");
        return;
    }
    if(s.attempt.ghost.has_value()!=!s.ghost.empty())throw std::invalid_argument("Enhanced reference and data disagree");
    if(!s.ghost.empty()){if(s.ghost.size()>enhancedHallGhostMaximumBytes||s.ghost.capacity()>enhancedEncodedAllocationMaximumBytes)throw std::invalid_argument("Enhanced ghost exceeds allocation bound");
        requireEnhancedGhostBinding(decodeEnhancedHallGhost(s.ghost),s.attempt,catalog);if(digest(s.ghost)!=*s.attempt.ghost)throw std::invalid_argument("Enhanced ghost digest mismatch");}
}
struct EnhancedArchiveTraits {
    using Model=EnhancedHallOfFame;using Catalog=EnhancedCatalog;using Attempt=EnhancedHallAttempt;using Ghost=EnhancedHallGhost;using Submission=EnhancedSubmission;
    using UnsupportedTable=UnsupportedEnhancedRecords;using UnsupportedGhost=UnsupportedEnhancedRecords;
    static constexpr auto accepted=EnhancedAdmission::Accepted;
    static constexpr std::uint32_t indexMagic=0x58444945,indexVersion=1;
    static constexpr std::size_t tableMaximum=enhancedHallTableMaximumBytes,ghostMaximum=enhancedHallGhostMaximumBytes;
    static constexpr const wchar_t* indexName=L"index.scr-ehall";
    static constexpr const char* ghostSuffix=".scr-eghost";
    static auto encodeTable(const Model& h){return encodeEnhancedHall(h);}
    static auto decodeTable(const HallBytes& b,Catalog c){return decodeEnhancedHall(b,c);}
    static auto encodeGhost(const Ghost& g){return encodeEnhancedHallGhost(g);}
    static auto decodeGhost(const HallBytes& b){return decodeEnhancedHallGhost(b);}
    static void requireBinding(const Ghost& g,const Attempt& a,Catalog c){requireEnhancedGhostBinding(g,a,c);}
    static void validateSubmission(const Submission& s,Catalog c){validateEnhancedSubmission(s,c);}
    static auto makeSubmission(Attempt a,const Ghost* g){return makeEnhancedSubmission(std::move(a),g);}
    static auto objectName(const HallAttemptId& id){return enhancedObjectName(id);}
};
using Engine=archive_detail::HallArchiveEngine<EnhancedArchiveTraits>;
std::size_t size(const EnhancedSubmission& s){
    std::size_t bytes=256+s.ghost.capacity();if(s.recording){const auto& g=s.recording->recording;
        bytes+=sizeof(EnhancedHallGhost)+g.samples.capacity()*sizeof(GhostLapSample)+g.bridges.capacity()*sizeof(GhostBridgeShape)+g.recoveryOffsets.capacity()*sizeof(std::int64_t);
        for(const auto& b:g.bridges)bytes+=b.cachedSections.capacity()*sizeof(std::uint16_t);}return bytes;
}
bool same(const EnhancedSubmission& a,const EnhancedSubmission& b,EnhancedCatalog catalog){EnhancedHallOfFame h(catalog);h.offer(a.attempt);h.offer(b.attempt);return a.ghost==b.ghost&&a.recording==b.recording;}
}
std::filesystem::path enhancedHallRoot(const std::filesystem::path& originalRoot){
    if(originalRoot.empty())throw std::invalid_argument("Enhanced Hall needs an Original root");
    auto root=std::filesystem::absolute(originalRoot).lexically_normal();
    if(root.filename().empty()&&root.has_relative_path())root=root.parent_path();
    if(root.filename().empty()||root.filename()==L".")throw std::invalid_argument("Enhanced Hall needs a named Original root");
    return root.parent_path()/(root.filename().wstring()+L"-enhanced-v1");
}
std::string enhancedObjectName(const HallAttemptId& id){std::ostringstream out;for(auto byte:id)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);return out.str()+".scr-eghost";}
HallAttemptId enhancedCompletionId(const HallAttemptId& run,std::uint64_t crossing,HallRecordKind kind,unsigned method){
    if(!method)throw std::invalid_argument("Missing Enhanced timing method");
    state::checkpoint::Writer writer;writer.text("SCR Enhanced timing");writer.u32(method);writer.bytes(run.data(),run.size());
    const auto hash=digest(writer.data);HallAttemptId salted{};std::copy_n(hash.begin(),salted.size(),salted.begin());return hallCompletionId(salted,crossing,kind);
}
EnhancedSubmission makeEnhancedSubmission(EnhancedHallAttempt attempt,const EnhancedHallGhost* ghost){
    if(attempt.ghost)throw std::invalid_argument("Enhanced references must come from a recording");
    EnhancedSubmission result{std::move(attempt),{}, {}};result.attempt.name=normaliseHallName(result.attempt.name).display;
    if(ghost){requireEnhancedGhostBinding(*ghost,result.attempt,ghost->catalog);result.ghost=encodeEnhancedHallGhost(*ghost);result.attempt.ghost=digest(result.ghost);}return result;
}
EnhancedSubmission captureEnhancedSubmission(EnhancedHallAttempt attempt,std::shared_ptr<const EnhancedHallGhost> recording){
    if(attempt.ghost)throw std::invalid_argument("Enhanced live capture supplied a digest");
    attempt.name=normaliseHallName(attempt.name).display;return {std::move(attempt),{},std::move(recording)};
}
EnhancedHallArchive::EnhancedHallArchive(std::filesystem::path root,EnhancedCatalog catalog,HallWriteObserver observer)
    :root_(enhancedHallRoot(root)),catalog_(catalog),observer_(std::move(observer)){validateEnhancedCatalog(catalog_);}
EnhancedHallRead EnhancedHallArchive::read()const{return Engine(root_,catalog_,observer_).read();}
EnhancedGhostRead EnhancedHallArchive::readGhost(const EnhancedHallAttempt& a)const{return Engine(root_,catalog_,observer_).readGhost(a);}
std::uint64_t EnhancedHallArchive::commit(const std::vector<std::shared_ptr<const EnhancedSubmission>>& submissions){return Engine(root_,catalog_,observer_).commit(submissions);}
EnhancedHallStore::EnhancedHallStore(std::filesystem::path root,EnhancedCatalog catalog,HallWriteObserver observer,std::size_t limit)
    :archive_(std::move(root),catalog,std::move(observer)),catalog_(catalog),limit_(limit){
    if(!limit||limit>enhancedCapturePendingMaximumBytes)throw std::invalid_argument("Invalid Enhanced pending bound");worker_=std::thread([this]{run();});}
EnhancedHallStore::~EnhancedHallStore(){flush();{std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}changed_.notify_one();worker_.join();}
EnhancedSubmitResult EnhancedHallStore::submit(EnhancedSubmission submission){
    if(!submission.attempt.time.rankable())return EnhancedSubmitResult::Ineligible;
    submission.attempt.name=normaliseHallName(submission.attempt.name).display;validateEnhancedSubmission(submission,catalog_);
    std::lock_guard<std::mutex> lock(mutex_);
    for(const auto* list:{&pending_,&active_})for(const auto& item:*list)if(item->attempt.id==submission.attempt.id){if(!same(*item,submission,catalog_))throw std::invalid_argument("Enhanced retry changed recording");return EnhancedSubmitResult::Duplicate;}
    EnhancedHallOfFame candidates(catalog_);for(const auto& item:pending_)candidates.offer(item->attempt);
    if(candidates.offer(submission.attempt)==EnhancedAdmission::OutsideTable)return EnhancedSubmitResult::OutsideTable;
    std::set<HallAttemptId> retained;for(const auto& board:candidates.snapshot().boards)for(const auto& e:board)retained.insert(e.attempt.id);
    auto bytes=size(submission);for(const auto& item:pending_)if(retained.count(item->attempt.id))bytes+=size(*item);
    if(bytes>limit_)return EnhancedSubmitResult::Full;
    pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[&](const auto& item){return !retained.count(item->attempt.id);}),pending_.end());
    pending_.push_back(std::make_shared<const EnhancedSubmission>(std::move(submission)));changed_.notify_one();return EnhancedSubmitResult::Queued;
}
void EnhancedHallStore::retry(){std::lock_guard<std::mutex> lock(mutex_);blocked_=false;error_.clear();changed_.notify_one();}
bool EnhancedHallStore::flush(){std::unique_lock<std::mutex> lock(mutex_);idle_.wait(lock,[&]{return !writing_&&(pending_.empty()||blocked_);});return !blocked_;}
bool EnhancedHallStore::pending()const{std::lock_guard<std::mutex> lock(mutex_);return writing_||!pending_.empty();}
bool EnhancedHallStore::failed()const{std::lock_guard<std::mutex> lock(mutex_);return blocked_;}
std::string EnhancedHallStore::error()const{std::lock_guard<std::mutex> lock(mutex_);return error_;}
void EnhancedHallStore::run(){for(;;){
    {std::unique_lock<std::mutex> lock(mutex_);changed_.wait(lock,[&]{return stopping_||(!blocked_&&!pending_.empty());});if(stopping_)return;active_=pending_;writing_=true;}
    std::string error;try{archive_.commit(active_);}catch(const std::exception& e){error=e.what();}
    {std::lock_guard<std::mutex> lock(mutex_);if(error.empty()){std::set<HallAttemptId> saved;for(const auto& item:active_)saved.insert(item->attempt.id);
        pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[&](const auto& item){return saved.count(item->attempt.id)!=0;}),pending_.end());}
        else{blocked_=true;error_=std::move(error);}active_.clear();writing_=false;idle_.notify_all();}
}}
} // namespace scr::runtime
