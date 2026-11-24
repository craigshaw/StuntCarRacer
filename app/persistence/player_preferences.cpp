#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "persistence/player_preferences.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace scr::runtime {
namespace {
std::string read(const std::filesystem::path& path){
    std::error_code error;const auto size=std::filesystem::file_size(path,error);
    if(error)return {};if(size>4096)throw std::runtime_error("Settings file exceeds its limit");
    std::ifstream file(path,std::ios::binary);std::string text(std::size_t(size),'\0');file.read(text.data(),std::streamsize(text.size()));
    if(!file)throw std::runtime_error("Cannot read settings");return text;
}
PlayerSelections selections(const std::string& text){
    PlayerSelections result;bool career=false,multiplayer=false;std::istringstream input(text);std::string line;
    while(std::getline(input,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        for(const auto& key:{std::string("last-career="),std::string("last-multiplayer=")})if(line.compare(0,key.size(),key)==0){
            auto id=line.substr(key.size());if(!id.empty()&&!validPlayerIdentity(id))throw std::runtime_error("Invalid saved-game preference");
            auto& seen=key=="last-career="?career:multiplayer;if(seen)throw std::runtime_error("Duplicate saved-game preference");seen=true;
            (key=="last-career="?result.career:result.multiplayer)=std::move(id);
        }
    }return result;
}
std::string body(const std::string& text){
    std::istringstream input(text);std::string line,result;
    while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.compare(0,12,"last-career=")!=0&&line.compare(0,17,"last-multiplayer=")!=0)result+=line+'\n';}
    return result;
}
bool write(const std::filesystem::path& path,const std::string& text){
    std::error_code error;if(path.has_parent_path())std::filesystem::create_directories(path.parent_path(),error);if(error)return false;
    const auto pending=std::filesystem::path(path.wstring()+L".tmp");
    const auto file=CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
    DWORD count{};const bool complete=WriteFile(file,text.data(),DWORD(text.size()),&count,nullptr)&&count==text.size()&&FlushFileBuffers(file);CloseHandle(file);
    if(!complete||!MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(pending.c_str());return false;}return true;
}
std::string document(std::string text,const PlayerSelections& ids){return text+"last-career="+ids.career+"\nlast-multiplayer="+ids.multiplayer+"\n";}
}
bool validPlayerIdentity(const std::string& id)noexcept{return id.size()==32&&std::all_of(id.begin(),id.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
bool playerSelectionLine(const std::string& line){
    for(const auto* key:{"last-career=","last-multiplayer="}){const std::string prefix(key);
        if(line.compare(0,prefix.size(),prefix)==0){const auto id=line.substr(prefix.size());return id.empty()||validPlayerIdentity(id);}}
    return false;
}
std::mutex& playerPreferencesMutex(){static std::mutex mutex;return mutex;}
PlayerSelections loadPlayerSelections(const std::filesystem::path& path){
    std::lock_guard<std::mutex> lock(playerPreferencesMutex());try{return selections(read(path));}catch(const std::exception&){return {};}
}
bool savePlayerSelection(const std::filesystem::path& path,const std::string& id,bool multiplayer){
    if(!id.empty()&&!validPlayerIdentity(id))return false;
    std::lock_guard<std::mutex> lock(playerPreferencesMutex());
    try{auto text=read(path);auto ids=selections(text);(multiplayer?ids.multiplayer:ids.career)=id;
        if(text.empty())text="SCR selections 1\n";
        else if(text.rfind("SCR display ",0)!=0&&text.rfind("SCR selections 1\n",0)!=0)return false;
        return write(path,document(body(text),ids));
    }catch(const std::exception&){return false;}
}
bool saveDisplayDocument(const std::filesystem::path& path,const std::string& displayBody){
    std::lock_guard<std::mutex> lock(playerPreferencesMutex());
    try{PlayerSelections ids;try{ids=selections(read(path));}catch(const std::exception&){}
        return write(path,document(displayBody,ids));}catch(const std::exception&){return false;}
}
} // namespace scr::runtime
