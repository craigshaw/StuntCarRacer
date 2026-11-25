#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "persistence/player_data_import.h"
#include "persistence/career_file_io.h"
#include "persistence/native_career_checkpoint.h"
#include "persistence/native_multiplayer_checkpoint.h"
#include "persistence/ghost_lap_codec.h"
#include "persistence/shared_hall_store.h"
#include "persistence/hall_ghost_codec.h"
#include "persistence/enhanced_hall_store.h"
#include "persistence/player_preferences.h"
#include "persistence/windows_file_path.h"
#include "assets/local_asset_pack.h"
#include "assets/native_startup.h"
#include "menus/native_opening_flow.h"
#include "launcher/native_display_settings.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace scr::runtime {
namespace fs=std::filesystem;using namespace state::checkpoint;
namespace {
constexpr std::uint32_t importMagic=0x504d4953;
struct NoImportChanges {};
bool sourceTransactions(const fs::path& root){
    const auto work=root/L".transactions";if(FolderTransactions::directoryLink(work))throw std::runtime_error("Import transaction links are not followed");
    if(!fs::is_directory(work))return false;for(const auto& e:fs::directory_iterator(work)){
        if(FolderTransactions::directoryLink(e.path()))throw std::runtime_error("Import transaction links are not followed");
        if(e.is_directory()&&validPlayerIdentity(e.path().filename().u8string())&&(fs::exists(e.path()/L"journal.scr-transaction")||fs::exists(e.path()/L"collection.scr-transaction")))return true;
    }return false;
}
Bytes read(const fs::path& path,std::size_t maximum=4*1024*1024){
    if(FolderTransactions::directoryLink(path))throw std::runtime_error("Import file links are not followed");
    const auto size=fs::file_size(path);if(size>maximum)throw std::runtime_error("Import file exceeds its bound");
    std::ifstream file(windowsFilePath(path),std::ios::binary);Bytes data(std::size_t(size),0);file.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size()));
    if(!file)throw std::runtime_error("Cannot read import source");return data;
}
void write(const fs::path& path,const Bytes& bytes){std::string error;if(!career_files::atomicWrite(path,bytes,error))throw std::runtime_error(error);}
void copyFile(const fs::path& source,const fs::path& target){
    if(FolderTransactions::directoryLink(source))throw std::runtime_error("Import links are not followed");
    fs::create_directories(windowsFilePath(target.parent_path()));if(!CopyFileW(windowsFilePath(source).c_str(),windowsFilePath(target).c_str(),TRUE))throw std::runtime_error("Cannot copy import file");
    // Flush the copied contents before publishing any collection entry.
    const auto file=CreateFileW(windowsFilePath(target).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot flush imported file");const bool okay=FlushFileBuffers(file)!=0;CloseHandle(file);if(!okay)throw std::runtime_error("Cannot flush imported file");
}
void copyImported(const fs::path& source,const fs::path& target){if(fs::is_directory(source))FolderTransactions::copy(source,target);else copyFile(source,target);}
std::set<std::string> receipt(const fs::path& path){
    if(!fs::exists(path))return {};const auto bytes=read(path,1024*1024);Reader r{bytes};if(r.u32()!=importMagic||r.u32()!=1)throw std::runtime_error("Unsupported migration record; data preserved");
    const auto count=r.u32();if(count>8192)throw std::runtime_error("Migration record exceeds its bound");std::set<std::string> result;
    for(unsigned n=0;n<count;++n)if(!result.insert(r.text(160)).second)throw std::runtime_error("Invalid migration record");
    const auto end=r.position;if(r.u64()!=hash(bytes.data(),end))throw std::runtime_error("Damaged migration record; data preserved");r.end();return result;
}
Bytes encodeReceipt(const std::set<std::string>& keys){
    if(keys.size()>8192)throw std::runtime_error("Migration record is full");Writer w;w.u32(importMagic);w.u32(1);w.u32(unsigned(keys.size()));for(const auto& key:keys)w.text(key);w.u64(hash(w.data.data(),w.data.size()));return std::move(w.data);
}
std::string fingerprint(const fs::path& folder){
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE digest{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot hash import contents");
    try{
        if(BCryptCreateHash(algorithm,&digest,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("Cannot hash import contents");
        const auto directory=windowsFilePath(folder);std::vector<fs::path> files;if(fs::is_directory(directory)){
            if(FolderTransactions::directoryLink(folder))throw std::runtime_error("Import links are not followed");
            for(const auto& e:fs::recursive_directory_iterator(directory)){if(FolderTransactions::directoryLink(e.path()))throw std::runtime_error("Import links are not followed");if(e.is_regular_file())files.push_back(e.path());}
        }else files.push_back(directory);
        if(files.size()>4096)throw std::runtime_error("Import has too many files");std::sort(files.begin(),files.end());std::uint64_t total=0;
        const auto feed=[&](const void* bytes,std::size_t size){if(BCryptHashData(digest,const_cast<PUCHAR>(static_cast<const UCHAR*>(bytes)),ULONG(size),0)<0)throw std::runtime_error("Cannot hash import contents");};
        for(const auto& p:files){const auto name=fs::is_directory(directory)?p.lexically_relative(directory).generic_u8string():p.filename().u8string();const auto size=fs::file_size(p);constexpr auto limit=2ULL*1024*1024*1024;if(size>limit||total>limit-size)throw std::runtime_error("Import exceeds its size bound");total+=size;
            const auto length=std::uint64_t(name.size());feed(&length,sizeof(length));feed(name.data(),name.size());feed(&size,sizeof(size));
            std::ifstream input(windowsFilePath(p),std::ios::binary);char block[65536];while(input){input.read(block,sizeof(block));if(input.gcount())feed(block,std::size_t(input.gcount()));}if(!input.eof())throw std::runtime_error("Cannot hash complete import source");}
        std::array<UCHAR,32> bytes{};if(BCryptFinishHash(digest,bytes.data(),ULONG(bytes.size()),0)<0)throw std::runtime_error("Cannot hash import contents");
        BCryptDestroyHash(digest);digest=nullptr;BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;std::ostringstream text;for(auto b:bytes)text<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return text.str();
    }catch(...){if(digest)BCryptDestroyHash(digest);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);throw;}
}
std::string safeLabel(std::string name){for(auto& c:name)if(static_cast<unsigned char>(c)<32||static_cast<unsigned char>(c)>=127||std::string("<>:\"/\\|?*").find(c)!=std::string::npos)c='_';return name.empty()?"Imported":name;}
fs::path chooseFolder(const fs::path& parent,const std::string& name){auto target=parent/fs::u8path(name);for(unsigned n=1;fs::exists(target);++n)target=parent/fs::u8path(name+"-"+std::to_string(n));return target;}
bool generationName(const std::string& name,const std::string& head){return name==head||name==head+".bak1"||name==head+".bak2"||name.rfind(head+".damaged-",0)==0||name.rfind(head+".bak1.damaged-",0)==0||name.rfind(head+".bak2.damaged-",0)==0;}
struct Game {std::string category,id,name;fs::path source;std::map<fs::path,fs::path> flat;};
std::vector<Game> games(const PlayerImportSource& source){
    std::vector<Game> result;const auto careers=source.root/L"careers";
    if(fs::exists(careers))for(const auto& entry:fs::directory_iterator(careers)){
        if(FolderTransactions::directoryLink(entry.path()))throw std::runtime_error("Import links are not followed");
        if(entry.is_directory()){bool found=false;for(unsigned n=0;n<3;++n)found|=fs::exists(career_files::generationPath(entry.path()/L"career.scr-career",n));if(found)result.push_back({"careers",{},"Imported",entry.path(),{}});}
        else{const auto name=entry.path().filename().u8string();if(name.size()<43||!validPlayerIdentity(name.substr(0,32))||!generationName(name.substr(32),".scr-career"))continue;
            const auto id=name.substr(0,32);auto i=std::find_if(result.begin(),result.end(),[&](const auto& g){return g.id==id&&!g.flat.empty();});
            if(i==result.end()){result.push_back({"careers",id,"Imported",{},{{fs::u8path("career"+name.substr(32)),entry.path()}}});}
            else i->flat.emplace(fs::u8path("career"+name.substr(32)),entry.path());}
    }
    for(auto& g:result)if(!g.flat.empty())for(const auto& category:{std::pair<const wchar_t*,const wchar_t*>{L"ghosts",L"original"},{L"ghosts-enhanced-v1",L"enhanced"}}){
        const auto folder=source.root/category.first/g.id;if(fs::exists(folder)){if(FolderTransactions::directoryLink(folder))throw std::runtime_error("Import links are not followed");
            for(const auto& e:fs::recursive_directory_iterator(folder)){if(FolderTransactions::directoryLink(e.path()))throw std::runtime_error("Import links are not followed");if(e.is_regular_file())g.flat.emplace(fs::path(L"personal-bests")/category.second/e.path().lexically_relative(folder),e.path());}}}
    const auto multiplayer=source.root/L"multiplayer";if(fs::exists(multiplayer))for(const auto& entry:fs::directory_iterator(multiplayer)){
        if(FolderTransactions::directoryLink(entry.path()))throw std::runtime_error("Import links are not followed");if(!entry.is_directory())continue;bool found=false;
        for(unsigned n=0;n<3;++n)found|=fs::exists(career_files::generationPath(entry.path()/L"game.scr-multiplayer",n));if(found)result.push_back({"multiplayer",{},"Imported",entry.path(),{}});
    }return result;
}
void validateGame(const fs::path& folder,Game& game,const NativeMemory& baseline,std::uint64_t content,PlayerImportReport& report){
    std::string owner=game.id;const auto warn=[&](const fs::path& p,const std::string& message){report.warnings.push_back(p.filename().u8string()+": "+message);};
    for(const auto& e:fs::recursive_directory_iterator(folder))if(e.is_regular_file()){
        const auto name=e.path().filename().u8string();try{
            std::string id;
            if(game.category=="multiplayer"&&generationName(name,"game.scr-multiplayer")){const auto saved=decodeNativeMultiplayerSave(baseline,read(e.path(),nativeMultiplayerSaveMaximumBytes));id=saved.info.id;game.name=saved.info.title;}
            else if(game.category=="careers"&&generationName(name,"career.scr-career")){const auto saved=career_files::readGeneration(e.path(),1);if(saved.state!=CareerFileState::Valid)throw std::runtime_error("unavailable career generation preserved");CareerBytes checkpoint;const auto info=decodeCareerInfo(saved.data,&checkpoint);(void)decodeNativeCheckpoint(baseline,checkpoint);id=info.id;game.name=info.name;}
            else if(name.find(".scr-ghost")!=std::string::npos){const auto saved=career_files::readGeneration(e.path(),4);if(saved.state!=CareerFileState::Valid)throw std::runtime_error("unavailable PB generation preserved");const auto lap=decodeGhostLap(saved.data);id=lap.context.key.career;(void)decodeGhostLap(saved.data,lap.context.key,content);}
            else if(name.find(".scr-ebest")!=std::string::npos){const auto saved=career_files::readGeneration(e.path(),5);if(saved.state!=CareerFileState::Valid)throw std::runtime_error("unavailable Enhanced PB generation preserved");const auto best=decodeEnhancedBest(saved.data);id=best.key.career;(void)decodeEnhancedBest(saved.data,best.key,{content,1,enhancedTimingMethodVersion});}
            else continue;
            if(!owner.empty()&&owner!=id)throw std::runtime_error("saved-game ownership differs; original file preserved");owner=id;
        }catch(const std::exception& error){warn(e.path(),error.what());}
    }game.id=owner;
}
bool validateHall(const fs::path& folder,bool enhanced,std::uint64_t content,PlayerImportReport& report){
    bool valid=false,future=false;const auto head=folder/(enhanced?L"index.scr-ehall":L"index.scr-hall");
    for(unsigned n=0;n<3;++n){const auto path=career_files::generationPath(head,n);if(!fs::exists(path))continue;
        try{const auto bytes=read(path,1024*1024);Reader r{bytes};if(r.u32()!=(enhanced?0x58444945u:0x58444948u))throw std::runtime_error("damaged Hall index");if(r.u32()!=1){future=true;throw std::runtime_error("unsupported Hall index preserved");}
            if(!r.u64())throw std::runtime_error("invalid Hall generation");const auto size=r.u32();r.require(size);Bytes table(bytes.begin()+r.position,bytes.begin()+r.position+size);r.position+=size;const auto end=r.position;if(r.u64()!=hash(bytes.data(),end))throw std::runtime_error("damaged Hall checksum");r.end();
            const auto checkGhost=[&](const auto& attempt,const auto& decode,const auto& bind,const auto& objectName){if(!attempt.ghost)return;const auto ghost=folder/L"ghosts"/objectName(attempt.id);
                try{const auto data=read(ghost,enhancedHallGhostMaximumBytes);const auto recording=decode(data);bind(recording,attempt); // A bad object remains unavailable, never a new attempt.
                    // SHA-256 binds the existing immutable object to its index reference.
                    BCRYPT_ALG_HANDLE a{};BCRYPT_HASH_HANDLE h{};std::array<UCHAR,32> digest{};
                    if(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot check Hall object hash");auto status=BCryptCreateHash(a,&h,nullptr,0,nullptr,0,0);
                    if(status>=0)status=BCryptHashData(h,const_cast<PUCHAR>(data.data()),ULONG(data.size()),0);if(status>=0)status=BCryptFinishHash(h,digest.data(),ULONG(digest.size()),0);if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);if(status<0||digest!=*attempt.ghost)throw std::runtime_error("Hall object digest differs");
                }catch(const std::exception& error){report.warnings.push_back(ghost.filename().u8string()+": "+error.what());}};
            if(enhanced){const EnhancedCatalog catalog{content,1,enhancedTimingMethodVersion};const auto hall=decodeEnhancedHall(table,catalog);for(const auto& board:hall.snapshot().boards)for(const auto& e:board)checkGhost(e.attempt,[](const auto& b){return decodeEnhancedHallGhost(b);},[&](const auto& g,const auto& a){requireEnhancedGhostBinding(g,a,catalog);},enhancedObjectName);}
            else{const HallCatalog catalog{content,1};const auto hall=decodeHallTable(table,catalog);for(const auto& board:hall.snapshot().boards)for(const auto& e:board)checkGhost(e.attempt,[](const auto& b){return decodeHallGhost(b);},[&](const auto& g,const auto& a){requireHallGhostBinding(g,a,catalog);},hallObjectName);}
            valid=true;
        }catch(const UnsupportedEnhancedRecords& error){future=true;report.warnings.push_back(error.what());}
        catch(const UnsupportedHallTable& error){future=true;report.warnings.push_back(error.what());}
        catch(const std::exception& error){report.warnings.push_back(path.filename().u8string()+": "+error.what());}
    }return valid&&!future;
}
}
PlayerImportSource playerImportSource(const fs::path& root,const fs::path& original,const fs::path& enhanced){
    PlayerImportSource source;source.root=fs::weakly_canonical(fs::absolute(root)).make_preferred();source.settings=fs::exists(source.root/L"settings.ini")?source.root/L"settings.ini":source.root/L"settings"/L"display.ini";
    source.originalHall=original.empty()?(fs::exists(source.root/L"hall-of-fame"/L"original")?source.root/L"hall-of-fame"/L"original":source.root/L"shared-hall"):original;
    source.enhancedHall=enhanced.empty()?(fs::exists(source.root/L"hall-of-fame"/L"enhanced")?source.root/L"hall-of-fame"/L"enhanced":enhancedHallRoot(source.originalHall)):enhanced;return source;
}
bool hasPlayerImportSource(const PlayerImportSource& source){
    return fs::exists(source.root/L"assets"/L"stunt-car-racer.scr-assets")||fs::exists(source.settings)||fs::exists(source.root/L"careers")||fs::exists(source.root/L"multiplayer")||fs::exists(source.originalHall)||fs::exists(source.enhancedHall)||sourceTransactions(source.root);
}
PlayerDataImport::PlayerDataImport(fs::path root,FolderTransactionObserver observer):layout_(playerDataLayout(root)),transactions_(layout_.root,std::move(observer)){transactions_.recover();}
bool PlayerDataImport::decided()const{if(!fs::exists(layout_.root/L"migration.scr-import"))return false;(void)receipt(layout_.root/L"migration.scr-import");return true;}
void PlayerDataImport::startFresh(){transactions_.recover();if(decided())return;transactions_.publishCollection([&](const auto& stage){write(stage/L"migration.scr-import",encodeReceipt({}));});}
PlayerImportReport PlayerDataImport::import(const PlayerImportSource& source,PlayerImportOptions options){
    transactions_.recover();
    if(source.root.empty()||!source.root.is_absolute())throw std::invalid_argument("Choose an absolute player-data source folder");
    std::error_code sameError;const bool same=fs::equivalent(source.root,layout_.root,sameError);
    if(!same&&sourceTransactions(source.root)){
        // Recover supported old journals only on a private copy. A source can
        // have its live head parked, so this precedes even asset discovery.
        const auto area=layout_.root/L".transactions"/NativeCareerStore::newId(),snapshot=windowsFilePath(area/L"source");
        fs::create_directories(snapshot);
        const auto cleanup=[&]{if(!fs::exists(area))return;if(area.parent_path()!=layout_.root/L".transactions"||!validPlayerIdentity(area.filename().u8string())||FolderTransactions::directoryLink(area))throw std::runtime_error("Unsafe import workspace");
            for(const auto& e:fs::recursive_directory_iterator(windowsFilePath(area)))if(FolderTransactions::directoryLink(e.path()))throw std::runtime_error("Import workspace links are not removed");fs::remove_all(windowsFilePath(area));};
        try{
            const auto before=fingerprint(source.root/L".transactions");
            for(const auto* name:{L"assets",L"careers",L"multiplayer",L"ghosts",L"ghosts-enhanced-v1",L"shared-hall",L"shared-hall-enhanced-v1",L"hall-of-fame",L"settings",L"settings.ini",L"last.scr-index",L"last.scr-index.bak1",L"last.scr-index.bak2",L"migration-recovery",L"migration.scr-import",L".transactions"}){
                const auto from=source.root/name;if(fs::exists(from))copyImported(from,snapshot/name);}
            if(before!=fingerprint(source.root/L".transactions"))throw std::runtime_error("Source transaction changed during import. Close its old game window and retry.");
            FolderTransactions(snapshot).recover();auto recovered=playerImportSource(snapshot);
            if(source.originalHall!=source.root/L"shared-hall"&&source.originalHall!=source.root/L"hall-of-fame"/L"original")recovered.originalHall=source.originalHall;
            if(source.enhancedHall!=source.root/L"shared-hall-enhanced-v1"&&source.enhancedHall!=source.root/L"hall-of-fame"/L"enhanced")recovered.enhancedHall=source.enhancedHall;
            const auto report=import(recovered,options);cleanup();return report;
        }catch(...){cleanup();throw;}
    }
    if(!hasPlayerImportSource(source))throw std::runtime_error("No recognised player data was found in that folder");
    if(FolderTransactions::directoryLink(source.root))throw std::runtime_error("Import links are not followed");
    for(const auto* category:{L"assets",L"careers",L"multiplayer",L"ghosts",L"ghosts-enhanced-v1",L"settings"})if(FolderTransactions::directoryLink(source.root/category))throw std::runtime_error("Import category links are not followed");
    if(FolderTransactions::directoryLink(source.settings)||FolderTransactions::directoryLink(source.settings.parent_path())||FolderTransactions::directoryLink(source.originalHall)||FolderTransactions::directoryLink(source.enhancedHall))throw std::runtime_error("Import source links are not followed");
    const auto sourcePack=source.root/L"assets"/L"stunt-car-racer.scr-assets";
    fs::path pack=layout_.pack;try{(void)loadLocalAssetPack(pack);}catch(const std::exception&){pack=sourcePack;}
    auto startup=startNativeGame(pack);initializeNativeOpeningSession(*startup.memory);NativeOpeningFlow opening(*startup.memory);const auto content=ghostTrackContentIdentity(startup.payload);
    auto keys=receipt(layout_.root/L"migration.scr-import");PlayerImportReport report;bool changed=false;
    try{transactions_.publishCollection([&](const auto& stage){
        for(const auto* name:{L"assets",L"careers",L"multiplayer",L"hall-of-fame",L"migration-recovery"})if(fs::exists(layout_.root/name))copyImported(layout_.root/name,stage/name);
        if(fs::exists(layout_.settings))copyFile(layout_.settings,stage/L"settings.ini");
        const auto recovery=stage/L"migration-recovery"/NativeCareerStore::newId();
        const auto preserve=[&](const fs::path& from,const fs::path& relative){if(fs::exists(from))copyImported(from,recovery/relative);};
        const auto accept=[&](const std::string& key){if(keys.count(key)){++report.skipped;return false;}keys.insert(key);changed=true;return true;};
        if(pack!=sourcePack&&fs::exists(sourcePack))try{(void)loadLocalAssetPack(sourcePack);}catch(const std::exception& error){
            if(accept("unavailable-assets:"+fingerprint(sourcePack)))preserve(sourcePack,L"source-assets/stunt-car-racer.scr-assets");report.warnings.push_back(std::string("Source assets preserved: ")+error.what());}
        if(pack==sourcePack){(void)loadLocalAssetPack(sourcePack);const auto key="assets:"+fingerprint(sourcePack);if(accept(key)){
            if(fs::exists(stage/L"assets")){preserve(stage/L"assets",L"previous-assets");fs::remove_all(windowsFilePath(stage/L"assets"));}
            fs::create_directories(stage/L"assets");copyFile(sourcePack,stage/L"assets"/L"stunt-car-racer.scr-assets");const fs::path meta=sourcePack.wstring()+L".json";if(fs::exists(meta))copyFile(meta,stage/L"assets"/L"stunt-car-racer.scr-assets.json");report.assets=true;}}
        unsigned number=0;for(auto game:games(source)){
            // A folder already in this collection is not an incoming copy.
            if(!game.source.empty()&&game.source.parent_path()==layout_.root/fs::u8path(game.category))continue;
            const auto prepared=stage/(L".source-"+std::to_wstring(number++));fs::create_directories(prepared);
            if(!game.source.empty()){const auto before=fingerprint(game.source);copyImported(game.source,prepared);if(before!=fingerprint(prepared)||before!=fingerprint(game.source))throw std::runtime_error("Source game changed during import. Close its old game window and retry.");}
            else for(const auto& file:game.flat)copyFile(file.second,prepared/file.first);
            const auto print=fingerprint(prepared);const auto key=game.category+":"+print;
            if(!accept(key)){fs::remove_all(windowsFilePath(prepared));continue;}
            validateGame(prepared,game,*startup.memory,content,report);
            const auto parent=stage/fs::u8path(game.category);fs::create_directories(parent);const auto target=chooseFolder(parent,safeLabel(game.name)+"-"+(game.id.empty()?print.substr(0,8):game.id.substr(0,8)));
            // Existing identities stay intact; normal Browse conflict resolution
            // provides Replace / Keep Both / Cancel after publication.
            if(!game.id.empty())for(const auto& e:fs::directory_iterator(parent))if(e.is_directory()){
                const auto head=e.path()/(game.category=="careers"?L"career.scr-career":L"game.scr-multiplayer");for(unsigned n=0;n<3;++n)try{const auto file=career_files::generationPath(head,n);if(!fs::exists(file))continue;
                    const auto id=game.category=="careers"?decodeCareerInfo(career_files::readGeneration(file,1).data).id:decodeNativeMultiplayerEnvelope(read(file,nativeMultiplayerSaveMaximumBytes)).info.id;if(id==game.id){++report.conflicts;break;}
                }catch(const std::exception&){} }
            fs::rename(prepared,target);(game.category=="careers"?report.careers:report.multiplayer)++;
        }
        // PB folders with no career and malformed flat checkpoint filenames are
        // retained as evidence rather than disappearing from an import.
        for(const auto* category:{L"ghosts",L"ghosts-enhanced-v1"}){const auto from=source.root/category;if(!fs::is_directory(from))continue;
            for(const auto& e:fs::directory_iterator(from))if(e.is_directory()){
                const auto id=e.path().filename().u8string();bool owner=false;for(unsigned n=0;n<3;++n)owner|=fs::exists(career_files::generationPath(source.root/L"careers"/(id+".scr-career"),n));
                if(!owner){if(accept("orphan-pb:"+fingerprint(e.path())))preserve(e.path(),fs::path(L"orphan-personal-bests")/category/e.path().filename());report.warnings.push_back("Unmatched personal bests preserved in migration recovery");}}}
        if(fs::is_directory(source.root/L"careers"))for(const auto& e:fs::directory_iterator(source.root/L"careers"))if(e.is_regular_file()){
            const auto name=e.path().filename().u8string();if(name.find(".scr-career")!=std::string::npos&&(name.size()<43||!validPlayerIdentity(name.substr(0,32)))){
                if(accept("unavailable-career:"+fingerprint(e.path())))preserve(e.path(),fs::path(L"unavailable-careers")/e.path().filename());report.warnings.push_back("Unrecognised career filename preserved in migration recovery");}}
        if(fs::exists(source.settings)){
            const auto key="settings:"+fingerprint(source.settings);if(!keys.count(key)){
                const auto preferences=readDisplayPreferences(source.settings);
                if(preferences&&(!fs::exists(stage/L"settings.ini")||options.replaceSettings)){
                    preserve(stage/L"settings.ini",L"previous-settings.ini");if(!saveDisplayPreferences(stage/L"settings.ini",*preferences))throw std::runtime_error("Cannot stage imported settings");
                    auto selections=loadPlayerSelections(source.settings);const auto last=career_files::readGeneration(source.root/L"last.scr-index",3);if(selections.career.empty()&&last.state==CareerFileState::Valid){Reader r{last.data};selections.career=r.text(32);r.end();}
                    if(!savePlayerSelection(stage/L"settings.ini",selections.career)||!savePlayerSelection(stage/L"settings.ini",selections.multiplayer,true))throw std::runtime_error("Cannot stage imported selections");report.settings=true;(void)accept(key);
                }else{report.warnings.push_back(preferences?"Existing game settings kept":"Unsupported or damaged source settings preserved");if(!preferences){preserve(source.settings,L"source-settings.ini");(void)accept(key);}}
            }else ++report.skipped;
        }
        if(!fs::exists(stage/L"settings.ini")){
            const auto last=career_files::readGeneration(source.root/L"last.scr-index",3);
            if(last.state==CareerFileState::Valid){Reader r{last.data};const auto id=r.text(32);r.end();if(accept("selection:"+id)){
                if(!saveDisplayPreferences(stage/L"settings.ini",{})||!savePlayerSelection(stage/L"settings.ini",id))throw std::runtime_error("Cannot import last-used career selection");report.settings=true;}}
        }
        for(bool enhanced:{false,true}){const auto from=enhanced?source.enhancedHall:source.originalHall;if(!fs::exists(from))continue;
            const auto name=enhanced?L"enhanced":L"original";const auto target=stage/L"hall-of-fame"/name;const auto key=std::string(enhanced?"enhanced-hall:":"original-hall:")+fingerprint(from);
            if(keys.count(key)){++report.skipped;continue;}
            if(!validateHall(from,enhanced,content,report)){if(accept("unavailable-"+key))preserve(from,fs::path(L"unavailable-hall")/name);report.warnings.push_back("Unavailable Hall archive preserved in migration recovery");continue;}
            if(fs::exists(target)&&!(enhanced?options.replaceEnhancedHall:options.replaceOriginalHall)){report.warnings.push_back(std::string(enhanced?"Enhanced":"Original")+" Hall kept; archive replacement was not selected");continue;}
            (void)accept(key);if(fs::exists(target)){preserve(target,fs::path(L"previous-hall")/name);fs::remove_all(windowsFilePath(target));}copyImported(from,target);(enhanced?report.enhancedHall:report.originalHall)=true;
        }
        if(!changed&&decided())throw NoImportChanges{};
        if(!report.warnings.empty()){std::string text;for(const auto& warning:report.warnings)text+=warning+'\n';write(recovery/L"report.txt",Bytes(text.begin(),text.end()));}
        write(stage/L"migration.scr-import",encodeReceipt(keys));
    });}catch(const NoImportChanges&){}return report;
}
} // namespace scr::runtime
