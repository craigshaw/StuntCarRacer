// Actual original RLE/masked-object byte execution. Test-only Musashi.
#include "planar_image.h"
#include "replay_render_assets.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::array<std::uint8_t,0x80000> ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,stopPc=0xa0000,frame=0x6a594;
bool done=false,invalid=false;unsigned instructions=0;
std::uint8_t read(unsigned a) {
    if(a<ram.size())return ram[a];
    if(a>=stackBase&&a<stackTop)return stack[a-stackBase];
    if(a>=stopPc&&a<stopPc+4){constexpr std::uint8_t stop[]{0x4e,0x72,0x27,0};return stop[a-stopPc];}
    invalid=true;return 0;
}
void write(unsigned a,std::uint8_t value) {
    if(a<ram.size())ram[a]=value;
    else if(a>=stackBase&&a<stackTop)stack[a-stackBase]=value;
    else invalid=true;
}
void putLong(unsigned a,unsigned value) {for(unsigned i=0;i<4;++i)write(a+i,std::uint8_t(value>>(24-i*8)));}
void hook(unsigned pc) {
    if(pc==stopPc){done=true;m68k_end_timeslice();return;}
    if(invalid||++instructions>1000000)m68k_end_timeslice();
}
std::vector<std::uint8_t> load(const char* name) {
    std::ifstream f(name,std::ios::binary);
    if(!f)throw std::runtime_error("Cannot read input");
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
void start(unsigned entry) {
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    putLong(stackTop-4,stopPc);m68k_set_reg(M68K_REG_PC,entry);
    done=invalid=false;instructions=0;
}
void execute() {
    m68k_execute(50000000);
    if(!done||invalid)throw std::runtime_error("Original image routine failed");
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){return read(a);}
unsigned int m68k_read_memory_16(unsigned int a){return (unsigned(read(a))<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned int a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){write(a,std::uint8_t(v));}
void m68k_write_memory_16(unsigned int a,unsigned int v){write(a,std::uint8_t(v>>8));write(a+1,std::uint8_t(v));}
void m68k_write_memory_32(unsigned int a,unsigned int v){putLong(a,v);}
}
int main(int argc,char** argv)try {
    if(argc!=3)throw std::runtime_error("Usage: planar_image_oracle payload chipram");
    const auto payload=load(argv[1]),capture=load(argv[2]);
    if(payload.size()!=408540||capture.size()!=ram.size())throw std::runtime_error("Input extent mismatch");
    for(const auto span:std::array<std::pair<unsigned,unsigned>,3>{{{0x593f8,0x59450},{0x69f42,0x69fe8},{0x1ba82,0x1bace}}})
        if(!std::equal(payload.begin()+span.first-0xe700,payload.begin()+span.second-0xe700,capture.begin()+span.first))
            throw std::runtime_error("Original image code differs in capture");
    const auto decoded=scr::decodePlanarRle(payload.data()+0x11aa4,payload.size()-0x11aa4,320,200,4);
    const auto assets=scr::viewer::decodeReplayRenderAssets(payload.data(),payload.size(),capture.data(),capture.size());
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    std::copy(capture.begin(),capture.end(),ram.begin());
    start(0x593f8);m68k_set_reg(M68K_REG_A0,0x201a4);m68k_set_reg(M68K_REG_A1,frame);execute();
    unsigned totalInstructions=instructions;
    if(!std::equal(decoded.planes.begin(),decoded.planes.end(),ram.begin()+frame)||
        m68k_get_reg(nullptr,M68K_REG_A0)!=0x201a4+decoded.consumed)
        throw std::runtime_error("Original/native RLE pixels or consumed byte count differ");
    unsigned cases=1;
    std::copy(capture.begin(),capture.end(),ram.begin());start(0x1ba82);execute();totalInstructions+=instructions;
    for(unsigned i=0;i<16;++i) {
        const auto hardware=m68k_read_memory_16(0xe770+i*4);
        const auto rgb=(((hardware>>8)&15)*17u<<16)|(((hardware>>4)&15)*17u<<8)|((hardware&15)*17u);
        if(rgb!=assets.palette[i])throw std::runtime_error("Original/native palette colour differs");
        ++cases;
    }
    for(unsigned id:{0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,49u,50u}) {
        const unsigned first=id<6 ? 101 : 0,last=id<6 ? 135 : 0;
        for(unsigned wheelY=first;wheelY<=last;++wheelY) {
            std::copy(capture.begin(),capture.end(),ram.begin());
            std::copy(decoded.planes.begin(),decoded.planes.end(),ram.begin()+frame);
            putLong(0x6a58c,frame);
            if(id<6) {
                const unsigned record=0x6a16c+id*16;
                m68k_write_memory_16(record+10,wheelY);m68k_write_memory_16(record+6,158-wheelY);
            }
            const auto state=scr::viewer::captureCockpitFrame(ram.data(),ram.size());
            std::vector<std::uint32_t> nativePixels;scr::viewer::makeReplayBackground(assets,nativePixels);
            scr::viewer::compositeMaskedObject(assets,id,state.objects[id],nativePixels);
            start(0x69f42);m68k_set_reg(M68K_REG_D0,id);execute();totalInstructions+=instructions;
            const auto original=scr::decodePlanarImage(ram.data()+frame,32000,320,200,4);
            for(unsigned i=0;i<original.pixels.size();++i)
                if(nativePixels[i]!=assets.palette[original.pixels[i]])
                    throw std::runtime_error("Original/native masked pixels differ for object"+std::to_string(id));
            ++cases;
        }
    }
    std::cout<<"{\"total_cases\":"<<cases<<",\"original_instructions\":"<<totalInstructions<<",\"rle_consumed\":"<<decoded.consumed<<",\"masked_object_ids\":17,\"mismatches\":0}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
