#pragma once
#include "persistence/native_career_store.h"
#include <stdexcept>
namespace scr::test::career {
inline runtime::NativeCareerInfo find(const runtime::NativeCareerStore& store,const std::string& id){
    runtime::NativeCareerInfo found;for(const auto& entry:store.list())if(entry.id==id){
        if(!found.folder.empty())throw std::runtime_error("Fixture career ID is ambiguous");found=entry;}
    if(found.folder.empty())throw std::runtime_error("Fixture career is missing");return found;
}
inline runtime::CareerRead read(const runtime::NativeCareerStore& store,const std::string& id){return store.readCareer(find(store,id).key());}
inline void select(runtime::NativeCareerStore& store,const std::string& id){store.selectCareer(find(store,id));}
inline void create(runtime::NativeCareerStore& store,const std::string& id){
    runtime::NativeCareerInfo info{id,"Fixture",0,0,1};store.saveCareer(info,{1});
    if(!store.flush())throw std::runtime_error("Cannot create fixture career");
}
} // namespace scr::test::career
