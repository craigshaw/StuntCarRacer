#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "persistence/native_save_store.h"
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <stdexcept>

namespace scr::runtime {
namespace {
std::uint8_t diskError(DWORD error){
    return error==ERROR_ACCESS_DENIED||error==ERROR_WRITE_PROTECT?0x1c:0x80;
}
}
NativeSaveStore::NativeSaveStore(std::filesystem::path file):file_(std::move(file)){
    if(file_.empty())return;
    auto extension=file_.extension().wstring();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return std::towlower(c);});
    if(extension!=L".scr-save")throw std::invalid_argument("Native save files must use the .scr-save extension");
}
std::uint8_t NativeSaveStore::read(Image& image){
    if(file_.empty()){image=temporary_;return 0;}
    std::error_code error;
    const bool exists=std::filesystem::exists(file_,error);
    if(error)return 0x80;
    if(!exists){image={};return 0;}
    const auto size=std::filesystem::file_size(file_,error);
    if(error||size!=sizeof(Image))return 0x80;
    std::ifstream input(file_,std::ios::binary);
    if(!input)return 0x80;
    input.read(reinterpret_cast<char*>(image.data()),sizeof(Image));
    return input?0:0x80;
}
std::uint8_t NativeSaveStore::commit(const Image& image){
    if(file_.empty()){temporary_=image;return 0;}
    std::error_code error;
    if(file_.has_parent_path())std::filesystem::create_directories(file_.parent_path(),error);
    if(error)return 0x80;
    const std::filesystem::path pending=file_.wstring()+L".tmp";
    const auto handle=CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return diskError(GetLastError());
    DWORD written{};
    const bool complete=WriteFile(handle,image.data(),DWORD(sizeof(Image)),&written,nullptr)&&
        written==sizeof(Image)&&FlushFileBuffers(handle);
    const auto failure=complete?ERROR_SUCCESS:GetLastError();
    CloseHandle(handle);
    if(!complete){DeleteFileW(pending.c_str());return diskError(failure);}
    if(!MoveFileExW(pending.c_str(),file_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        const auto result=diskError(GetLastError());DeleteFileW(pending.c_str());return result;
    }
    return 0;
}
std::uint8_t NativeSaveStore::readDirectory(NativeSaveBlock& directory){
    Image image{};if(const auto error=read(image))return error;
    directory=image[0];return 0;
}
std::uint8_t NativeSaveStore::readSlot(unsigned slot,NativeSaveBlock& payload){
    if(slot>=30)throw std::out_of_range("Native save slot exceeds29");
    Image image{};if(const auto error=read(image))return error;
    payload=image[slot+1];return 0;
}
std::uint8_t NativeSaveStore::writeDirectory(const NativeSaveBlock& directory){
    Image image{};if(const auto error=read(image))return error;
    image[0]=directory;return commit(image);
}
std::uint8_t NativeSaveStore::writeSlot(unsigned slot,const NativeSaveBlock& directory,const NativeSaveBlock& payload){
    if(slot>=30)throw std::out_of_range("Native save slot exceeds29");
    Image image{};if(const auto error=read(image))return error;
    image[0]=directory;image[slot+1]=payload;return commit(image);
}
} // namespace scr::runtime
