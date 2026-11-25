#pragma once
#include <filesystem>

namespace scr::runtime {
// Folder histories can exceed MAX_PATH even when the chosen data root is short.
// Win32 calls and file streams need the extended absolute spelling explicitly.
inline std::filesystem::path windowsFilePath(const std::filesystem::path& path){
    const auto value=std::filesystem::absolute(path).lexically_normal().wstring();
    if(value.rfind(L"\\\\?\\",0)==0)return value;
    if(value.rfind(L"\\\\",0)==0)return L"\\\\?\\UNC\\"+value.substr(2);
    return L"\\\\?\\"+value;
}
}
