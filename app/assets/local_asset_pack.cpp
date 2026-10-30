#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "assets/local_asset_pack.h"
#include "supported_adf_sources.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <iomanip>

namespace scr::runtime {
namespace {
constexpr std::size_t headerBytes=88,payloadBytes=detail::payload_bytes;
const detail::AdfSource* findSource(const std::string& digest){
    for(const auto& source:detail::adfSources)if(digest==source.sha256)return &source;
    return nullptr;
}
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
void putWord(std::vector<std::uint8_t>& bytes,unsigned offset,std::uint32_t value){
    for(unsigned n=0;n<4;++n)bytes[offset+n]=std::uint8_t(value>>(n*8));
}
void putHash(std::vector<std::uint8_t>& bytes,unsigned offset,const char* value){
    const auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(unsigned n=0;n<32;++n)bytes[offset+n]=std::uint8_t(nibble(value[n*2])*16+nibble(value[n*2+1]));
}
std::string jsonString(const std::wstring& value){
    std::ostringstream out;out<<'"';
    for(auto c:value){
        if(c==L'"'||c==L'\\')out<<'\\'<<char(c);
        else if(c>=32&&c<127)out<<char(c);
        else out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c);
    }
    out<<'"';return out.str();
}
void writeComplete(const std::filesystem::path& path,const void* data,std::size_t bytes){
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot write imported game data. Check that the data folder is writable, then retry.");
    DWORD written{};const bool good=WriteFile(file,data,DWORD(bytes),&written,nullptr)&&written==bytes&&FlushFileBuffers(file);
    CloseHandle(file);
    if(!good)throw std::runtime_error("Imported game data could not be written completely. Retry the import.");
}
void replaceFile(const std::filesystem::path& from,const std::filesystem::path& to){
    if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace imported game data. Close other copies of the game and retry.");
}
}
LocalAssetPack loadLocalAssetPack(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Game data is missing or unreadable. Choose your supported original ADF to import it.");
    if(file.tellg()!=std::streamoff(headerBytes+payloadBytes))throw std::runtime_error("Asset pack has an invalid extent");
    std::vector<std::uint8_t> bytes(headerBytes+payloadBytes);file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));
    if(!file)throw std::runtime_error("Asset pack read failed");
    const std::array<std::uint8_t,8> magic{'S','C','R','D','A','T','A','1'};
    if(!std::equal(magic.begin(),magic.end(),bytes.begin())||word(bytes,8)!=1||word(bytes,12)!=headerBytes)
        throw std::runtime_error("Unsupported local asset pack version");
    const auto source=findSource(hex(bytes.data()+16,32));
    if(!source||hex(bytes.data()+48,32)!=source->payload_sha256||
       word(bytes,80)!=detail::load_address||word(bytes,84)!=payloadBytes||sha256(bytes.data()+headerBytes,payloadBytes)!=source->payload_sha256)
        throw std::runtime_error("Local asset pack failed source/payload SHA-256 validation");
    return {{bytes.begin()+headerBytes,bytes.end()}};
}
void importLocalAssetPack(const std::filesystem::path& source,const std::filesystem::path& destination){
    if(destination.extension()!=L".scr-assets")throw std::invalid_argument("Imported game data requires a .scr-assets destination.");
    const auto adf=std::filesystem::absolute(source).lexically_normal();
    const auto pack=std::filesystem::absolute(destination).lexically_normal();
    const std::filesystem::path manifest=pack.wstring()+L".json",pending=pack.wstring()+L".tmp",pendingManifest=manifest.wstring()+L".tmp";
    for(const auto& output:{pack,manifest,pending,pendingManifest}){
        std::error_code error;const bool same=std::filesystem::equivalent(adf,output,error);
        if(adf==output||(!error&&same))throw std::invalid_argument("Import must preserve the original source disk.");
    }
    std::ifstream input(adf,std::ios::binary|std::ios::ate);
    if(!input)throw std::runtime_error("Cannot read this ADF. Check the file is available, then choose it again.");
    if(input.tellg()!=detail::adf_bytes)throw std::runtime_error("This ADF is not yet supported. Choose a verified Stunt Car Racer disk.");
    std::vector<std::uint8_t> disk(detail::adf_bytes);input.seekg(0);input.read(reinterpret_cast<char*>(disk.data()),std::streamsize(disk.size()));
    if(!input)throw std::runtime_error("The ADF could not be read completely. Choose it again.");
    const auto sourceDefinition=findSource(sha256(disk.data(),disk.size()));
    if(!sourceDefinition||sha256(disk.data()+detail::disk_offset,payloadBytes)!=sourceDefinition->payload_sha256)
        throw std::runtime_error("This ADF is not yet supported. Choose a verified Quartex (QTX) or Woodpecker disk.");
    std::vector<std::uint8_t> bytes(headerBytes+payloadBytes);
    const std::string magic="SCRDATA1";std::copy(magic.begin(),magic.end(),bytes.begin());
    putWord(bytes,8,1);putWord(bytes,12,headerBytes);putHash(bytes,16,sourceDefinition->sha256);putHash(bytes,48,sourceDefinition->payload_sha256);
    putWord(bytes,80,detail::load_address);putWord(bytes,84,payloadBytes);std::copy_n(disk.begin()+detail::disk_offset,payloadBytes,bytes.begin()+headerBytes);
    std::ostringstream record;record<<"{\n  \"format\": \"SCRDATA1\", \"version\": 1,\n  \"source\": {\"filename\": "<<jsonString(adf.filename().wstring())
        <<", \"bytes\": "<<detail::adf_bytes<<", \"sha256\": \""<<sourceDefinition->sha256<<"\", \"variant\": \""<<sourceDefinition->name<<"\"},\n  \"source_image\": {\"disk_offset\": "<<detail::disk_offset<<", \"bytes\": "<<payloadBytes<<", \"load_address\": "<<detail::load_address<<", \"sha256\": \""<<sourceDefinition->payload_sha256
        <<"\"},\n  \"pack\": {\"bytes\": "<<bytes.size()<<", \"sha256\": \""<<sha256(bytes.data(),bytes.size())
        <<"\"},\n  \"recipe\": \"native verified ADF slice v1\",\n  \"capture_input\": false, \"rom_input\": false, \"cpu_execution\": false\n}\n";
    if(pack.has_parent_path())std::filesystem::create_directories(pack.parent_path());
    try{
        writeComplete(pending,bytes.data(),bytes.size());(void)loadLocalAssetPack(pending);
        const auto json=record.str();writeComplete(pendingManifest,json.data(),json.size());
        // Validate both files before replacing either retained output. The
        // pack header remains authoritative if a provenance replacement fails.
        replaceFile(pendingManifest,manifest);replaceFile(pending,pack);
    }catch(...){DeleteFileW(pending.c_str());DeleteFileW(pendingManifest.c_str());throw;}
}
std::wstring supportedAdfNames(){
    std::wstring names;
    for(const auto& source:detail::adfSources){
        if(!names.empty())names+=L", ";
        const std::string name=source.name;names.append(name.begin(),name.end());
    }
    return names;
}
std::wstring localAssetSourceName(const std::filesystem::path& pack){
    const auto fallback=L"Verified original disk";
    std::ifstream input(pack.wstring()+L".json",std::ios::binary|std::ios::ate);
    if(!input||input.tellg()>65536)return fallback;
    const auto size=input.tellg();if(size<0)return fallback;
    std::string data(std::size_t(size),'\0');input.seekg(0);input.read(data.data(),std::streamsize(data.size()));if(!input)return fallback;
    std::ifstream packed(pack,std::ios::binary);std::array<std::uint8_t,48> header{};
    if(!packed.read(reinterpret_cast<char*>(header.data()),header.size()))return fallback;
    // A failed pack replacement can leave newer advisory provenance behind.
    // Never label the retained pack with a different source disk's filename.
    if(data.find(hex(header.data()+16,32))==std::string::npos)return fallback;
    auto at=data.find("\"filename\"");if(at==std::string::npos)return fallback;
    at=data.find(':',at+10);if(at==std::string::npos)return fallback;
    at=data.find_first_not_of(" \r\n\t",at+1);if(at==std::string::npos||data[at++]!='"')return fallback;
    std::wstring value;
    while(at<data.size()&&value.size()<512){
        const unsigned char c=data[at++];if(c=='"')return value.empty()?fallback:value;
        if(c=='\\'){
            if(at==data.size())return fallback;const char escaped=data[at++];
            if(escaped=='u'){
                if(data.size()-at<4)return fallback;unsigned code=0;
                for(unsigned n=0;n<4;++n){const char digit=data[at++];code<<=4;
                    if(digit>='0'&&digit<='9')code+=digit-'0';else if(digit>='a'&&digit<='f')code+=digit-'a'+10;else if(digit>='A'&&digit<='F')code+=digit-'A'+10;else return fallback;}
                if(code<32)return fallback;value+=wchar_t(code);
            }else if(escaped=='"'||escaped=='\\'||escaped=='/')value+=wchar_t(escaped);else return fallback;
        }else if(c>=32&&c<128)value+=wchar_t(c);else return fallback;
    }
    return fallback;
}
} // namespace scr::runtime
