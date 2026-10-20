#include "../app/native_race_initialization.h"
#include "../app/native_menu_flow.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using Ram=scr::viewer::NativeMemory;
Ram original{},native{},baseline{};
std::array<std::uint8_t,4096> stackBytes{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned instructions{},pc{},maximumInstructions{};
bool invalid{},returned{},promotion{};
std::uint64_t cases{},compared{};
std::uint32_t seed=0x53454153;
std::uint32_t next(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
unsigned read(unsigned address) {
    if(address<original.size())return original[address];
    if(address>=stackBase&&address<stackTop)return stackBytes[address-stackBase];
    if(address>=sentinel&&address<sentinel+4) {
        constexpr std::uint8_t stop[]{0x4e,0x72,0x27,0};return stop[address-sentinel];
    }
    invalid=true;return 0;
}
void write(unsigned address,unsigned value) {
    if(address<original.size()){original[address]=std::uint8_t(value);return;}
    if(address>=stackBase&&address<stackTop){stackBytes[address-stackBase]=std::uint8_t(value);return;}
    invalid=true;
}
void skipCall() {
    const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);
    const unsigned target=(read(sp)<<24)|(read(sp+1)<<16)|(read(sp+2)<<8)|read(sp+3);
    m68k_set_reg(M68K_REG_SP,sp+4);m68k_set_reg(M68K_REG_PC,target);
}
void hook(unsigned address) {
    pc=address;
    if(address==sentinel){returned=true;m68k_end_timeslice();return;}
    if(promotion&&(address==0x5deb6||address==0x64340||address==0x64b0e||
            address==0x64c2e||address==0x5a656)){skipCall();return;}
    if(address<0xe700||address>=0x724dc||++instructions>10000000)invalid=true;
    if(invalid)m68k_end_timeslice();
}
void compare(const char* stage) {
    for(unsigned a=0;a<original.size();++a) {
        ++compared;
        if(original[a]!=native[a]) {
            std::ostringstream error;error<<stage<<" case="<<cases<<" address=0x"<<std::hex<<a
                <<" original="<<unsigned(original[a])<<" native="<<unsigned(native[a]);
            throw std::runtime_error(error.str());
        }
    }
}
void run(unsigned entry,void(*routine)(Ram&),const char* stage,unsigned d0=0) {
    promotion=entry==0x64916;native=original;routine(native);
    instructions=0;invalid=returned=false;stackBytes.fill(0);
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    m68k_set_reg(M68K_REG_D0,d0);
    for(unsigned i=0;i<4;++i)stackBytes[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(200000000);
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stackTop) {
        std::ostringstream error;error<<"Original season routine failed at0x"<<std::hex<<pc;
        throw std::runtime_error(error.str());
    }
    compare(stage);maximumInstructions=std::max(maximumInstructions,instructions);++cases;
}
void prepare(unsigned variation) {
    original=baseline;
    original[0x5eb79]=11;original[0x1c9d0]=(variation&1)?11:0;
    scr::viewer::initializeNativeGameData(original);
    original[0x1c9d0]=(variation&1)?11:0;
    original[0x57c3c]=original[0x5eb76]=0;
    original[0x1c9ce]=std::uint8_t((variation>>1)&3);
    original[0x1bb5f]=original[0x1c9ce];original[0x1ca35]=std::uint8_t(variation%6);
    original[0x1bbb4]=std::uint8_t(((variation>>3)&3)<<6);
    for(unsigned a=0x625a2;a<0x625a7;++a)original[a]=std::uint8_t(next());
    for(unsigned i=0;i<12;++i){
        original[0x1c9de+i]=std::uint8_t(next()%8);
        original[0x1c9ea+i]=std::uint8_t(next()%8);
        original[0x1c9f6+i]=std::uint8_t(next()%16);
    }
    for(unsigned i=12;i>1;--i)std::swap(original[0x1c9c2+i-1],original[0x1c9c2+next()%i]);
}
void flowTest() {
    auto memory=std::make_unique<Ram>(baseline);
    (*memory)[0x5eb79]=11;scr::viewer::initializeNativeGameData(*memory);
    scr::viewer::initializeNativeMenuSession(*memory);scr::viewer::NativeMenuFlow flow(*memory);
    auto tick=[&](unsigned mask,unsigned keys=0){return flow.update({std::uint8_t(mask),std::uint8_t(keys)},1.0);};
    for(unsigned i=0;i<4;++i)tick(0);
    tick(0,4);for(unsigned i=0;i<4;++i)tick(0);
    tick(16);tick(0);
    if(flow.page()!=scr::viewer::NativeMenuPage::Matchup||!flow.competitive())throw std::runtime_error("Start Season route missing");
    unsigned humanRaces=0;
    while(flow.page()!=scr::viewer::NativeMenuPage::Main) {
        if(flow.page()==scr::viewer::NativeMenuPage::Matchup) {
            const unsigned ordinal=2*(*memory)[0x1c9ce]+((*memory)[0x1ca35]&1);
            if((*memory)[0x1ca33]!=(*memory)[0x648c2+ordinal]||(*memory)[0x1ca29]==11)
                throw std::runtime_error("Invalid original season opponent or track");
        }
        tick(0);tick(0);const auto action=tick(16);
        if(flow.page()==scr::viewer::NativeMenuPage::Preview) {
            tick(0);const auto start=tick(16);
            if(start!=scr::viewer::NativeMenuAction::StartRace)throw std::runtime_error("Season did not start competitive race");
            (*memory)[0x1bbb4]=std::uint8_t((humanRaces&3)<<6);flow.returnFromRace();++humanRaces;
        }
        (void)action;
        if(humanRaces>4)throw std::runtime_error("Season did not terminate");
    }
    if(humanRaces!=4)throw std::runtime_error("Original six-round schedule needs four human races");
}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){return read(a);}
unsigned int m68k_read_memory_16(unsigned int a){if(a&1)invalid=true;return (read(a)<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned int a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){write(a,v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(a&1)invalid=true;write(a,v>>8);write(a+1,v);}
void m68k_write_memory_32(unsigned int a,unsigned int v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try {
    if(argc!=2)throw std::runtime_error("Usage: native_opponent_race_tests payload.bin");
    std::ifstream file(argv[1],std::ios::binary);
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    if(payload.size()!=408540)throw std::runtime_error("Payload size mismatch");
    std::copy(payload.begin(),payload.end(),baseline.begin()+0xe700);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned variation=0;variation<128;++variation) {
        prepare(variation);run(0x5ab46,scr::viewer::configureNativeSeasonMatch,"configure-match");
        prepare(variation);run(0x5ee68,scr::viewer::randomizeNativeSeasonCars,"local-car-assignments");
        prepare(variation);run(0x5aa84,scr::viewer::scoreNativeSeasonRound,"score-and-sort-round");
        prepare(variation);run(0x64916,scr::viewer::finishNativeSeason,"promotion-and-relegation");
        if(variation<32){prepare(variation);run(0x609ae,scr::viewer::encodeNativeSeason,"league-encoder-rng");}
        prepare(variation);original[0x1ca33]=std::uint8_t(variation%8);
        for(unsigned a=0x7a61a;a<0x7a81a;++a)original[a]=std::uint8_t(next());
        run(0x5f25a,[](Ram& m){scr::viewer::transferNativeLapRecords(m,true);},"load-track-records",0x80);
        prepare(variation);original[0x1ca33]=std::uint8_t(variation%8);
        for(unsigned a=0x5ec48;a<0x5ec61;++a)original[a]=std::uint8_t(next());
        run(0x5f25a,[](Ram& m){scr::viewer::transferNativeLapRecords(m,false);},"save-track-records");
    }
    flowTest();
    std::cout<<"{\"cases\":"<<cases<<",\"compared_bytes\":"<<compared<<",\"mismatches\":0,\"maximum_instructions\":"<<maximumInstructions
        <<",\"whole_ram_compared\":true,\"original_code_unmodified\":true,\"promotion_drawing_calls_skipped\":true,\"local_season_human_races\":4}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
