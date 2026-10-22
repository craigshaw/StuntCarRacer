#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_save_store.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace scr::viewer;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
NativeSaveBlock pattern(unsigned seed){
    NativeSaveBlock result{};
    for(unsigned i=0;i<result.size();++i)result[i]=std::uint8_t(seed*73+i*19+(i>>4));
    return result;
}
}
int main(int argc,char** argv)try{
    if(argc!=2)throw std::invalid_argument("native_save_store_tests output-directory");
    const std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    const auto file=root/"slots.scr-save";
    std::filesystem::remove(file);std::filesystem::remove(file.string()+".tmp");
    NativeSaveStore store(file);NativeSaveBlock block{};
    require(!store.readDirectory(block)&&block==NativeSaveBlock{},"Missing save file was not a blank medium");
    auto directory=pattern(99);
    for(unsigned slot=0;slot<30;++slot){
        require(!store.writeSlot(slot,directory,pattern(slot)),"Slot write failed");
        NativeSaveStore relaunched(file);
        require(!relaunched.readDirectory(block)&&block==directory,"Relaunch lost directory bytes");
        for(unsigned old=0;old<=slot;++old)
            require(!relaunched.readSlot(old,block)&&block==pattern(old),"Saving another slot changed existing payloads");
    }
    directory=pattern(123);require(!store.writeDirectory(directory),"Directory update failed");
    require(!store.readSlot(29,block)&&block==pattern(29),"Directory update changed slot payload");
    require(std::filesystem::file_size(file)==31*512,"Save medium size differs from original sectors");
    require(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"Cannot prepare read-only save test");
    const auto denied=store.writeSlot(29,pattern(124),pattern(125));
    const auto resetAttributes=SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_NORMAL);
    require(resetAttributes!=0&&denied!=0,"Read-only save was overwritten");
    require(!store.readDirectory(block)&&block==directory,"Failed write damaged existing directory");
    require(!store.readSlot(29,block)&&block==pattern(29),"Failed write damaged existing payload");
    require(!std::filesystem::exists(file.string()+".tmp"),"Failed commit left temporary output");
    {std::ofstream pending(file.string()+".tmp",std::ios::binary);pending<<"interrupted write";}
    require(!store.readSlot(29,block)&&block==pattern(29),"Interrupted temporary file replaced committed save");
    require(!store.writeSlot(29,directory,pattern(55)),"Saving after interrupted temporary write failed");
    for(unsigned size:{0u,511u,15871u,15873u}){
        const auto corrupt=root/("invalid-"+std::to_string(size)+".scr-save");
        {std::ofstream output(corrupt,std::ios::binary);for(unsigned i=0;i<size;++i)output.put(char(i));}
        NativeSaveStore broken(corrupt);block=pattern(5);
        require(broken.readDirectory(block)!=0&&block==pattern(5),"Corrupt file partly applied a directory");
        require(broken.writeSlot(0,directory,pattern(0))!=0&&std::filesystem::file_size(corrupt)==size,"Write replaced a corrupt medium silently");
    }
    bool rejected=false;
    try{NativeSaveStore invalid(root/"original.adf");}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Save store accepted an original-media destination");
    NativeSaveStore temporary;require(!temporary.writeSlot(0,directory,pattern(42)),"Temporary medium write failed");
    require(!temporary.readSlot(0,block)&&block==pattern(42),"Temporary medium lost slot");
    rejected=false;try{(void)temporary.readSlot(30,block);}catch(const std::out_of_range&){rejected=true;}
    require(rejected,"Save store accepted an invalid slot");
    std::cout<<"{\"status\":\"passed\",\"slots\":30,\"relaunches\":30,\"existing_slots_preserved\":true,\"atomic_failure_checked\":true,\"interrupted_write_checked\":true,\"invalid_lengths\":4,\"original_media_destination_rejected\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
