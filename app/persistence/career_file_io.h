#pragma once
#include "persistence/native_career_store.h"

namespace scr::runtime::career_files {
CareerRead readGeneration(const std::filesystem::path&,unsigned kind);
CareerBytes envelope(unsigned kind,std::uint64_t generation,const CareerBytes&);
bool atomicWrite(const std::filesystem::path&,const CareerBytes&,std::string& error);
std::filesystem::path generationPath(const std::filesystem::path&,unsigned);
} // namespace scr::runtime::career_files
