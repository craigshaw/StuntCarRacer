#pragma once
#include "state/native_checkpoint_io.h"
#include <filesystem>

namespace scr::runtime {
using SavedGameBytes=state::checkpoint::Bytes;
enum class SavedGameFileState {Missing,Valid,Recovered,Corrupt,Unsupported};
struct SavedGameRead {SavedGameFileState state{SavedGameFileState::Missing};SavedGameBytes data;std::uint64_t generation{};};
struct SavedGameEntry {
    std::string id,name;unsigned division{},league{};std::uint64_t lastPlayed{};
    SavedGameFileState state{SavedGameFileState::Valid};std::filesystem::path folder;
    unsigned players{1};
    unsigned championship{},race{};bool competition{},report{},finish{};
    std::string key()const{return folder.empty()?id:folder.filename().u8string();}
};
// Existing career APIs and byte formats retain their names and representation.
using CareerBytes=SavedGameBytes;
using CareerFileState=SavedGameFileState;
using CareerRead=SavedGameRead;
using NativeCareerInfo=SavedGameEntry;
} // namespace scr::runtime
