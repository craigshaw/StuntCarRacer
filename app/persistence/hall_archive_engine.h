#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include "persistence/hall_storage_types.h"
#include "platform/windows/shared_hall_storage.h"
#include <algorithm>
#include <array>
#include <limits>
#include <set>

namespace scr::runtime::archive_detail {
namespace fs=std::filesystem;
using namespace state::checkpoint;
class HallIoError:public std::runtime_error {public:using std::runtime_error::runtime_error;};
[[noreturn]]inline void failure(const char* message,DWORD code=GetLastError()){
    throw HallIoError(std::string(message)+" (Windows error "+std::to_string(code)+")");
}
struct File {HANDLE value{INVALID_HANDLE_VALUE};~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
inline HallGhostDigest digest(const Bytes& bytes){
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE h{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot open Hall ghost hash");
    auto status=BCryptCreateHash(algorithm,&h,nullptr,0,nullptr,0,0);HallGhostDigest result{};
    if(status>=0)status=BCryptHashData(h,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0);
    if(status>=0)status=BCryptFinishHash(h,result.data(),ULONG(result.size()),0);
    if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0)throw std::runtime_error("Cannot hash Hall ghost");return result;
}
inline std::optional<Bytes> readBytes(const fs::path& path,std::size_t maximum){
    File file;file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file.value==INVALID_HANDLE_VALUE){const auto code=GetLastError();if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)return {};failure("Cannot read shared Hall",code);}
    LARGE_INTEGER size{};if(!GetFileSizeEx(file.value,&size))failure("Cannot size shared Hall");
    if(size.QuadPart<0||std::uint64_t(size.QuadPart)>maximum)throw std::length_error("Shared Hall file exceeds its bound");
    Bytes bytes(std::size_t(size.QuadPart),0);DWORD count{};
    if(!ReadFile(file.value,bytes.data(),DWORD(bytes.size()),&count,nullptr)||count!=bytes.size())failure("Cannot read complete shared Hall");
    return bytes;
}
inline void atomicWrite(const fs::path& path,const Bytes& bytes,const HallWriteObserver& observer,HallWriteStage flushed,HallWriteStage published){
    const auto temporary=fs::path(path.wstring()+L".tmp");
    {File file;file.value=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file.value==INVALID_HANDLE_VALUE)failure("Cannot create shared Hall temporary");DWORD count{};
        if(!WriteFile(file.value,bytes.data(),DWORD(bytes.size()),&count,nullptr)||count!=bytes.size()||!FlushFileBuffers(file.value))failure("Cannot flush shared Hall temporary");}
    if(observer)observer(flushed);
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))failure("Cannot publish shared Hall");
    if(observer)observer(published);
}

