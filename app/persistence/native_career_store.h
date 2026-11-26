#pragma once
#include "persistence/enhanced_record_codec.h"
#include "state/native_checkpoint_io.h"
#include "persistence/saved_game_folders.h"
#include <array>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <functional>

namespace scr::runtime {
struct GhostLap;struct GhostLapKey;
// Immutable, bounded native payloads. All generated data stays outside source.
CareerBytes encodeCareerInfo(const NativeCareerInfo&,const CareerBytes& checkpoint);
NativeCareerInfo decodeCareerInfo(const CareerBytes&,CareerBytes* checkpoint=nullptr);

class NativeCareerStore {
public:
    using Validator=std::function<CareerFileState(const CareerBytes&)>;
    explicit NativeCareerStore(std::filesystem::path root,Validator validator={},std::filesystem::path settings={});
    ~NativeCareerStore();
    NativeCareerStore(const NativeCareerStore&)=delete;
    NativeCareerStore& operator=(const NativeCareerStore&)=delete;
    const std::filesystem::path& root()const noexcept{return root_;}
    static std::string newId();
    CareerRead readCareer(const std::string&)const;
    std::string lastUsed()const;
    std::vector<NativeCareerInfo> list()const;
    std::filesystem::path saveCareer(const NativeCareerInfo&,const CareerBytes&);
    void setLastUsed(const std::string&);
    CareerRead readGhost(const GhostLapKey&,std::uint64_t trackContentIdentity)const;
    void saveGhost(const GhostLap&);
    bool clearGhost(const GhostLapKey&);
    CareerRead readEnhancedBest(const EnhancedBestKey&,EnhancedCatalog)const;
    void saveEnhancedBest(const EnhancedBest&);
    bool clearEnhancedBest(const EnhancedBestKey&);
    void retry();
    bool flush();
    bool pending()const;
    bool failed()const;
    bool ghostFailed()const;
    bool enhancedBestFailed()const;
    std::string error()const;
    std::string failureSummary()const;
    void selectCareer(const NativeCareerInfo&);
    bool cancelCareerDraft(const NativeCareerInfo&);
    // Both modes share this actor's coalescing, retry and departure lifecycle.
    using CustomWriter=std::function<bool(const CareerBytes&,std::string&)>;
    void submitSavedGame(std::filesystem::path,CareerBytes,CustomWriter);
    void forgetWrites(const std::filesystem::path&);
    void acknowledgeFailures();
private:
    struct Job {std::filesystem::path path;unsigned kind{};CareerBytes data;std::uint64_t serial{};Validator validator;CustomWriter custom;};
    std::filesystem::path root_;
    Validator validator_;
    std::filesystem::path settings_;
    SavedGameFolders folders_;
    NativeCareerInfo selectedCareer_;
    std::filesystem::path recordsFolder(const std::string&)const;
    mutable std::mutex mutex_;
    std::condition_variable changed_,idle_;
    std::deque<Job> jobs_;
    std::map<std::filesystem::path,Job> latest_;
    std::map<std::filesystem::path,std::string> errors_;
    std::thread worker_;
    bool stopping_{},writing_{};std::uint64_t serial_{};
    std::filesystem::path careerPath(const std::string&)const;
    std::filesystem::path ghostPath(const GhostLapKey&)const;
    std::filesystem::path enhancedBestPath(const EnhancedBestKey&)const;
    bool clearRecord(const std::filesystem::path&);
    static CareerRead readFile(const std::filesystem::path&,unsigned kind,const Validator& validator={});
    static bool writeFile(const Job&,std::string& error,const Validator&);
    void submit(std::filesystem::path,unsigned,CareerBytes,Validator validator={});
    void run();
};
} // namespace scr::runtime
