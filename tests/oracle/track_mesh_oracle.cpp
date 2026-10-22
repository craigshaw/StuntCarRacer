#include "track_mesh_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>

namespace {
namespace f=scr::test::mesh;
scr_test::ChipRam ram{},baseline{};
std::array<std::uint8_t,4096> stackRam{};
constexpr unsigned stackBase=0x8f000,stack=0x90000,sentinel=0xa0000;
unsigned pc=0,instructions=0,maximumInstructions=0,selectedPoint=0;
std::uint64_t verticesChecked=0,generatedBytes=0;bool invalid=false,returned=false,projecting=false,horizontalChecked=false;
scr::TrackMeshVertex expected{};std::string failure;
std::vector<std::uint8_t> readFile(const char* p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot read input");return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
int signedWord(unsigned n){n&=65535;return n<32768?int(n):int(n)-65536;}
void hook(unsigned address){
    pc=address;
    if(projecting&&address==0x65f42)m68k_set_reg(M68K_REG_A6,selectedPoint);
    if(projecting&&address==0x65fa0){
        const auto x=signedWord(m68k_get_reg(nullptr,M68K_REG_D0)),z=signedWord(m68k_get_reg(nullptr,M68K_REG_D3));
        if(x!=int(std::floor(expected.x*8))||z!=int(std::floor(expected.z*8))){failure="Original X/Z coordinate prefix differs from native mesh";invalid=true;}
        horizontalChecked=true;
    }
    if(projecting&&address==0x65fc4){
        const auto y=signedWord(m68k_get_reg(nullptr,M68K_REG_D0));
        if(!horizontalChecked||y!=int(std::floor((4096-expected.y*32)/8))){failure="Original vertical coordinate prefix differs from native mesh";invalid=true;}
        returned=true;m68k_end_timeslice();return;
    }
    if(address==sentinel){returned=true;m68k_end_timeslice();return;}
    if(address<0xe700||address>=0x7aa1a||++instructions>2000000)invalid=true;
    if(invalid)m68k_end_timeslice();
}
unsigned read(unsigned a){if(a<ram.size())return ram[a];if(a>=stackBase&&a<stack)return stackRam[a-stackBase];if(a>=sentinel&&a<sentinel+4){constexpr unsigned char stop[]{0x4e,0x72,0x27,0};return stop[a-sentinel];}invalid=true;return 0;}
void write(unsigned a,unsigned v){if(a<ram.size()){ram[a]=std::uint8_t(v);return;}if(a>=stackBase&&a<stack){stackRam[a-stackBase]=std::uint8_t(v);return;}invalid=true;}
void original(unsigned entry,unsigned a6=0){
    invalid=returned=horizontalChecked=false;failure.clear();instructions=0;m68k_pulse_reset();
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_D1,4);m68k_set_reg(M68K_REG_A6,a6);m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stack-4);
    stackRam[4092]=std::uint8_t(sentinel>>24);stackRam[4093]=std::uint8_t(sentinel>>16);stackRam[4094]=std::uint8_t(sentinel>>8);stackRam[4095]=std::uint8_t(sentinel);
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(40000000);
    if(invalid||!returned){std::ostringstream s;s<<"Original mesh execution failed PC0x"<<std::hex<<pc<<" "<<failure;throw std::runtime_error(s.str());}
    maximumInstructions=std::max(maximumInstructions,instructions);
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){return read(a);}
unsigned int m68k_read_memory_16(unsigned int a){if(a&1)invalid=true;return (read(a)<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned int a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){write(a,v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(a&1)invalid=true;write(a,v>>8);write(a+1,v);}
void m68k_write_memory_32(unsigned int a,unsigned int v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("Usage: track_mesh_oracle payload.bin chipram.bin");
    const auto payload=readFile(argv[1]),capture=readFile(argv[2]);if(payload.size()!=408540||capture.size()!=ram.size())throw std::runtime_error("Input size");
    std::copy(capture.begin(),capture.end(),baseline.begin());
    for(const auto range:std::array<std::pair<unsigned,unsigned>,3>{{{0x65bec,0x65e30},{0x65ec4,0x660fa},{0x5c6c4,0x5c808}}})
        if(!std::equal(payload.begin()+range.first-0xe700,payload.begin()+range.second-0xe700,baseline.begin()+range.first))throw std::runtime_error("Captured mesh code differs");
    const auto cached=f::readData(baseline);const auto mesh=scr::buildTrackMesh(cached);const auto expectedEnd=scr_test::readLong(baseline,f::endPointer);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    ram=baseline;scr_test::writeWord(ram,0x1bb22,0);scr_test::writeWord(ram,0x1bb26,0);ram[0x1bb7f]=0;
    projecting=false;original(0x65bec);
    if(scr_test::readLong(ram,f::endPointer)!=expectedEnd)throw std::runtime_error("Regenerated stream length differs");
    for(unsigned a=f::streamBase;a<expectedEnd;++a)if(ram[a]!=baseline[a]){std::ostringstream s;s<<"Regenerated cache differs at0x"<<std::hex<<a<<" original="<<unsigned(ram[a])<<" saved="<<unsigned(baseline[a]);throw std::runtime_error(s.str());}
    for(unsigned a=f::pointerTable;a<f::pointerTable+cached.segmentCount*4;++a)if(ram[a]!=baseline[a])throw std::runtime_error("Regenerated segment pointer table differs");
    generatedBytes=expectedEnd-f::streamBase;
    for(unsigned section=0;section<cached.sections.size();++section)for(unsigned side=0;side<2;++side){
        ram=baseline;ram[0x1bb04]=ram[0x1bb06]=ram[0x1bb23]=ram[0x1bb27]=ram[0x1bb2e]=ram[0x1bb32]=0;
        scr_test::writeWord(ram,0x1bc30,0);scr_test::writeWord(ram,0x1bbfa,4096);
        const auto record=f::streamBase+section*14;selectedPoint=record+2+side*6;expected=mesh.vertices[section*2+side];projecting=true;original(0x65edc,record);++verticesChecked;
    }
    if(mesh.faces.back().indices[1]!=0||mesh.faces.back().indices[2]!=1)throw std::runtime_error("Road strip does not close");
    std::cout<<"{\"total_cases\":"<<verticesChecked+1<<",\"mismatches\":0,\"sections\":"<<cached.sections.size()<<",\"vertices\":"<<mesh.vertices.size()<<",\"faces\":"<<mesh.faces.size()<<",\"regenerated_original_cache_bytes\":"<<generatedBytes<<",\"generated_cache_matches_capture\":true,\"original_vertex_prefix_cases\":"<<verticesChecked<<",\"maximum_instructions\":"<<maximumInstructions<<",\"original_code_unmodified\":true,\"synthetic_projection_context\":true,\"projection_context\":\"zero view origin and quadrant, camera height4096; A6 selects each saved vertex at original loop head\"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
