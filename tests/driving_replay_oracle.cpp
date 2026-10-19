// Headless execution of the original main-loop gameplay interval. Renderer CPU
// instructions run; pixel output and asynchronous Amiga devices are boundaries.
#include "driving_replay_io.h"
#include "m68k.h"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
namespace a=scr::test::audio;
namespace replay=scr::test::replay;
namespace fx=scr::test::effects;
scr_test::ChipRam ram{};
std::unique_ptr<replay::NativeReplay> native;
unsigned currentTick=0;
std::string failure;
std::uint64_t compared=0,totalInstructions=0;
std::uint64_t comparedAudioWrites=0;
std::vector<scr::AudioRegisterWrite> originalAudio;
std::map<std::string,std::vector<unsigned>> projections;
std::map<std::string,unsigned> stages;
void compare(const std::string& stage,const std::vector<unsigned>& bytes){
    projections[stage]=bytes;
    ++stages[stage];
    for(auto address:bytes){++compared;if(ram[address]!=native->ram[address]){
        std::ostringstream s;s<<"tick="<<currentTick<<" stage="<<stage<<" address=0x"<<std::hex<<address<<std::dec<<" original="<<unsigned(ram[address])<<" native="<<unsigned(native->ram[address]);
        throw std::runtime_error(s.str());}}
}
std::vector<unsigned> range(unsigned a,unsigned count){std::vector<unsigned> v;while(count--)v.push_back(a++);return v;}
void stage(unsigned address){
    if(address==0x5d48a){native->controlsStage();std::vector<unsigned> v(scr::test::controls::byteOutputs.begin(),scr::test::controls::byteOutputs.end());for(auto a:range(0x625a2,4))v.push_back(a);v.push_back(0x1bd2a);v.push_back(0x1bd2b);compare("controls",v);}
    if(address==0x5d490){native->physicsStage();compare("physics",replay::physicsOutputs());}
    if(address==0x5d496){native->engineStage();compare("engine",{0x1bbf4,0x1bbf5,0x5b646,0x625a2,0x625a3,0x625a4,0x625a5,0x625a6});}
    if(address==0x64f80){native->progressStage();
        // Renderer arithmetic reuses polar/view scratch after this boundary.
        // Compare the complete persistent gameplay projection, including all
        // track outputs consumed by the next physics update.
        std::vector<unsigned> v{0x1bb1c,0x1bb9b,0x1bb9c,0x1bbd1,0x1bbe1,0x1bb41,0x1bb75,0x1bb9f,0x1bb9e};
        for(auto a0:{0x1bcd0,0x1bc5e,0x1bc5c,0x1bb10,0x1bd5a,0x1bb0a,0x1bc28})for(auto a1:range(a0,2))v.push_back(a1);
        for(auto a0:range(0x1bcd8,12))v.push_back(a0);for(auto a0:range(0x1c3c0,64))v.push_back(a0);compare("track",v);}
    if(address==0x65102){native->groundStage();compare("particles",range(0x1c380,256));compare("particle-rng",{0x625a2,0x625a3,0x625a4,0x625a5,0x625a6,0x1bba7});}
    if(address==0x65122){native->wheelsStage();compare("wheel-bounce",{0x1bbda});}
    if(address==0x5d4ae){native->damageStage();compare("damage",{0x1bb4f,0x1bb50,0x1bb51,0x1bb54,0x1bbb6,0x1bb55,0x662aa,0x662ab,0x1bb73,0x1c9cf,0x1bb6c,0x1bca2,0x1bbe2,0x1bc56,0x1bbdd,0x625a2,0x625a3,0x625a4,0x625a5,0x625a6});
        const auto frame=fx::damageBuffer(ram),nativeFrame=fx::damageBuffer(native->ram);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<256;++x){++compared;if(fx::occupied(ram,frame,x,y)!=fx::occupied(native->ram,nativeFrame,x,y))throw std::runtime_error("Damage-strip occupancy mismatch at pixel "+std::to_string(y*256+x));}}
    if(address==0x5d4b4){native->timingStage();compare("timing",replay::timingOutputs());}
}
std::array<std::uint8_t,4096> stackRam{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,stopPc=0xa0000,entry=0x5d47e,boundary=0x5d4ba;
unsigned pc=0,instructions=0,badAddress=0;bool invalid=false,done=false,running=false;
std::map<unsigned,unsigned> readPorts,writePorts;
unsigned joy=0,cia=0xff,ddra=0xff;
std::uint8_t readByte(unsigned address){
    if(address<ram.size())return ram[address];
    if(address>=stackBase&&address<stackTop)return stackRam[address-stackBase];
    if(address==0xbfe001)return std::uint8_t(cia);
    if(address==0xbfe201)return std::uint8_t(ddra);
    if(address==0xdff00c)return std::uint8_t(joy>>8);
    if(address==0xdff00d)return std::uint8_t(joy);
    if(address>=stopPc&&address<stopPc+4){constexpr std::uint8_t stop[4]={0x4e,0x72,0x27,0};return stop[address-stopPc];}
    // DMACONR: headless renderer sees a completed blit; no bus wait needed.
    if(address==0xdff002||address==0xdff003)return 0;
    badAddress=address;invalid=true;return 0;
}
void writeByte(unsigned address,std::uint8_t value){
    if(address<ram.size()){ram[address]=value;return;}
    if(address>=stackBase&&address<stackTop){stackRam[address-stackBase]=value;return;}
    if(address==0xbfe201){ddra=value;return;}
    if(address>=0xdff000&&address<0xdff200){++writePorts[address];return;}
    badAddress=address;invalid=true;
}
void hook(unsigned address){
    pc=address;
    if(invalid){m68k_end_timeslice();return;}
    try{stage(address);}catch(const std::exception& e){failure=e.what();invalid=true;m68k_end_timeslice();return;}
    if(address==boundary){done=true;m68k_set_reg(M68K_REG_PC,stopPc);m68k_end_timeslice();return;}
    if(address<0xe700||address>=0x724dc||++instructions>3000000){invalid=true;m68k_end_timeslice();}
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned int x){if(x>=0xdff000)++readPorts[x];return readByte(x);}
unsigned int m68k_read_memory_16(unsigned int x){return (unsigned(readByte(x))<<8)|readByte(x+1);}
unsigned int m68k_read_memory_32(unsigned int x){return (m68k_read_memory_16(x)<<16)|m68k_read_memory_16(x+2);}
void m68k_write_memory_8(unsigned int x,unsigned int v){writeByte(x,std::uint8_t(v));}
void m68k_write_memory_16(unsigned int x,unsigned int v){
    if(pc>=0xf362&&pc<0xf3ee&&x>=0xdff000&&x<0xdff200)originalAudio.push_back({x,std::uint16_t(v)});
    writeByte(x,std::uint8_t(v>>8));writeByte(x+1,std::uint8_t(v));}
void m68k_write_memory_32(unsigned int x,unsigned int v){m68k_write_memory_16(x,v>>16);m68k_write_memory_16(x+2,v);}
}
int main(int argc,char** argv)try{
    if(argc!=4)throw std::runtime_error("Usage: driving_replay_oracle chipram inputs.csv trace.csv");
    ram=*replay::loadCapture(argv[1]);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);m68k_pulse_reset();
    native=std::make_unique<replay::NativeReplay>(ram);
    const auto masks=replay::loadInputs(argv[2]);
    std::ofstream trace(argv[3]);if(!trace)throw std::runtime_error("Cannot create trace");
    replay::traceHeader(trace);
    const unsigned ticks=unsigned(masks.size());
    for(unsigned tick=0;tick<ticks;++tick){
        currentTick=tick;native->begin(masks[tick]);auto hardware=replay::hardware(masks[tick],std::uint8_t(ddra));joy=hardware.joy1dat;cia=hardware.ciaPortA;
        m68k_pulse_reset();
        std::fill_n(ram.begin()+0xead6,128,0);
        for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
        m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop);m68k_set_reg(M68K_REG_PC,entry);
        instructions=0;badAddress=0;invalid=done=false;originalAudio.clear();running=true;m68k_execute(80000000);running=false;
        if(!invalid&&done){
            if(originalAudio.size()!=native->audioWrites.size())failure="Ordered sample audio write count mismatch";
            else for(unsigned i=0;i<originalAudio.size();++i)if(originalAudio[i].address!=native->audioWrites[i].address||originalAudio[i].value!=native->audioWrites[i].value){failure="Ordered sample audio register mismatch";break;}
            for(const auto& name:{"controls","physics","engine","track","particles","particle-rng","wheel-bounce","damage","timing"})
                if(stages[name]!=tick+1)failure=std::string("Missing or repeated comparison stage: ")+name;
        }
        if(invalid||!done||!failure.empty()){
            const auto prefix=std::string(argv[3])+".failure";
            for(const auto& item:std::array<std::pair<const char*,const scr_test::ChipRam*>,2>{{{".original.bin",&ram},{".native.bin",&native->ram}}}){
                std::ofstream dump(prefix+item.first,std::ios::binary);dump.write(reinterpret_cast<const char*>(item.second->data()),item.second->size());}
            std::ofstream detail(prefix+".txt");detail<<"tick="<<tick<<" controls="<<masks[tick]<<" pc=0x"<<std::hex<<pc<<" address=0x"<<badAddress<<std::dec<<" instructions="<<instructions<<'\n'<<failure<<'\n';
            std::cerr<<"tick="<<tick<<" pc="<<std::hex<<pc<<" memory="<<badAddress<<std::dec<<" instructions="<<instructions<<'\n';throw std::runtime_error(failure.empty()?"Original loop failed":failure);}
        totalInstructions+=instructions;
        comparedAudioWrites+=originalAudio.size();replay::traceRow(trace,ram,tick,masks[tick],instructions);
    }
    std::cout<<"{\"ticks\":"<<ticks<<",\"mismatches\":0,\"compared_bytes\":"<<compared<<",\"original_instructions\":"<<totalInstructions
        <<",\"native_state_reseeded\":false,\"original_code_patched_by_harness\":false,\"registers_reset_between_ticks\":true,\"ordered_sample_audio_writes_compared\":true,\"audio_register_writes\":"<<comparedAudioWrites
        <<",\"whole_ram_compared\":false,\"interrupts_emulated\":false,\"damage_occupancy_compared\":true,\"per_tick_stages_complete\":true,\"interval\":{\"entry\":\"0x5d47e\",\"boundary_before\":\"0x5d4ba\"},\"stages\":{";
    bool first=true;for(const auto& s:stages){if(!first)std::cout<<',';first=false;std::cout<<'"'<<s.first<<"\":"<<s.second;}std::cout<<"},\"projection_byte_addresses\":{";
    first=true;for(const auto& p:projections){if(!first)std::cout<<',';first=false;std::cout<<'"'<<p.first<<"\":[";for(unsigned i=0;i<p.second.size();++i){if(i)std::cout<<',';std::cout<<p.second[i];}std::cout<<']';}std::cout<<"}}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