// Common locked file protocol; policies retain distinct models and wire formats.
template<class Traits>class HallArchiveEngine {
    using Model=typename Traits::Model;using Catalog=typename Traits::Catalog;
    using Attempt=typename Traits::Attempt;using Ghost=typename Traits::Ghost;
    using Submission=typename Traits::Submission;
    using UnsupportedTable=typename Traits::UnsupportedTable;using UnsupportedGhost=typename Traits::UnsupportedGhost;
    using ArchiveRead=HallArchiveReadFor<Model>;using GhostRead=HallGhostReadFor<Ghost>;
    fs::path root_;Catalog catalog_;HallWriteObserver observer_;
Bytes envelope(const Model& hall,std::uint64_t generation)const{
    const auto table=Traits::encodeTable(hall);Writer w;w.u32(Traits::indexMagic);w.u32(Traits::indexVersion);w.u64(generation);w.u32(unsigned(table.size()));w.bytes(table.data(),table.size());
    w.u64(hash(w.data.data(),w.data.size()));return std::move(w.data);
}
struct Generation {HallArchiveState state{HallArchiveState::Missing};Bytes bytes;std::shared_ptr<const Model> hall;std::uint64_t number{};unsigned slot{};};
fs::path indexPath(const fs::path& root,unsigned slot)const{return root/(std::wstring(Traits::indexName)+(slot?L".bak"+std::to_wstring(slot):L""));}
Generation readGeneration(const fs::path& root,unsigned slot,Catalog catalog)const{
    Generation g;g.slot=slot;
    try{
        auto bytes=readBytes(indexPath(root,slot),Traits::tableMaximum+28);if(!bytes)return g;g.bytes=std::move(*bytes);
        Reader r{g.bytes};if(r.u32()!=Traits::indexMagic)throw std::runtime_error("Invalid Hall index magic");
        if(r.u32()!=Traits::indexVersion)throw UnsupportedTable("Unsupported Hall index version");
        g.number=r.u64();const auto size=r.u32();if(!g.number||size>Traits::tableMaximum)throw std::runtime_error("Invalid Hall index header");
        r.require(size);Bytes table(g.bytes.begin()+r.position,g.bytes.begin()+r.position+size);r.position+=size;
        const auto end=r.position;if(r.u64()!=hash(g.bytes.data(),end))throw std::runtime_error("Hall index checksum mismatch");r.end();
        g.hall=std::make_shared<const Model>(Traits::decodeTable(table,catalog));g.state=HallArchiveState::Valid;
    }catch(const HallIoError&){throw;}
    catch(const UnsupportedTable&){g.state=HallArchiveState::Unsupported;}
    catch(const std::length_error&){g.state=HallArchiveState::Corrupt;}
    catch(const std::invalid_argument&){g.state=HallArchiveState::Corrupt;}
    // Format-reader failures are distinct from OS I/O failures above.
    catch(const std::runtime_error&){g.state=HallArchiveState::Corrupt;}
    return g;
}
struct Loaded {std::array<Generation,3> copies;ArchiveRead result;std::vector<Generation*> valid;};
Loaded load(const fs::path& root,Catalog catalog)const{
    Loaded loaded;bool corrupt=false,unsupported=false;
    for(unsigned n=0;n<3;++n){auto& g=loaded.copies[n];g=readGeneration(root,n,catalog);
        corrupt|=g.state==HallArchiveState::Corrupt;unsupported|=g.state==HallArchiveState::Unsupported;}
    // Populate pointers only after returning Loaded (see select below).
    loaded.result.state=unsupported?HallArchiveState::Unsupported:corrupt?HallArchiveState::Corrupt:HallArchiveState::Missing;
    return loaded;
}
void select(Loaded& l,Catalog catalog)const{
    if(l.result.state==HallArchiveState::Unsupported)return;
    for(auto& g:l.copies)if(g.state==HallArchiveState::Valid)l.valid.push_back(&g);
    std::stable_sort(l.valid.begin(),l.valid.end(),[](const auto* a,const auto* b){return a->number>b->number;});
    for(unsigned n=1;n<l.valid.size();++n)if(l.valid[n-1]->number==l.valid[n]->number&&l.valid[n-1]->bytes!=l.valid[n]->bytes){l.result.state=HallArchiveState::Corrupt;l.valid.clear();return;}
    if(!l.valid.empty()){const auto& g=*l.valid.front();l.result={g.slot?HallArchiveState::Recovered:HallArchiveState::Valid,g.hall,g.number};}
    else if(l.result.state==HallArchiveState::Missing)l.result.hall=std::make_shared<const Model>(catalog);
}
bool objectFilename(const std::string& name)const{
    const std::string suffix=Traits::ghostSuffix;
    if(name.size()!=32+suffix.size()&&name.size()!=36+suffix.size())return false;
    if(name.substr(32)!=suffix&&name.substr(32)!=suffix+".tmp")return false;
    return std::all_of(name.begin(),name.begin()+32,[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
void collect(const fs::path& root,Catalog catalog)const{
    auto loaded=load(root,catalog);select(loaded,catalog);
    if(loaded.result.state==HallArchiveState::Unsupported||loaded.result.state==HallArchiveState::Corrupt)return;
    std::set<std::string> retained;
    for(const auto& g:loaded.copies){
        if(g.state==HallArchiveState::Corrupt)return; // Cannot inspect this generation's references safely.
        if(g.hall)for(const auto& board:g.hall->snapshot().boards)for(const auto& e:board)if(e.attempt.ghost)retained.insert(Traits::objectName(e.attempt.id));
    }
    for(const auto& file:fs::directory_iterator(root/L"ghosts")){
        if(!file.is_regular_file())continue;const auto name=file.path().filename().string();
        if(objectFilename(name)&&!retained.count(name)&&!DeleteFileW(file.path().c_str()))failure("Cannot collect unreferenced Hall ghost");
    }
}
public:
ArchiveRead read()const{prepareSharedHallDirectory(root_);SharedHallLock lock(root_);auto l=load(root_,catalog_);select(l,catalog_);return l.result;}
GhostRead readGhost(const Attempt& attempt)const{
    if(!attempt.ghost)return {};
    try{prepareSharedHallDirectory(root_);SharedHallLock lock(root_);auto bytes=readBytes(root_/L"ghosts"/Traits::objectName(attempt.id),Traits::ghostMaximum);if(!bytes)return {};
        if(digest(*bytes)!=*attempt.ghost)return {HallGhostState::Corrupt,{}};
        auto ghost=std::make_shared<const Ghost>(Traits::decodeGhost(*bytes));Traits::requireBinding(*ghost,attempt,catalog_);return {HallGhostState::Valid,std::move(ghost)};
    }catch(const HallIoError&){return {HallGhostState::Unavailable,{}};}
    catch(const UnsupportedGhost&){return {HallGhostState::Unsupported,{}};}
    catch(const std::length_error&){return {HallGhostState::Corrupt,{}};}
    catch(const std::exception&){return {HallGhostState::Corrupt,{}};}
}
std::uint64_t commit(const std::vector<std::shared_ptr<const Submission>>& submissions){
    std::vector<Attempt> attempts;attempts.reserve(submissions.size());
    for(const auto& s:submissions){if(!s)throw std::invalid_argument("Empty Hall submission");Traits::validateSubmission(*s,catalog_);
        attempts.push_back(s->recording?Traits::makeSubmission(s->attempt,s->recording.get()).attempt:s->attempt);}
    prepareSharedHallDirectory(root_);prepareSharedHallDirectory(root_/L"ghosts");
    SharedHallLock lock(root_);auto l=load(root_,catalog_);select(l,catalog_);
    if(!l.result.hall)throw std::runtime_error(l.result.state==HallArchiveState::Unsupported?"Shared Hall uses unsupported content, rules or format":"Shared Hall has no readable generation; existing files preserved");
    // Reclaim leftovers from an interrupted writer before creating more objects.
    // All surviving index generations are locked and protected by collect().
    collect(root_,catalog_);
    Model merged=*l.result.hall;bool changed=l.result.state==HallArchiveState::Recovered;
    for(const auto& a:attempts)changed|=merged.offer(a)==Traits::accepted;
    // Write only objects still referenced after this entire batch has merged.
    for(std::size_t n=0;n<submissions.size();++n){const auto& s=submissions[n];const auto& a=attempts[n];const auto& board=merged.entries(a.board);
        if(!a.ghost||std::none_of(board.begin(),board.end(),[&](const auto& e){return e.attempt.id==a.id;}))continue;
        const auto path=root_/L"ghosts"/Traits::objectName(a.id);bool healthy=false;
        try{const auto old=readBytes(path,Traits::ghostMaximum);healthy=old&&digest(*old)==*a.ghost;}catch(const std::length_error&){}
        if(!healthy){
            // Re-encode one retained capture at a time: bounded scratch instead
            // of holding a second full batch of encoded recordings in memory.
            const auto captured=s->recording?Traits::encodeGhost(*s->recording):HallBytes{};
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

    HallArchiveEngine(fs::path root,Catalog catalog,HallWriteObserver observer={}):root_(std::move(root)),catalog_(catalog),observer_(std::move(observer)){}
};
} // namespace scr::runtime::archive_detail
