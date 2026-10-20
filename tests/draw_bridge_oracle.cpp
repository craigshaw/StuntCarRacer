// Differential execution of the unmodified original0x5a794 routine. Inputs
// come from the supplied disk payload and synthetic scalar edge cases only.
#include "draw_bridge_fixture.h"
#include "m68k.h"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
namespace f=scr::test::drawBridge;
f::Ram ram{},native{};
std::array<bool,0x80000> allowed{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000,cacheBase=0x7b000;
unsigned pc{},instructions{},maximumInstructions{},geometry{},cycles{},heldCalls{},geometryCalls{},cacheCalls{};
std::uint64_t totalInstructions{},cases{},compared{};
std::uint32_t seed=0x42524944;
bool invalid{},returned{};std::string failure;
std::uint32_t next(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
bool in(unsigned address,unsigned first,unsigned end){return address>=first&&address<end;}
void fail(const char* text,unsigned address){invalid=true;if(failure.empty()){std::ostringstream s;s<<text<<" address0x"<<std::hex<<address<<" pc0x"<<pc;failure=s.str();}}
void hook(unsigned address){
    pc=address;
    if(address==sentinel){returned=true;m68k_end_timeslice();return;}
    if(!in(address,f::entry,f::end)||++instructions>1200){fail("Code/budget",address);m68k_end_timeslice();}
}
unsigned read(unsigned address){
    if(address<ram.size())return ram[address];
    if(in(address,stackBase,stackTop))return stack[address-stackBase];
    if(in(address,sentinel,sentinel+4))return (address&1)?0x71:0x4e;
    fail("Read",address);return 0;
}
void write(unsigned address,unsigned value){
    if(in(address,stackBase,stackTop)){stack[address-stackBase]=std::uint8_t(value);return;}
    if(address<ram.size()&&allowed[address]){ram[address]=std::uint8_t(value);return;}
    fail("Write",address);
}
void compare(const std::vector<unsigned>& addresses){
    for(auto address:addresses){++compared;if(ram[address]!=native[address]){
        std::ostringstream s;s<<"Draw Bridge mismatch case"<<cases<<" address0x"<<std::hex<<address<<std::dec
            <<" original="<<unsigned(ram[address])<<" native="<<unsigned(native[address]);throw std::runtime_error(s.str());
    }}
}
void run(const std::vector<unsigned>& addresses){
    const auto result=f::animate(native);if(result.geometryUpdated)++geometryCalls;else if(native[0x1ca33]==5)++heldCalls;if(result.cacheUpdated)++cacheCalls;
    instructions=0;invalid=returned=false;failure.clear();
    m68k_pulse_reset();for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    // Byte-sized MOVE instructions retain the high bytes of D1/D2. Their
    // normal callers supply zero high bytes, which is the typed API contract.
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned n=0;n<4;++n)stack[4092+n]=std::uint8_t(sentinel>>(24-8*n));
    m68k_set_reg(M68K_REG_PC,f::entry);m68k_execute(20000);
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stackTop)throw std::runtime_error("Original Draw Bridge failed "+failure);
    compare(addresses);totalInstructions+=instructions;maximumInstructions=std::max(maximumInstructions,instructions);++cases;
}
void put(unsigned address,std::uint8_t value){ram[address]=value;native[address]=value;}
void putWord(unsigned address,std::uint16_t value){f::writeWord(ram,address,value);f::writeWord(native,address,value);}
void seedCase(unsigned phase,unsigned player,unsigned opponent,unsigned active,unsigned throttle,unsigned update){
    put(0x1ca33,5);put(0x1bbb0,std::uint8_t(phase));put(0x1bb1c,std::uint8_t(player));put(0x1bb1d,std::uint8_t(opponent));
    put(0x1bb3f,std::uint8_t(active));put(0x1bbcd,std::uint8_t(throttle));put(0x1bb71,std::uint8_t(update));
    put(0x1bb1b,std::uint8_t(next()));putWord(0x1bc2a,std::uint16_t(next()));putWord(0x1bbf6,std::uint16_t(next()));putWord(0x1bcc0,std::uint16_t(next()));
    putWord(0x1c6b6,std::uint16_t(next()));putWord(0x1c6b8,std::uint16_t(next()));
    for(unsigned i=0;i<5;++i)put(0x1c982+i,std::uint8_t(next()));
    for(unsigned i=0;i<0x48;++i)put(geometry+i,std::uint8_t(next()));
    for(unsigned i=0;i<0x54;++i)put(cacheBase+i,std::uint8_t(next()));
}
std::vector<std::uint8_t> readFile(const char* path){std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read payload");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
} // namespace
extern "C" {
unsigned m68k_read_memory_8(unsigned address){return read(address);}
unsigned m68k_read_memory_16(unsigned address){if(address&1)fail("Odd read",address);return (read(address)<<8)|read(address+1);}
unsigned m68k_read_memory_32(unsigned address){return (m68k_read_memory_16(address)<<16)|m68k_read_memory_16(address+2);}
void m68k_write_memory_8(unsigned address,unsigned value){write(address,value);}
void m68k_write_memory_16(unsigned address,unsigned value){if(address&1)fail("Odd write",address);write(address,value>>8);write(address+1,value);}
void m68k_write_memory_32(unsigned address,unsigned value){m68k_write_memory_16(address,value>>16);m68k_write_memory_16(address+2,value);}
}
int main(int argc,char** argv)try{
    if(argc!=2)throw std::runtime_error("Usage: draw_bridge_oracle payload.bin");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Original payload length mismatch");
    std::copy(payload.begin(),payload.end(),ram.begin()+0xe700);geometry=f::geometryAddress(f::readWord(ram,0x1f060));
    f::writeWord(ram,0x7aae6,std::uint16_t(cacheBase>>16));f::writeWord(ram,0x7aae8,std::uint16_t(cacheBase));native=ram;
    const auto addresses=f::outputs(ram);for(auto address:addresses)allowed[address]=true;
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    // Every phase byte, both sides of each active/retention boundary, existing
    // active latch, frame throttle sign, initial/full cache update. Poisoned
    // heights and signed bases also exercise exact16-bit wrapping.
    constexpr std::array<unsigned,8> segments{{0,0x2f,0x30,0x32,0x33,0x37,0x38,0xff}};
    for(unsigned phase=0;phase<256;++phase)for(auto player:segments)for(auto opponent:segments)
        for(unsigned active:{0U,0x0cU})for(unsigned throttle:{0U,0x80U})for(unsigned update:{0U,0x80U}){
            seedCase(phase,player,opponent,active,throttle,update);run(addresses);
        }
    const auto edgeCases=cases;
    // Also visit every possible segment byte for each car at every phase;
    // the boundary matrix above supplies all flag combinations at transitions.
    for(unsigned phase=0;phase<256;++phase)for(unsigned segment=0;segment<256;++segment)for(unsigned car=0;car<2;++car){
        seedCase(phase,car==0?segment:0,car==1?segment:0x2f,(phase&1)?0x0c:0,(phase&2)?0x80:0,(phase&4)?0x80:0);run(addresses);
    }
    const auto allPositionsCases=cases-edgeCases;
    // Nonbridge tracks must be true no-ops, including arbitrary byte IDs.
    for(unsigned track=0;track<256;++track)if(track!=5){seedCase(255,0x33,0x2f,0xff,0,0x80);put(0x1ca33,std::uint8_t(track));run(addresses);}
    // Independent native/original state evolves for complete byte-phase wraps.
    seedCase(0,0,0x2f,0,0,0);for(unsigned tick=0;tick<768;++tick)run(addresses);cycles+=24;
    // Bridge occupancy freezes the phase and shape; release resumes precisely
    // the next phase, without borrowing any original outputs.
    for(auto segment:{0x33U,0x37U}){put(0x1bb1c,std::uint8_t(segment));for(unsigned tick=0;tick<64;++tick)run(addresses);}
    put(0x1bb1c,0);put(0x1bb1d,0);for(unsigned tick=0;tick<64;++tick)run(addresses);cycles+=2;
    for(unsigned address=0;address<ram.size();++address){++compared;if(ram[address]!=native[address])throw std::runtime_error("Whole RAM end comparison differs");}
    std::cout<<"{\"cases\":"<<cases<<",\"edge_cases\":"<<edgeCases<<",\"all_segment_cases\":"<<allPositionsCases<<",\"complete_animation_cycles\":"<<cycles
        <<",\"geometry_calls\":"<<geometryCalls<<",\"cache_calls\":"<<cacheCalls<<",\"held_calls\":"<<heldCalls
        <<",\"compared_bytes\":"<<compared<<",\"original_instructions\":"<<totalInstructions<<",\"maximum_instructions\":"<<maximumInstructions
        <<",\"mismatches\":0,\"original_code_unmodified\":true,\"native_state_reseeded_with_original_outputs\":false,\"capture_input\":false}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
