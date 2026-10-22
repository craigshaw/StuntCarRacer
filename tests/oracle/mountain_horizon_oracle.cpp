// Test-only MC68000 execution. The selector, substitutions, word projection,
// screen roll and polygon-command consumer execute original bytes. At the
// line/pixel backend entry points the harness records commands and returns
// symbolic line handles; it does not claim original clipping/raster coverage.
#include "mountain_horizon.h"
#include "motion_transforms.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Ram=std::array<std::uint8_t,0x80000>;
Ram ram{},baseline{};std::array<std::uint8_t,4096> stackBytes{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,stopPc=0xa0000,lineBase=0x78000;
bool done=false,invalid=false,rendering=false;unsigned pcNow=0,instructions=0,color=0;
std::uint64_t totalInstructions=0,cases=0,vertexCases=0,lineCases=0,faceCases=0,loaderCases=0;
std::uint32_t randomState=0x4d4f554e;
struct Face {unsigned color{};std::vector<unsigned> lines;};
struct Instance {
    unsigned placement{},shape{};std::vector<scr::MountainPoint> vertices;
    std::vector<std::array<unsigned,2>> edges;std::vector<Face> faces;
};
std::vector<Instance> observed;
unsigned currentPlacement=0;
unsigned next(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
int signedWord(unsigned w){w&=65535;return w<32768?int(w):int(w)-65536;}
unsigned word(unsigned a){return (unsigned(ram[a])<<8)|ram[a+1];}
unsigned longword(unsigned a){return (word(a)<<16)|word(a+2);}
void putWord(unsigned a,unsigned v){ram[a]=std::uint8_t(v>>8);ram[a+1]=std::uint8_t(v);}
void putLong(unsigned a,unsigned v){putWord(a,v>>16);putWord(a+2,v);}
std::vector<std::uint8_t> load(const char* path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot read input");return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
unsigned read(unsigned a){
    if(a<ram.size())return ram[a];
    if(a>=stackBase&&a<stackTop)return stackBytes[a-stackBase];
    if(a>=stopPc&&a<stopPc+4){constexpr unsigned char stop[]{0x4e,0x72,0x27,0};return stop[a-stopPc];}
    invalid=true;return 0;
}
void write(unsigned a,unsigned v){
    if(a>=stackBase&&a<stackTop){stackBytes[a-stackBase]=std::uint8_t(v);return;}
    if((a>=0x69736&&a<0x6979e)||(a>=0x1bfb0&&a<0x1c230)||(a>=0x68148&&a<0x6814a)){
        ram[a]=std::uint8_t(v);return;
    }
    invalid=true;
}
unsigned reg(m68k_register_t r){return m68k_get_reg(nullptr,r);}
void returnFromBackend(){
    const unsigned stack=reg(M68K_REG_SP);
    const unsigned target=(read(stack)<<24)|(read(stack+1)<<16)|(read(stack+2)<<8)|read(stack+3);
    m68k_set_reg(M68K_REG_SP,stack+4);m68k_set_reg(M68K_REG_PC,target);
}
void hook(unsigned pc){
    pcNow=pc;++instructions;++totalInstructions;
    if(pc==stopPc){done=true;m68k_end_timeslice();return;}
    if(invalid||instructions>200000){invalid=true;m68k_end_timeslice();return;}
    if(rendering&&pc==0x69576)currentPlacement=reg(M68K_REG_D1)&255;
    if(rendering&&pc==0x69612){
        Instance instance{};instance.placement=currentPlacement;instance.shape=ram[0x69766+currentPlacement];
        const unsigned table=0x699b8+instance.shape*8,count=word(longword(table));
        for(unsigned n=0;n<count;++n)instance.vertices.push_back({signedWord(word(0x1bfb0+2*n)),signedWord(word(0x1c0f0+2*n))});
        observed.push_back(instance);
    }
    if(rendering&&pc==0x668a8){
        if(observed.empty()){invalid=true;return;}
        auto& instance=observed.back();
        const unsigned a=reg(M68K_REG_D1)&65535,b=reg(M68K_REG_D2)&65535;
        if((a|b)&1||a/2>=instance.vertices.size()||b/2>=instance.vertices.size()){invalid=true;return;}
        const unsigned id=unsigned(instance.edges.size());instance.edges.push_back({a/2,b/2});
        // Symbolic edge pointers keep all original polygon commands visible.
        // Their clip flag byte is zero: clipping is outside this test's claim.
        putLong(0x7b08a+word(0x68148),lineBase+id*16);
        m68k_set_reg(M68K_REG_D1,reg(M68K_REG_D1)&0xffff0000u);
        m68k_set_reg(M68K_REG_D2,reg(M68K_REG_D2)&0xffff0000u);
        returnFromBackend();return;
    }
    if(rendering&&pc==0x6770a){color=reg(M68K_REG_D0)&255;returnFromBackend();return;}
    if(rendering&&(pc==0x6950c||pc==0x66618)){
        Face face{};face.color=color;
        for(unsigned n=0;n<(pc==0x6950c?3u:4u);++n){
            const unsigned pointer=reg(static_cast<m68k_register_t>(M68K_REG_A0+n));
            if(pointer<lineBase||(pointer-lineBase)%16){invalid=true;return;}
            face.lines.push_back((pointer-lineBase)/16);
        }
        observed.back().faces.push_back(face);returnFromBackend();return;
    }
    const bool inMain=pc>=0x6953e&&pc<0x69736,inRoll=pc>=0x6252a&&pc<0x62574;
    if(!(inMain||inRoll)){invalid=true;m68k_end_timeslice();}
}
void execute(unsigned entry){
    done=invalid=false;instructions=0;observed.clear();m68k_pulse_reset();
    for(unsigned n=0;n<15;++n)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+n),0);
    m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_SR,0x2700);
    stackBytes[4092]=std::uint8_t((stopPc>>24)&255);stackBytes[4093]=std::uint8_t((stopPc>>16)&255);stackBytes[4094]=std::uint8_t((stopPc>>8)&255);stackBytes[4095]=std::uint8_t(stopPc&255);
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(50000000);
    if(invalid||!done){std::ostringstream s;s<<"Original mountain execution failed PC0x"<<std::hex<<pcNow;throw std::runtime_error(s.str());}
}
void compare(const scr::MountainHorizonData& data,std::uint16_t yaw,const scr::RenderProjectionCamera& camera){
    putWord(0x1bce6,yaw);putWord(0x1bc42,camera.viewportCenterY);putWord(0x1c250,camera.sinRoll);putWord(0x1c252,camera.cosRoll);
    const auto expected=scr::projectMountainHorizon(data,yaw,camera);rendering=true;execute(0x6953e);
    if(observed.size()!=expected.instances.size())throw std::runtime_error("Mountain selection count mismatch");
    for(unsigned i=0;i<observed.size();++i){
        const auto& actual=observed[i];const auto& native=expected.instances[i];const auto& shape=data.shapes[native.shapeIndex];
        if(actual.placement!=native.placementIndex||actual.shape!=native.shapeIndex||actual.vertices.size()!=native.vertices.size())throw std::runtime_error("Mountain selection order/shape mismatch");
        for(unsigned n=0;n<actual.vertices.size();++n){
            if(actual.vertices[n].x!=native.vertices[n].x||actual.vertices[n].y!=native.vertices[n].y)throw std::runtime_error("Original/native mountain vertex mismatch");
            ++vertexCases;
        }
        if(actual.edges.size()!=shape.edges.size()||actual.faces.size()!=shape.faces.size())throw std::runtime_error("Mountain command count mismatch");
        for(unsigned n=0;n<actual.edges.size();++n){
            if(actual.edges[n][0]!=shape.edges[n][0]||actual.edges[n][1]!=shape.edges[n][1])throw std::runtime_error("Mountain edge connectivity mismatch");
            ++lineCases;
        }
        for(unsigned n=0;n<actual.faces.size();++n){
            const auto& a=actual.faces[n];const auto& b=shape.faces[n];
            if(a.color!=b.color||a.lines.size()!=b.vertexCount)throw std::runtime_error("Mountain face palette/count mismatch");
            for(unsigned e=0;e<b.vertexCount;++e)if(a.lines[e]!=b.edges[e])throw std::runtime_error("Mountain polygon edge order mismatch");
            // Verify the native closed-boundary vertices use exactly those
            // original line commands, independent of winding/cycle start.
            for(unsigned e=0;e<b.vertexCount;++e){
                const auto v=b.vertices[e],w=b.vertices[(e+1)%b.vertexCount];bool found=false;
                for(unsigned f=0;f<b.vertexCount;++f){const auto edge=shape.edges[b.edges[f]];found|=(edge[0]==v&&edge[1]==w)||(edge[0]==w&&edge[1]==v);}
                if(!found)throw std::runtime_error("Native mountain polygon boundary differs from original edges");
            }
            ++faceCases;
        }
    }
    ++cases;
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
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("Usage: mountain_horizon_oracle payload chipram");
    const auto payload=load(argv[1]),capture=load(argv[2]);
    if(payload.size()!=408540||capture.size()!=baseline.size())throw std::runtime_error("Input extent mismatch");
    std::copy(capture.begin(),capture.end(),baseline.begin());
    for(const auto span:std::array<std::array<unsigned,2>,3>{{{0x6953e,0x69736},{0x6979e,0x69aca},{0x6252a,0x62574}}})
        if(!std::equal(payload.begin()+span[0]-0xe700,payload.begin()+span[1]-0xe700,baseline.begin()+span[0]))throw std::runtime_error("Captured original mountain code/table differs");
    const auto decoded=scr::decodeMountainHorizonData(payload.data(),payload.size());scr::TrigTable trig{};
    ram=baseline;for(unsigned n=0;n<trig.size();++n)trig[n]=std::uint16_t(word(0x1ca42+2*n));
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    rendering=false;
    for(unsigned track=0;track<256;++track){
        ram=baseline;std::fill(ram.begin()+0x69736,ram.begin()+0x69796,std::uint8_t(0xa5));ram[0x1ca33]=std::uint8_t(track);execute(0x696fc);
        if(ram[0x69799]!=decoded.placements.size())throw std::runtime_error("Original loader placement count mismatch");
        for(unsigned n=0;n<decoded.placements.size();++n)if(ram[0x69736+n]!=decoded.placements[n].horizontalAngle||ram[0x69766+n]!=decoded.placements[n].shapeIndex)throw std::runtime_error("Original loader placement data mismatch");
        ++loaderCases;
    }
    for(unsigned shape=0;shape<decoded.shapes.size();++shape){
        ram=baseline;ram[0x69799]=1;ram[0x69736]=10;ram[0x69766]=std::uint8_t(shape);
        auto synthetic=decoded;synthetic.placements={{10,std::uint8_t(shape)}};
        scr::RenderProjectionCamera camera{};camera.cosRoll=0x7fff;compare(synthetic,0x0a00,camera);
    }
    constexpr std::array<std::uint16_t,6> lowEdges{0,1,2,0xfd,0xfe,0xff};
    for(unsigned high=0;high<256;++high)for(const auto low:lowEdges){
        ram=baseline;rendering=false;execute(0x696fc);
        scr::RenderProjectionCamera camera{};camera.viewportCenterY=std::uint16_t(next());
        const auto roll=std::uint16_t(next());camera.sinRoll=scr::gameSine(roll,trig);camera.cosRoll=scr::gameCosine(roll,trig);
        compare(decoded,std::uint16_t((high<<8)|low),camera);
    }
    for(unsigned n=0;n<5000;++n){
        ram=baseline;rendering=false;execute(0x696fc);
        scr::RenderProjectionCamera camera{};camera.viewportCenterY=std::uint16_t(next());
        const auto roll=std::uint16_t(next());camera.sinRoll=scr::gameSine(roll,trig);camera.cosRoll=scr::gameCosine(roll,trig);
        compare(decoded,std::uint16_t(next()),camera);
    }
    std::cout<<"{\"total_cases\":"<<cases+loaderCases<<",\"projection_cases\":"<<cases<<",\"loader_cases\":"<<loaderCases
        <<",\"vertices_compared\":"<<vertexCases<<",\"line_commands_compared\":"<<lineCases<<",\"face_commands_compared\":"<<faceCases
        <<",\"mismatches\":0,\"original_instructions\":"<<totalInstructions<<",\"shape_variants\":25,\"placement_count\":"<<decoded.placements.size()
        <<",\"original_selector_and_substitution_executed\":true,\"original_screen_roll_executed\":true,\"original_polygon_command_consumer_executed\":true,\"original_code_unmodified\":true,\"line_and_pixel_backends_intercepted\":true,\"original_clipping_or_pixel_equality_claimed\":false}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"case="<<cases<<" "<<e.what()<<'\n';return 1;}
