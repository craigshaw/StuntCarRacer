#include "render_projection_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
namespace f=scr::test::renderProjection;
using Ram=std::array<std::uint8_t,0x80000>;
Ram ram{},baseline{};scr::TrigTable trig{};scr::RenderProjectionTables tables{};
constexpr unsigned stack=0x7f000,sentinel=0x7e000,record=0x78000;
unsigned pcNow=0,stopPc=0,instructions=0;bool executing=false,invalid=false,returned=false;
std::uint64_t cases=0,coordinateCases=0,worldCases=0,totalInstructions=0,excludedDomainCases=0;
std::uint32_t rng=0x50524f4a;
std::uint32_t next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
std::uint16_t randomWord(){return static_cast<std::uint16_t>(next());}
bool inside(unsigned a,unsigned n,unsigned begin,unsigned end){return a>=begin&&a<end&&n<=end-a;}
bool validRead(unsigned a,unsigned n){return inside(a,n,0,unsigned(ram.size()))&&(n==1||!(a&1));}
std::vector<std::uint8_t> readFile(const char* path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error(std::string("Cannot read ")+path);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void stop(){returned=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();}
void hook(unsigned pc){
    pcNow=pc;++instructions;++totalInstructions;
    if(invalid||instructions>20000){invalid=true;stop();return;}
    if(pc==sentinel||pc==stopPc){stop();return;}
    bool allowed=false;for(unsigned n=0;n+1<f::codeRanges.size();++n){auto r=f::codeRanges[n];allowed|=inside(pc,2,r.begin,r.end);}
    if(!allowed||(pc&1)){invalid=true;stop();}
}
bool allowedWrite(unsigned a,unsigned n){
    if(!executing||!validRead(a,n))return false;
    return inside(a,n,stack-256,stack)||inside(a,n,0x1bb00,0x1bd60)||inside(a,n,0x1be70,0x1c278)||
        inside(a,n,0x65ec0,0x65ec4)||inside(a,n,0x660fc,0x66102)||inside(a,n,0x7ab5a,0x7ab64);
}
void original(unsigned entry,unsigned boundary=sentinel,std::uint16_t x=0,std::uint16_t z=0){
    executing=false;f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,entry);f::putLong(ram.data(),stack,sentinel);
    f::putWord(ram.data(),sentinel,0x4e71);m68k_pulse_reset();
    for(unsigned n=0;n<15;++n)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+n),next());
    m68k_set_reg(M68K_REG_D0,(next()&0xffff0000u)|x);m68k_set_reg(M68K_REG_D3,(next()&0xffff0000u)|z);
    m68k_set_reg(M68K_REG_D1,next()&0xffff0000u);m68k_set_reg(M68K_REG_D2,next()&0xffff00ffu);
    m68k_set_reg(M68K_REG_SR,0x2700|(next()&31));m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);
    stopPc=boundary;instructions=0;invalid=returned=false;executing=true;m68k_execute(1000000);executing=false;
    if(invalid||!returned)throw std::runtime_error("Original projection failed at PC "+std::to_string(pcNow));
}
void comparePoint(const scr::RenderProjectedPoint& point,unsigned index){
    if(point.x!=f::signedWord(f::word(ram.data(),0x1bfb0+index))||point.y!=f::signedWord(f::word(ram.data(),0x1c0f0+index))||point.radialDistance!=f::word(ram.data(),0x1bc36)){
        std::cerr<<"Projection mismatch case="<<cases<<" original="<<f::signedWord(f::word(ram.data(),0x1bfb0+index))<<','<<f::signedWord(f::word(ram.data(),0x1c0f0+index))
            <<" native="<<point.x<<','<<point.y<<" radial="<<f::word(ram.data(),0x1bc36)<<'/'<<point.radialDistance<<'\n';
        throw std::runtime_error("Projection output mismatch");
    }
    ++cases;
}
void checkCoordinates(std::uint16_t x,std::uint16_t z,std::uint16_t y,const scr::RenderProjectionCamera& camera){
    f::putWord(ram.data(),0x1bc2e,camera.yawResidual);f::putWord(ram.data(),0x1bc42,camera.viewportCenterY);
    f::putWord(ram.data(),0x1c250,camera.sinRoll);f::putWord(ram.data(),0x1c252,camera.cosRoll);
    scr::RenderProjectedPoint expected{};
    try{expected=scr::projectRenderCoordinates(x,z,y,camera,tables);}catch(const std::out_of_range&){++excludedDomainCases;return;}
    original(0x6249c,sentinel,x,z);const auto radius=f::word(ram.data(),0x1bc36);
    original(0x624f0,sentinel,y,radius);original(0x62518);comparePoint(expected,0);++coordinateCases;
}
void checkWorld(const scr::RenderCameraInputs& input,unsigned gridX,unsigned gridZ,std::uint16_t localX,std::uint16_t localZ,std::uint16_t height){
    ram=baseline;f::writeCamera(ram.data(),input);
    const auto camera=scr::makeRenderProjectionCamera(input,trig);
    original(0x60190);original(0x61368);
    if(camera.quarterTurn!=f::word(ram.data(),0x1bc30)||camera.heightBaseline!=f::word(ram.data(),0x1bbfa)||camera.yawResidual!=f::word(ram.data(),0x1bc2e)||
        camera.sinRoll!=f::word(ram.data(),0x1c250)||camera.cosRoll!=f::word(ram.data(),0x1c252))throw std::runtime_error("Camera producer mismatch");
    f::putWord(ram.data(),0x1bc42,camera.viewportCenterY);
    ram[0x1bb85]=0;ram[0x1c588]=std::uint8_t((gridZ<<4)|gridX);f::putLong(ram.data(),0x7aa1a,record);
    f::putWord(ram.data(),record,0);f::putWord(ram.data(),record+2,localX);f::putWord(ram.data(),record+4,localZ);f::putWord(ram.data(),record+6,height);
    const scr::RenderWorldPoint point{double(gridX*128)+double(localX)/16,double(f::signedWord(height))/32,double(gridZ*128)+double(localZ)/16};
    const auto expected=scr::projectRenderWorldPoint(point,camera,tables);
    original(0x65ec4,0x6607a);comparePoint(expected,4);++worldCases;
}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){if(!validRead(a,1)){invalid=true;return 0;}return ram[a];}
unsigned int m68k_read_memory_16(unsigned int a){if(!validRead(a,2)){invalid=true;return 0;}return f::word(ram.data(),a);}
unsigned int m68k_read_memory_32(unsigned int a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){if(!allowedWrite(a,1)){invalid=true;return;}ram[a]=std::uint8_t(v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(!allowedWrite(a,2)){invalid=true;return;}f::putWord(ram.data(),a,std::uint16_t(v));}
void m68k_write_memory_32(unsigned int a,unsigned int v){if(!allowedWrite(a,4)){invalid=true;return;}f::putLong(ram.data(),a,v);}
}
int main(int argc,char** argv)try{
    if(argc!=3&&argc!=4)throw std::runtime_error("Usage: render_projection_oracle payload.bin [label] chipram.bin");
    const auto payload=readFile(argv[1]),capture=readFile(argv[argc-1]);
    if(payload.size()!=408540||capture.size()!=ram.size())throw std::runtime_error("Invalid payload/capture size");
    for(auto r:f::codeRanges)if(!std::equal(payload.begin()+r.begin-0xe700,payload.begin()+r.end-0xe700,capture.begin()+r.begin))throw std::runtime_error("Captured projection code/table differs");
    std::copy(capture.begin(),capture.end(),baseline.begin());ram=baseline;tables=f::readTables(ram.data());trig=f::readTrig(ram.data());
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    constexpr std::array<std::uint16_t,15> edge{0,1,2,7,31,32,255,1024,0x3fff,0x7fff,0x8001,0xc000,0xff00,0xfffe,0xffff};
    for(auto x:edge)for(auto z:edge)for(unsigned phase=0;phase<4;++phase){
        auto in=f::readCamera(baseline.data());in.roll=std::uint16_t(phase*0x4000);in.yaw=std::uint16_t(phase*0x4000+0x1fff);in.pitch=std::uint16_t(phase*0x800);
        checkCoordinates(x,z,std::uint16_t((int(x)+int(z))%8192),scr::makeRenderProjectionCamera(in,trig));
    }
    for(unsigned n=0;n<6000;++n){
        auto in=f::readCamera(baseline.data());in.roll=randomWord();in.yaw=randomWord();in.pitch=randomWord();
        auto x=randomWord(),z=randomWord();if(x==0x8000)++x;if(z==0x8000)++z;
        // Native valid magnitude domain also applies to the horizontal radius
        // consumed by vertical atan; avoid radial wrap into sign-minimum.
        x=std::uint16_t(f::signedWord(x)/2);z=std::uint16_t(f::signedWord(z)/2);
        checkCoordinates(x,z,std::uint16_t(int(randomWord()%8192)-4096),scr::makeRenderProjectionCamera(in,trig));
    }
    for(unsigned n=0;n<12000;++n){
        scr::RenderCameraInputs in{{next()%0x08000000,0x00050000+next()%0x01000000,next()%0x08000000},
            std::uint16_t(int(next()%8192)-4096),randomWord(),std::uint16_t(int(next()%8192)-4096),std::uint16_t(next()%2048)};
        if(n%16==0)in.worldPosition[0]&=0xffff0000u;if(n%17==0)in.worldPosition[2]&=0xffff0000u;
        checkWorld(in,next()%16,next()%16,std::uint16_t(next()%2049),std::uint16_t(next()%2049),std::uint16_t(next()%8192));
    }
    bool rejected=false;try{scr::projectRenderCoordinates(0x8000,1,1,{},tables);}catch(const std::out_of_range&){rejected=true;}
    if(!rejected)throw std::runtime_error("Projection accepted unsupported signed magnitude minimum");
    std::cout<<"{\"total_cases\":"<<cases<<",\"coordinate_cases\":"<<coordinateCases<<",\"world_vertex_cases\":"<<worldCases
        <<",\"mismatches\":0,\"original_instructions\":"<<totalInstructions<<",\"excluded_signed_magnitude_minimum_cases\":"<<excludedDomainCases
        <<",\"original_code_unmodified\":true,\"original_camera_and_matrix_producers_executed\":true,\"original_first_road_vertex_pipeline_executed\":true,\"near_clipping_is_viewer_choice\":true}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"case="<<cases<<' '<<e.what()<<'\n';return 1;}
