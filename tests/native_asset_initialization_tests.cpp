#include "native_asset_initialization.h"
#include "replay_render_assets.h"
#include "m68k.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Memory=scr::viewer::NativeMemory;
Memory ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned phase{},pc{},instructions{};
std::uint64_t totalInstructions{},comparedBytes{};
bool executing{},invalid{},returned{};
std::string failure;
bool in(unsigned a,unsigned first,unsigned end){return a>=first&&a<end;}
void fail(const char* why,unsigned address){invalid=true;if(failure.empty())failure=std::string(why)+" address="+std::to_string(address)+" pc="+std::to_string(pc);}
bool code(unsigned a){
    if(phase==0)return in(a,0xed56,0xeec8)||in(a,0x1b7b6,0x1b7dc)||in(a,0x69e9a,0x69f04)||in(a,0xf402,0xf472);
    if(phase==1)return in(a,0x69cfc,0x69e9a)||in(a,0x59450,0x594c6);
    if(phase==2)return in(a,0x5aa02,0x5aa38);
    return in(a,0x1bace,0x1baf8)||in(a,0x593f8,0x59450);
}
bool writable(unsigned a){
    if(in(a,stackBase,stackTop))return true;
    if(phase==0)return in(a,0x64,0x80)||in(a,0xe7b0,0xe830)||in(a,0xf474,0xf4b4)||
        in(a,0xf4b8,0x1b6ca)||in(a,0x1b73a,0x1b742)||in(a,0x1b812,0x1b817)||
        in(a,0x69ec4,0x69ec6)||in(a,0x6a584,0x6a594);
    if(phase==1)return in(a,0x6490,0xe298)||in(a,0x6a4ac,0x6a57c)||in(a,0x6a5b6,0x722b6)||a==0x69f40;
    if(phase==2)return in(a,0x1fe82,0x20182);
    return in(a,0x6a594,0x79f94);
}
void hook(unsigned address){
    pc=address;
    if(invalid){m68k_end_timeslice();return;}
    if(address==sentinel){returned=true;m68k_end_timeslice();return;}
    if((address&1)||!code(address)||++instructions>10000000){fail("Unexpected PC or instruction budget",address);m68k_end_timeslice();}
}
std::uint8_t readByte(unsigned address){
    if(address<ram.size())return ram[address];
    if(in(address,stackBase,stackTop))return stack[address-stackBase];
    if(in(address,sentinel,sentinel+4))return (address&1)?0x71:0x4e;
    if(in(address,0xdff000,0xdff200)||in(address,0xbfd000,0xbff001))return 0;
    fail("Unexpected read",address);return 0;
}
void writeByte(unsigned address,std::uint8_t value){
    if(in(address,0xdff000,0xdff200)||in(address,0xbfd000,0xbff001))return;
    if(!executing||!writable(address)){fail("Unexpected write",address);return;}
    if(in(address,stackBase,stackTop))stack[address-stackBase]=value;
    else if(address<ram.size())ram[address]=value;
    else fail("Out-of-range write",address);
}
void run(unsigned entry,unsigned stage){
    phase=stage;executing=false;m68k_pulse_reset();
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SR,0x2700);
    for(unsigned i=0;i<4;++i)stack[4092+i]=static_cast<std::uint8_t>(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_PC,entry);
    invalid=returned=false;failure.clear();instructions=0;executing=true;m68k_execute(1000000000);executing=false;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stackTop)
        throw std::runtime_error("Original asset initialization failed: "+failure+" phase="+std::to_string(phase));
    totalInstructions+=instructions;
}
std::vector<std::uint8_t> readFile(const char* path){
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read local fixture");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
template<typename Bytes> std::uint32_t longword(const Bytes& memory,unsigned a){
    return (std::uint32_t(memory[a])<<24)|(std::uint32_t(memory[a+1])<<16)|(std::uint32_t(memory[a+2])<<8)|memory[a+3];
}
void compare(const Memory& native,unsigned first,unsigned end){
    for(unsigned a=first;a<end;++a){++comparedBytes;if(native[a]!=ram[a])throw std::runtime_error("Native/original asset mismatch address="+std::to_string(a));}
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return readByte(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1){fail("Odd word read",a);return 0;}return (unsigned(readByte(a))<<8)|readByte(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){writeByte(a,static_cast<std::uint8_t>(v));}
void m68k_write_memory_16(unsigned a,unsigned v){if(a&1){fail("Odd word write",a);return;}writeByte(a,static_cast<std::uint8_t>(v>>8));writeByte(a+1,static_cast<std::uint8_t>(v));}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv){try{
    if(argc<2||argc>3)throw std::invalid_argument("Usage: native_asset_initialization_tests original-payload.bin [capture-chipram.bin]");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::invalid_argument("Wrong original payload size");
    auto native=std::make_unique<Memory>();std::copy(payload.begin(),payload.end(),native->begin()+0xe700);ram=*native;
    scr::viewer::initializeNativeAssets(*native);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    run(0xed56,0);run(0x69cfc,1);run(0x5aa02,2);run(0x1bace,3);
    // Every byte agrees except native OS interrupt vectors and buffers which
    // native startup deliberately clears independently of the Amiga hardware.
    compare(*native,0,0x64);compare(*native,0x80,0x80000);
    const auto assets=scr::viewer::decodeReplayRenderAssets(payload.data(),payload.size(),native->data(),native->size());
    if(assets.background.pixels.size()!=64000)throw std::runtime_error("Native assets did not decode into the renderer");
    unsigned captureObjects=0;
    if(argc==3){
        const auto capture=readFile(argv[2]);if(capture.size()!=ram.size())throw std::invalid_argument("Wrong capture size");
        for(unsigned id=0;id<52;++id){
            const auto source=longword(*native,0x6a4ac+4*id);
            if(source!=longword(capture,0x6a4ac+4*id))throw std::runtime_error("Capture/native object pointer mismatch");
            auto end=0xe298u;
            for(unsigned next=0;next<52;++next){const auto pointer=longword(*native,0x6a4ac+4*next);if(pointer>source)end=std::min(end,pointer);}
            // Hardware sprite position headers are deliberately mutable.
            const auto first=source+((id>=37&&id<=48)?4:0);
            if(!std::equal(native->begin()+first,native->begin()+end,capture.begin()+first))throw std::runtime_error("Capture/native immutable object bytes differ id="+std::to_string(id));
            comparedBytes+=end-first;++captureObjects;
        }
        if(!std::equal(native->begin()+0x1fe82,native->begin()+0x20182,capture.begin()+0x1fe82))throw std::runtime_error("Initialized font differs from capture");
        comparedBytes+=768;
    }
    std::cout<<"{\"mismatches\":0,\"total_cases\":4,\"compared_bytes\":"<<comparedBytes<<",\"capture_objects\":"<<captureObjects<<",\"original_instructions\":"<<totalInstructions<<",\"native_capture_input\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
