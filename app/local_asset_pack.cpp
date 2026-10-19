#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "local_asset_pack.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>

namespace scr::viewer {
namespace {
constexpr std::size_t headerBytes=88,payloadBytes=408540;
constexpr char adfHash[]="548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06";
constexpr char payloadHash[]="508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c";
std::uint32_t word(const std::vector<std::uint8_t>& data,unsigned offset){
    return std::uint32_t(data[offset])|(std::uint32_t(data[offset+1])<<8)|
        (std::uint32_t(data[offset+2])<<16)|(std::uint32_t(data[offset+3])<<24);
}
std::string hex(const std::uint8_t* data,std::size_t size){
    constexpr char alphabet[]="0123456789abcdef";std::string out;out.reserve(size*2);
    for(std::size_t i=0;i<size;++i){out+=alphabet[data[i]>>4];out+=alphabet[data[i]&15];}return out;
}
std::string sha256(const std::uint8_t* data,std::size_t size){
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    const auto checked=[](NTSTATUS status){if(status<0)throw std::runtime_error("Windows SHA-256 validation failed");};
    checked(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0));
    try{
        checked(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0));
        checked(BCryptHashData(hash,const_cast<PUCHAR>(data),ULONG(size),0));
        std::array<std::uint8_t,32> digest{};checked(BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0));
        BCryptDestroyHash(hash);hash=nullptr;BCryptCloseAlgorithmProvider(algorithm,0);return hex(digest.data(),digest.size());
    }catch(...){if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);throw;}
}
}
LocalAssetPack loadLocalAssetPack(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Cannot open local asset pack; run scripts/import_native_assets.py with your original ADF");
    if(file.tellg()!=std::streamoff(headerBytes+payloadBytes))throw std::runtime_error("Asset pack has an invalid extent");
    std::vector<std::uint8_t> bytes(headerBytes+payloadBytes);file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));
    if(!file)throw std::runtime_error("Asset pack read failed");
    const std::array<std::uint8_t,8> magic{'S','C','R','D','A','T','A','1'};
    if(!std::equal(magic.begin(),magic.end(),bytes.begin())||word(bytes,8)!=1||word(bytes,12)!=headerBytes)
        throw std::runtime_error("Unsupported local asset pack version");
    if(hex(bytes.data()+16,32)!=adfHash||hex(bytes.data()+48,32)!=payloadHash||
       word(bytes,80)!=0xe700||word(bytes,84)!=payloadBytes||sha256(bytes.data()+headerBytes,payloadBytes)!=payloadHash)
        throw std::runtime_error("Local asset pack failed source/payload SHA-256 validation");
    return {{bytes.begin()+headerBytes,bytes.end()}};
}
} // namespace scr::viewer
