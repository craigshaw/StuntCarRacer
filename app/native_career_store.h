#pragma once
#include "state/native_checkpoint_io.h"
#include <array>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <functional>

namespace scr::viewer {
using CareerBytes=state::checkpoint::Bytes;
using NativeHallBanks=std::array<std::uint8_t,512>;
enum class CareerFileState { Missing,Valid,Recovered,Corrupt,Unsupported };
struct CareerRead {
    CareerFileState state{CareerFileState::Missing};CareerBytes data;std::uint64_t generation{};
};
struct NativeCareerInfo {
    std::string id,name;unsigned division{},league{};std::uint64_t lastPlayed{};
    CareerFileState state{CareerFileState::Valid};
};
// Immutable, bounded native payloads. All generated data stays outside source.
CareerBytes encodeCareerInfo(const NativeCareerInfo&,const CareerBytes& checkpoint);
NativeCareerInfo decodeCareerInfo(const CareerBytes&,CareerBytes* checkpoint=nullptr);
bool mergeNativeHall(NativeHallBanks&,const NativeHallBanks&);
bool validNativeHall(const NativeHallBanks&)noexcept;

class NativeCareerStore {
public:
    using Validator=std::function<CareerFileState(const CareerBytes&)>;
    explicit NativeCareerStore(std::filesystem::path root,Validator validator={});
    ~NativeCareerStore();
    NativeCareerStore(const NativeCareerStore&)=delete;
    NativeCareerStore& operator=(const NativeCareerStore&)=delete;
    const std::filesystem::path& root()const noexcept{return root_;}
    static std::string newId();
    CareerRead readCareer(const std::string&)const;
    CareerRead readHall()const;
    std::string lastUsed()const;
    CareerFileState lastUsedState()const;
    std::vector<NativeCareerInfo> list()const;
    void saveCareer(const NativeCareerInfo&,const CareerBytes&);
    void saveHall(const NativeHallBanks&);
    void setLastUsed(const std::string&);
    void retry();
    bool flush();
    bool pending()const;
    bool failed()const;
    std::string error()const;
    std::string failureSummary()const;
    bool removeCareer(const std::string&);
    bool resetHall(const NativeHallBanks&);
    void acknowledgeFailures();
private:
    struct Job {std::filesystem::path path;unsigned kind{};CareerBytes data;std::uint64_t serial{};};
    std::filesystem::path root_;
    Validator validator_;
    mutable std::mutex mutex_;
    std::condition_variable changed_,idle_;
    std::deque<Job> jobs_;
    std::map<std::filesystem::path,Job> latest_;
    std::map<std::filesystem::path,std::string> errors_;
    std::thread worker_;
    bool stopping_{},writing_{};std::uint64_t serial_{};
    std::filesystem::path careerPath(const std::string&)const;
    static CareerRead readFile(const std::filesystem::path&,unsigned kind,const Validator& validator={});
    static bool writeFile(const Job&,std::string& error,const Validator&);
    void submit(std::filesystem::path,unsigned,CareerBytes);
    void run();
};
} // namespace scr::viewer
