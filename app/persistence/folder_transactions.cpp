#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "persistence/folder_transactions.h"
#include "persistence/native_career_store.h"
#include "persistence/player_data_layout.h"
#include "persistence/career_file_io.h"
#include "persistence/windows_file_path.h"
#include "persistence/player_preferences.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <system_error>

namespace scr::runtime {
namespace {
namespace fs=std::filesystem;
using namespace state::checkpoint;
constexpr std::uint32_t magic=0x32585446,version=1;
constexpr std::size_t maximumFiles=4096;
constexpr std::uint64_t maximumBytes=2ULL*1024*1024*1024;
struct File {std::string name;std::uint64_t bytes{},hash{};};
struct Plan {std::string target;std::vector<std::string> retire;std::vector<File> files;};
[[noreturn]]void failed(const char* operation){throw std::system_error(int(GetLastError()),std::system_category(),operation);}
bool safeName(const std::string& value){
    return !value.empty()&&value.size()<=255&&value!="."&&value!=".."&&value.back()!='.'&&value.back()!=' '&&
        std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32||std::string("<>:\"/\\|?*").find(char(c))!=std::string::npos;});
}
fs::path relativePath(const std::string& text,bool category){
    const auto path=fs::u8path(text);if(path.empty()||path.is_absolute()||path.has_root_name())throw std::runtime_error("Invalid folder transaction path");
    std::size_t count=0;for(const auto& part:path){if(!safeName(part.u8string()))throw std::runtime_error("Unsafe folder transaction path");++count;}
    if(category&&(count!=2||(*path.begin()!="careers"&&*path.begin()!="multiplayer")))throw std::runtime_error("Transaction is outside saved-game folders");
    return path;
}
void ordinaryDirectory(const fs::path& path){
    if(FolderTransactions::directoryLink(path))throw std::runtime_error("Saved-game directory links are not followed");
    if(!fs::is_directory(windowsFilePath(path)))throw std::runtime_error("Saved-game folder is unavailable");
}
std::uint64_t fileHash(const fs::path& path){
    std::ifstream file(windowsFilePath(path),std::ios::binary);if(!file)throw std::runtime_error("Cannot read folder transaction file");
    std::array<char,65536> buffer{};std::uint64_t result=14695981039346656037ULL;
    while(file){file.read(buffer.data(),std::streamsize(buffer.size()));for(std::streamsize n=0;n<file.gcount();++n){result^=std::uint8_t(buffer[std::size_t(n)]);result*=1099511628211ULL;}}
    if(!file.eof())throw std::runtime_error("Cannot finish reading folder transaction file");return result;
}
std::vector<File> manifest(const fs::path& root){
    ordinaryDirectory(root);std::vector<File> result;std::uint64_t total=0;
    const auto directory=windowsFilePath(root);
    for(const auto& entry:fs::recursive_directory_iterator(directory)){
        if(FolderTransactions::directoryLink(entry.path()))throw std::runtime_error("Directory links are not copied");
        if(entry.is_directory())continue;
        if(!entry.is_regular_file())throw std::runtime_error("Unsupported saved-game file type");
        const auto bytes=entry.file_size();if(bytes>maximumBytes||total>maximumBytes-bytes||result.size()>=maximumFiles)throw std::runtime_error("Saved-game folder exceeds transaction limits");
        total+=bytes;const auto name=entry.path().lexically_relative(directory).generic_u8string();(void)relativePath(name,false);
        result.push_back({name,bytes,fileHash(entry.path())});
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.name<b.name;});return result;
}
void write(const fs::path& path,const Bytes& bytes){std::string error;if(!career_files::atomicWrite(path,bytes,error))throw std::runtime_error(error);}
Bytes bytes(const fs::path& path){
    if(FolderTransactions::directoryLink(path))throw std::runtime_error("Transaction file links are not followed");
    const auto size=fs::file_size(path);if(size>8*1024*1024)throw std::runtime_error("Folder transaction journal exceeds limit");
    std::ifstream file(windowsFilePath(path),std::ios::binary);Bytes result(std::size_t(size),0);file.read(reinterpret_cast<char*>(result.data()),std::streamsize(result.size()));
    if(!file)throw std::runtime_error("Cannot read folder transaction journal");return result;
}
Bytes encode(const Plan& plan){
    Writer w;w.u32(magic);w.u32(version);w.text(plan.target);w.u32(unsigned(plan.retire.size()));for(const auto& path:plan.retire)w.text(path);
    w.u32(unsigned(plan.files.size()));for(const auto& f:plan.files){w.text(f.name);w.u64(f.bytes);w.u64(f.hash);}w.u64(hash(w.data.data(),w.data.size()));return std::move(w.data);
}
Plan decode(const Bytes& data){
    Reader r{data};if(r.u32()!=magic||r.u32()!=version)throw std::runtime_error("Unsupported folder transaction journal; files preserved");
    Plan plan;plan.target=r.text(1024);if(!plan.target.empty())(void)relativePath(plan.target,true);
    const auto count=r.u32();if(count>256)throw std::runtime_error("Too many retired saved-game folders");
    std::set<std::string> paths;
    for(unsigned n=0;n<count;++n){auto path=r.text(1024);(void)relativePath(path,true);if(!paths.insert(path).second)throw std::runtime_error("Duplicate transaction folder");plan.retire.push_back(std::move(path));}
    const auto files=r.u32();if(files>maximumFiles)throw std::runtime_error("Too many transaction files");
    std::uint64_t total=0;paths.clear();
    for(unsigned n=0;n<files;++n){File f;f.name=r.text(4096);(void)relativePath(f.name,false);f.bytes=r.u64();f.hash=r.u64();
        if(!paths.insert(f.name).second||f.bytes>maximumBytes||total>maximumBytes-f.bytes)throw std::runtime_error("Invalid transaction manifest");total+=f.bytes;plan.files.push_back(std::move(f));}
    const auto end=r.position;if(r.u64()!=hash(data.data(),end))throw std::runtime_error("Damaged folder transaction journal; files preserved");r.end();
    if(plan.target.empty()&&(!plan.files.empty()||plan.retire.empty()))throw std::runtime_error("Invalid folder deletion journal");return plan;
}
void verify(const fs::path& folder,const std::vector<File>& expected){
    const auto actual=manifest(folder);if(actual.size()!=expected.size())throw std::runtime_error("Staged saved-game contents changed; files preserved");
    for(std::size_t n=0;n<actual.size();++n)if(actual[n].name!=expected[n].name||actual[n].bytes!=expected[n].bytes||actual[n].hash!=expected[n].hash)
        throw std::runtime_error("Staged saved-game checksum differs; files preserved");
}
void move(const fs::path& from,const fs::path& to){
    ordinaryDirectory(from);if(fs::exists(to))throw std::runtime_error("Saved-game transaction destination already exists");
    fs::create_directories(to.parent_path());if(!MoveFileExW(windowsFilePath(from).c_str(),windowsFilePath(to).c_str(),MOVEFILE_WRITE_THROUGH))failed("Cannot publish saved-game folder");
}
}
bool FolderTransactions::directoryLink(const fs::path& path){const auto attributes=GetFileAttributesW(windowsFilePath(path).c_str());return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;}
FolderTransactions::FolderTransactions(fs::path root,FolderTransactionObserver observer):root_(playerDataLayout(root).root),work_(root_/L".transactions"),observer_(std::move(observer)){}
std::string FolderTransactions::relative(const fs::path& path)const{
    const auto absolute=fs::absolute(path).lexically_normal().make_preferred();const auto result=absolute.lexically_relative(root_).generic_u8string();
    const auto checked=relativePath(result,true);if((root_/checked).make_preferred()!=absolute)throw std::runtime_error("Saved-game folder is outside its data root");
    if(directoryLink(absolute.parent_path())||directoryLink(absolute))throw std::runtime_error("Saved-game directory links are not followed");return result;
}
void FolderTransactions::copy(const fs::path& source,const fs::path& destination){
    ordinaryDirectory(source);
    const auto from=windowsFilePath(source),to=windowsFilePath(destination);
    const auto relative=fs::weakly_canonical(to).lexically_relative(fs::weakly_canonical(from));
    if(!relative.empty()&&*relative.begin()!=L"..")throw std::runtime_error("Cannot copy a saved-game folder inside itself");
    const auto files=manifest(source);fs::create_directories(to);
    for(const auto& entry:fs::recursive_directory_iterator(from))if(entry.is_directory())fs::create_directories(windowsFilePath(to/entry.path().lexically_relative(from)));
    for(const auto& f:files){const auto target=windowsFilePath(to/fs::u8path(f.name));fs::create_directories(target.parent_path());
        if(!CopyFileW(windowsFilePath(source/fs::u8path(f.name)).c_str(),windowsFilePath(target).c_str(),TRUE))failed("Cannot copy saved-game file");
        const auto file=CreateFileW(windowsFilePath(target).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)failed("Cannot flush copied saved-game file");const bool flushed=FlushFileBuffers(file)!=0;const auto error=GetLastError();CloseHandle(file);
        if(!flushed)throw std::system_error(int(error),std::system_category(),"Cannot flush saved-game copy");
    }
}
void FolderTransactions::publish(const fs::path& target,const std::vector<fs::path>& retire,const std::function<void(const fs::path&)>& build){
    recover();Plan plan;plan.target=relative(target);for(const auto& path:retire){ordinaryDirectory(path);plan.retire.push_back(relative(path));}
    if(fs::exists(target)&&std::find(plan.retire.begin(),plan.retire.end(),plan.target)==plan.retire.end())throw std::runtime_error("Cannot replace an unrelated saved-game folder");
    if(directoryLink(work_))throw std::runtime_error("Transaction directory links are not followed");
    const auto transaction=work_/NativeCareerStore::newId(),stage=transaction/L"new";fs::create_directories(stage);
    build(stage);const auto id=transaction.filename().string();write(stage/L".publish.scr-folder",Bytes(id.begin(),id.end()));
    plan.files=manifest(stage);write(transaction/L"journal.scr-transaction",encode(plan));if(observer_)observer_(FolderTransactionStage::Prepared);finish(transaction);
}
void FolderTransactions::erase(const fs::path& folder){
    recover();ordinaryDirectory(folder);Plan plan;plan.retire.push_back(relative(folder));
    if(directoryLink(work_))throw std::runtime_error("Transaction directory links are not followed");
    const auto transaction=work_/NativeCareerStore::newId();fs::create_directories(transaction);
    write(transaction/L"journal.scr-transaction",encode(plan));if(observer_)observer_(FolderTransactionStage::Prepared);finish(transaction);
}
namespace {
bool collectionName(const std::string& name){return name=="assets"||name=="careers"||name=="multiplayer"||name=="hall-of-fame"||name=="migration-recovery"||name=="settings.ini"||name=="migration.scr-import";}
bool collectionFile(const std::string& name){return name=="settings.ini"||name=="migration.scr-import";}
struct Collection {std::vector<std::pair<std::string,bool>> entries;std::vector<File> files;};
Bytes encodeCollection(const Collection& plan){
    Writer w;w.u32(magic);w.u32(2);w.u32(unsigned(plan.entries.size()));for(const auto& e:plan.entries){w.text(e.first);w.u32(e.second);}
    w.u32(unsigned(plan.files.size()));for(const auto& f:plan.files){w.text(f.name);w.u64(f.bytes);w.u64(f.hash);}w.u64(hash(w.data.data(),w.data.size()));return std::move(w.data);
}
Collection decodeCollection(const Bytes& data){
    Reader r{data};if(r.u32()!=magic||r.u32()!=2)throw std::runtime_error("Unsupported import transaction; files preserved");Collection plan;const auto count=r.u32();
    if(!count||count>7)throw std::runtime_error("Invalid import entry count");std::set<std::string> names;
    for(unsigned n=0;n<count;++n){auto name=r.text(64);const auto existed=r.u32();if(!collectionName(name)||!names.insert(name).second||existed>1)throw std::runtime_error("Unsafe import destination");plan.entries.push_back({name,existed!=0});}
    const auto files=r.u32();if(files>maximumFiles)throw std::runtime_error("Import has too many files");std::uint64_t total=0;std::set<std::string> paths;
    for(unsigned n=0;n<files;++n){File f;f.name=r.text(4096);(void)relativePath(f.name,false);const auto first=fs::u8path(f.name).begin()->u8string();f.bytes=r.u64();f.hash=r.u64();
        if(!names.count(first)||!paths.insert(f.name).second||f.bytes>maximumBytes||total>maximumBytes-f.bytes)throw std::runtime_error("Invalid import manifest");total+=f.bytes;plan.files.push_back(std::move(f));}
    const auto end=r.position;if(r.u64()!=hash(data.data(),end))throw std::runtime_error("Damaged import transaction; files preserved");r.end();return plan;
}
void verifyCollectionEntry(const fs::path& path,const std::string& name,const std::vector<File>& files){
    if(FolderTransactions::directoryLink(path))throw std::runtime_error("Import directory links are not followed");
    if(collectionFile(name)){
        const auto expected=std::find_if(files.begin(),files.end(),[&](const auto& f){return f.name==name;});
        if(expected==files.end()||!fs::is_regular_file(path)||fs::file_size(path)!=expected->bytes||fileHash(path)!=expected->hash)throw std::runtime_error("Imported file changed; files preserved");
    }else{std::vector<File> expected;for(const auto& f:files)if(f.name.rfind(name+"/",0)==0)expected.push_back({f.name.substr(name.size()+1),f.bytes,f.hash});verify(path,expected);}
}
void moveCollectionEntry(const fs::path& from,const fs::path& to){
    if(FolderTransactions::directoryLink(from)||fs::exists(to))throw std::runtime_error("Unsafe import publication");
    fs::create_directories(to.parent_path());if(!MoveFileExW(windowsFilePath(from).c_str(),windowsFilePath(to).c_str(),MOVEFILE_WRITE_THROUGH))failed("Cannot publish imported data");
}
}
void FolderTransactions::publishCollection(const std::function<void(const fs::path&)>& build){
    recover();if(directoryLink(work_))throw std::runtime_error("Transaction directory links are not followed");
    const auto tx=work_/NativeCareerStore::newId(),stage=tx/L"new";fs::create_directories(stage);
    try{build(stage);}catch(...){(void)manifest(tx);fs::remove_all(windowsFilePath(tx));throw;}Collection plan;
    for(const auto& entry:fs::directory_iterator(stage)){const auto name=entry.path().filename().u8string();
        if(!collectionName(name)||directoryLink(entry.path())||collectionFile(name)!=entry.is_regular_file())throw std::runtime_error("Invalid staged import entry");
        if(directoryLink(root_/entry.path().filename()))throw std::runtime_error("Import destination links are not followed");plan.entries.push_back({name,fs::exists(root_/entry.path().filename())});}
    if(plan.entries.empty())throw std::runtime_error("An import needs at least one managed entry");
    std::sort(plan.entries.begin(),plan.entries.end());plan.files=manifest(stage);write(tx/L"collection.scr-transaction",encodeCollection(plan));if(observer_)observer_(FolderTransactionStage::Prepared);finishCollection(tx);
}
void FolderTransactions::finishCollection(const fs::path& tx){
    if(tx.parent_path()!=work_||!validPlayerIdentity(tx.filename().string()))throw std::runtime_error("Invalid import transaction directory");ordinaryDirectory(tx);
    const auto plan=decodeCollection(bytes(tx/L"collection.scr-transaction"));const auto committed=tx/L"committed.scr-transaction";
    if(directoryLink(tx/L"new")||directoryLink(tx/L"old"))throw std::runtime_error("Import staging links are not followed");
    if(fs::exists(committed)&&bytes(committed)!=Bytes{1})throw std::runtime_error("Damaged import commit marker");
    for(const auto& e:plan.entries){const auto name=fs::u8path(e.first),target=root_/name,pending=tx/L"new"/name,old=tx/L"old"/name;
        if(directoryLink(target)||directoryLink(pending)||directoryLink(old))throw std::runtime_error("Import links are not followed");
        if(!fs::exists(committed)&&fs::exists(pending)){
            verifyCollectionEntry(pending,e.first,plan.files);
            if(e.second&&!fs::exists(old)){if(!fs::exists(target))throw std::runtime_error("An import destination is missing; files preserved");moveCollectionEntry(target,old);if(observer_)observer_(FolderTransactionStage::Parked);}
            moveCollectionEntry(pending,target);if(observer_)observer_(FolderTransactionStage::Published);
        }
        verifyCollectionEntry(target,e.first,plan.files);
    }
    if(!fs::exists(committed)){write(committed,Bytes{1});if(observer_)observer_(FolderTransactionStage::Committed);}
    (void)manifest(tx);fs::remove_all(windowsFilePath(tx));
}
void FolderTransactions::finish(const fs::path& transaction){
    if(transaction.parent_path()!=work_||!validPlayerIdentity(transaction.filename().string()))throw std::runtime_error("Invalid transaction directory");
    ordinaryDirectory(transaction);const auto plan=decode(bytes(transaction/L"journal.scr-transaction"));
    if(directoryLink(transaction/L"new")||directoryLink(transaction/L"old"))throw std::runtime_error("Transaction staging links are not followed");
    const auto committed=transaction/L"committed.scr-transaction",stage=transaction/L"new";
    const auto target=plan.target.empty()?fs::path{}:(root_/relativePath(plan.target,true)).make_preferred();
    if(fs::exists(committed)&&bytes(committed)!=Bytes{1})throw std::runtime_error("Damaged folder commit marker; files preserved");
    if(fs::exists(committed)&&!target.empty()){
        auto expected=plan.files;if(!fs::exists(target/L".publish.scr-folder"))expected.erase(std::remove_if(expected.begin(),expected.end(),[](const auto& file){return file.name==".publish.scr-folder";}),expected.end());
        verify(target,expected);
    }
    if(!fs::exists(committed)){
        if(!target.empty())verify(fs::exists(stage)?stage:target,plan.files);
        for(std::size_t n=0;n<plan.retire.size();++n){const auto source=(root_/relativePath(plan.retire[n],true)).make_preferred(),parked=transaction/L"old"/std::to_string(n);
            (void)relative(source);
            if(fs::exists(parked)){ordinaryDirectory(parked);continue;}
            if(!fs::exists(source))throw std::runtime_error("A transaction source is missing; files preserved");move(source,parked);
            if(observer_)observer_(FolderTransactionStage::Parked);
        }
        if(!target.empty()&&fs::exists(stage))move(stage,target);
        if(observer_)observer_(FolderTransactionStage::Published);
        write(committed,Bytes{1});if(observer_)observer_(FolderTransactionStage::Committed);
    }
    if(!target.empty()){
        ordinaryDirectory(target);const auto marker=target/L".publish.scr-folder";
        if(fs::exists(marker)){const auto id=bytes(marker);if(std::string(id.begin(),id.end())!=transaction.filename().string())throw std::runtime_error("Transaction publication identity differs");fs::remove(marker);}
    }
    // Cleanup is confined to this verified, inert transaction directory. Reject
    // reparse points before recursive removal so no external tree is traversed.
    (void)manifest(transaction);fs::remove_all(windowsFilePath(transaction));
}
bool FolderTransactions::recover(){
    if(!fs::exists(work_))return false;ordinaryDirectory(work_);
    std::vector<fs::path> pending;
    for(const auto& entry:fs::directory_iterator(work_)){
        if(directoryLink(entry.path()))throw std::runtime_error("Transaction directory link found; files preserved");
        if(entry.is_directory()&&validPlayerIdentity(entry.path().filename().string())&&(fs::exists(entry.path()/L"journal.scr-transaction")||fs::exists(entry.path()/L"collection.scr-transaction")))pending.push_back(entry.path());
    }
    std::sort(pending.begin(),pending.end());for(const auto& path:pending){const bool collection=fs::exists(path/L"collection.scr-transaction");
        if(collection&&fs::exists(path/L"journal.scr-transaction"))throw std::runtime_error("Ambiguous transaction; files preserved");if(collection)finishCollection(path);else finish(path);}return !pending.empty();
}
} // namespace scr::runtime
