#include "track_sides_fixture.h"
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
namespace f=scr::test::sides;using Ram=scr_test::ChipRam;
Ram ram{},baseline{};constexpr unsigned stack=0x7f000,sentinel=0x7e000,queue=0x7b08a;
unsigned boundary=0,pcNow=0,instructions=0;std::uint64_t cases=0,totalInstructions=0,admissionCases=0,colorCases=0,geometryCases=0;
bool invalid=false,returned=false,executing=false;
std::uint32_t seed=0x53494445;
std::uint32_t next(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
std::vector<std::uint8_t> readFile(const char* p){std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("Cannot read input");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
bool inside(unsigned a,unsigned n,unsigned first,unsigned last){return a>=first&&a<last&&n<=last-a;}
bool valid(unsigned a,unsigned n){return inside(a,n,0,unsigned(ram.size()))&&(n==1||!(a&1));}
void stop(){returned=true;m68k_end_timeslice();}
void hook(unsigned pc){
    pcNow=pc;++instructions;++totalInstructions;if(pc==boundary||pc==sentinel){stop();return;}
    bool allowed=false;for(auto range:f::codeRanges)allowed|=inside(pc,2,range.begin,range.end);
    if(!allowed||instructions>10000){invalid=true;stop();}
}
void begin(unsigned entry,unsigned end){
    executing=false;f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,entry);f::putLong(ram.data(),stack,sentinel);m68k_pulse_reset();
    for(unsigned n=0;n<15;++n)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+n),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);
    boundary=end;instructions=0;invalid=returned=false;
}
void execute(){executing=true;m68k_execute(1000000);executing=false;if(invalid||!returned)throw std::runtime_error("Original side execution failed at PC "+std::to_string(pcNow));++cases;}
void admission(scr::TrackSide side,std::uint16_t current,std::uint16_t previous,std::uint8_t offRoad,std::uint8_t flags){
    ram=baseline;const unsigned index=side==scr::TrackSide::right?4:6;
    f::putWord(ram.data(),0x1bfb0+index,current);f::putWord(ram.data(),side==scr::TrackSide::right?0x660fc:0x660fe,previous);
    f::putWord(ram.data(),0x66100,std::uint16_t(flags)<<8);ram[0x1bbd1]=offRoad;
    f::putWord(ram.data(),0x1bbfa,4096);f::putWord(ram.data(),0x1bc36,4096);
    f::putWord(ram.data(),0x1bc42,0);f::putWord(ram.data(),0x1c250,0);f::putWord(ram.data(),0x1c252,32767);
    begin(0x65fca,0x66074);m68k_set_reg(M68K_REG_D1,index);m68k_set_reg(M68K_REG_A6,0x1be70);execute();
    const auto expected=scr::trackSideVertexRequired(side,current,previous,offRoad,flags);
    if(f::word(ram.data(),0x1be70+index+0x78)!=(expected?0x200:0x8000))throw std::runtime_error("Lower-vertex admission differs");
    if(expected)++geometryCases; // raw lower-height write checked above
    ++admissionCases;
}
void colors(std::uint8_t segment,std::uint8_t flags,scr::TrackSideStyle style){
    constexpr unsigned offset=0x40;ram=baseline;ram[queue+offset+0x1c]=segment;ram[queue+offset+0x1e]=flags;
    ram[0x691b3]=style.edgeColor;ram[0x691b2]=style.alternateFillColor;ram[0x1bb66]=style.nearColorOverride?0x80:0;
    f::putWord(ram.data(),0x68148,offset);f::putLong(ram.data(),queue+offset+0x18,0x70000);
    for(auto entry:std::array<unsigned,2>{0x68aa0,0x68c36}){
        begin(entry,entry==0x68aa0?0x68ad2:0x68c68);m68k_set_reg(M68K_REG_A4,queue);m68k_set_reg(M68K_REG_D3,offset);execute();
        if(ram[0x69aec]!=scr::trackSideFillColor(segment,style,true))throw std::runtime_error("Near side fill differs");++colorCases;
    }
    begin(0x68d82,0x68df4);execute();if(ram[0x69aec]!=scr::trackRoadFillColor(segment,flags))throw std::runtime_error("Road fill differs");++colorCases;
    for(auto entry:std::array<unsigned,2>{0x691b4,0x69270}){
        // Distinct nonzero edge pointers prove each quadrilateral selects the
        // matching upper/lower and current/previous cross-section edges.
        for(int displacement=-16;displacement<=24;displacement+=4)f::putLong(ram.data(),unsigned(int(queue+offset)+displacement),0x71000+unsigned(displacement+16)*16);
        ram[queue+offset+0x1c]=segment;begin(entry,entry==0x691b4?0x69268:0x69324);execute();
        if((ram[0x66475]>>4)!=scr::trackSideFillColor(segment,style))throw std::runtime_error("Far side fill differs");++colorCases;
        const std::array<int,4> edgeOffsets=entry==0x691b4?std::array<int,4>{12,-12,4,20}:std::array<int,4>{8,16,0,-16};
        for(unsigned n=0;n<4;++n)if(m68k_get_reg(nullptr,static_cast<m68k_register_t>(M68K_REG_A0+n))!=f::longword(ram.data(),unsigned(int(queue+offset)+edgeOffsets[n])))throw std::runtime_error("Far side edge topology differs");
        ++geometryCases;
    }
}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){if(!valid(a,1)){invalid=true;return 0;}return ram[a];}
unsigned int m68k_read_memory_16(unsigned int a){if(!valid(a,2)){invalid=true;return 0;}return f::word(ram.data(),a);}
unsigned int m68k_read_memory_32(unsigned int a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){if(!executing||!valid(a,1)){invalid=true;return;}ram[a]=std::uint8_t(v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(!executing||!valid(a,2)){invalid=true;return;}f::putWord(ram.data(),a,std::uint16_t(v));}
void m68k_write_memory_32(unsigned int a,unsigned int v){if(!executing||!valid(a,4)){invalid=true;return;}f::putLong(ram.data(),a,v);}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("Usage: track_sides_oracle payload.bin chipram.bin");const auto payload=readFile(argv[1]),capture=readFile(argv[2]);
    if(payload.size()!=408540||capture.size()!=ram.size())throw std::runtime_error("Input size differs");std::copy(capture.begin(),capture.end(),baseline.begin());
    for(auto range:f::codeRanges)if(range.begin!=0x691b2&&!std::equal(payload.begin()+range.begin-0xe700,payload.begin()+range.end-0xe700,baseline.begin()+range.begin))throw std::runtime_error("Captured side code/table differs");
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned side=0;side<2;++side)for(unsigned flags=0;flags<256;++flags)for(unsigned offRoad=0;offRoad<2;++offRoad)
        for(auto pair:std::array<std::pair<std::uint16_t,std::uint16_t>,5>{{{99,100},{100,100},{101,100},{0x8000,0x7fff},{0x7fff,0x8000}}})
            admission(side?scr::TrackSide::left:scr::TrackSide::right,pair.first,pair.second,std::uint8_t(offRoad),std::uint8_t(flags));
    for(unsigned n=0;n<1000;++n)admission(next()&1?scr::TrackSide::right:scr::TrackSide::left,std::uint16_t(next()),std::uint16_t(next()),std::uint8_t(next()),std::uint8_t(next()));
    for(unsigned n=0;n<256;++n)for(unsigned overrideFlag=0;overrideFlag<2;++overrideFlag)colors(std::uint8_t(n),std::uint8_t(n),{std::uint8_t(next()%16),std::uint8_t(next()%16),overrideFlag!=0});
    for(unsigned depth=0;depth<256;++depth)for(unsigned alternate=0;alternate<2;++alternate){
        ram=baseline;ram[0x1bb58]=std::uint8_t(depth);ram[0x69ad8]=std::uint8_t(alternate);
        begin(0x673ee,0x67438);m68k_set_reg(M68K_REG_A1,queue);m68k_set_reg(M68K_REG_D3,0x40);execute();
        if(ram[queue+0x41]!=scr::trackRoadEdgeColor(std::uint8_t(depth),alternate!=0))throw std::runtime_error("Near road edge color differs");++colorCases;
    }
    for(unsigned flags=0;flags<256;++flags){
        ram=baseline;const auto segment=std::uint8_t(next()%44);f::putWord(ram.data(),0x7ab5e,std::uint16_t((flags<<8)|segment));
        f::putWord(ram.data(),0x68148,0x5c);begin(0x676a2,0x676ce);m68k_set_reg(M68K_REG_D1,4);execute();
        if(ram[queue+0x5c]!=segment||ram[queue+0x5e]!=flags)throw std::runtime_error("Far queue header byte mapping differs");++geometryCases;
    }
    const auto mesh=scr::buildTrackMesh(scr::test::mesh::readData(baseline));const auto sides=scr::buildTrackSides(mesh,f::readStyle(baseline.data()));
    for(unsigned n=0;n<mesh.faces.size();++n){
        // Execute the complete original edge construction for one strip. The
        // four synthetic projected corners identify previous (D2=0) and next
        // (D1=4) cross-sections; original drawTrackLines emits seven real edges
        // and the header for those edges. This catches a start/end metadata
        // shift, including the cyclic last strip, independently of face build.
        ram=baseline;const unsigned current=(n+1)%unsigned(mesh.faces.size());
        f::putWord(ram.data(),0x7ab5e,f::word(baseline.data(),scr::test::mesh::streamBase+current*14));
        constexpr std::array<unsigned,4> index{0,2,4,6};
        constexpr std::array<unsigned,4> x{10,210,50,180},y{100,100,90,90};
        for(unsigned k=0;k<4;++k){
            f::putWord(ram.data(),0x1be70+index[k],1024);f::putWord(ram.data(),0x1be70+index[k]+0x78,512);
            f::putWord(ram.data(),0x1bfb0+index[k],std::uint16_t(x[k]));f::putWord(ram.data(),0x1c0f0+index[k],std::uint16_t(y[k]));
            f::putWord(ram.data(),0x1bfb0+index[k]+0x78,std::uint16_t(x[k]));f::putWord(ram.data(),0x1c0f0+index[k]+0x78,110);
        }
        f::putWord(ram.data(),0x68148,0x40);f::putLong(ram.data(),0x68144,0x3d80);
        begin(0x67460,0x676ce);execute();
        if(ram[queue+0x5c]!=mesh.faces[n].segment||ram[queue+0x5e]!=mesh.faces[n].flags)throw std::runtime_error("Native strip owns wrong original queue header");
        const auto rightEdge=f::longword(ram.data(),queue+0x40)&0xffffff;
        if(f::word(ram.data(),rightEdge)!=100||f::word(ram.data(),rightEdge+2)!=90||f::word(ram.data(),rightEdge+4)!=10||f::word(ram.data(),rightEdge+6)!=50)throw std::runtime_error("Original strip edges do not pair previous and current sections");
        ++geometryCases;
    }
    if(sides.size()!=mesh.faces.size()*2)throw std::runtime_error("Side strip count differs");
    for(unsigned n=0;n<sides.size();++n){const auto& side=sides[n];const auto& road=mesh.faces[n/2];const unsigned which=n&1;
        for(unsigned v=0;v<4;++v){const auto& p=side.vertices[v];const auto& top=mesh.vertices[road.indices[which?(v==0||v==3?3:2):(v==0||v==3?0:1)]];
            if(p.x!=top.x||p.z!=top.z||p.y!=(v<2?top.y:16))throw std::runtime_error("Native side geometry differs");++geometryCases;}
    }
    bool rejected=false;try{(void)scr::buildTrackSides(mesh,{16,0,false});}catch(const std::invalid_argument&){rejected=true;}if(!rejected)throw std::runtime_error("Invalid edge palette accepted");
    rejected=false;try{(void)scr::buildTrackSides(mesh,{0,16,false});}catch(const std::invalid_argument&){rejected=true;}if(!rejected)throw std::runtime_error("Invalid fill palette accepted");
    auto broken=mesh;broken.faces[0].indices[0]=unsigned(mesh.vertices.size());rejected=false;
    try{(void)scr::buildTrackSides(broken,{0,0,false});}catch(const std::out_of_range&){rejected=true;}if(!rejected)throw std::runtime_error("Invalid road index accepted");
    std::cout<<"{\"total_cases\":"<<cases<<",\"admission_cases\":"<<admissionCases<<",\"material_cases\":"<<colorCases<<",\"geometry_cases\":"<<geometryCases<<",\"side_quads\":"<<sides.size()<<",\"original_instructions\":"<<totalInstructions<<",\"mismatches\":0,\"original_code_unmodified\":true,\"synthetic_branch_context\":true,\"ground_raw_height\":512,\"ground_world_y\":16,\"native_visibility_queue_claimed\":false}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"case="<<cases<<' '<<e.what()<<'\n';return 1;}
