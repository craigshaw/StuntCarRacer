#include "opponent_car_fixture.h"
#include "native_startup.h"
#include "motion_transforms.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
namespace f=scr::test::opponentCar;using Ram=scr::viewer::NativeMemory;
Ram ram{};constexpr unsigned stack=0x7f000,sentinel=0x7e000;
bool failed{},returned{};unsigned pc{},instructions{};std::uint64_t totalInstructions{},cases{},comparedEdges{},comparedCommands{};
struct Command {unsigned color{};std::array<unsigned,4> pointers{};};std::vector<Command> commands;
void ret(){const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);m68k_set_reg(M68K_REG_PC,f::longword(ram.data(),sp));m68k_set_reg(M68K_REG_SP,sp+4);}
void hook(unsigned address){
    pc=address;++instructions;++totalInstructions;
    if(address==sentinel){returned=true;m68k_end_timeslice();return;}
    if(address==0x66618){Command c{};c.color=ram[0x66475]>>4;for(unsigned n=0;n<4;++n)c.pointers[n]=m68k_get_reg(nullptr,static_cast<m68k_register_t>(M68K_REG_A0+n));commands.push_back(c);
        m68k_set_reg(M68K_REG_D1,0);m68k_set_reg(M68K_REG_D2,0);ret();return;}
    if(address<0xe700||address>=0x7abda||instructions>100000){failed=true;m68k_end_timeslice();}
}
void run(unsigned entry){
    failed=returned=false;instructions=0;commands.clear();f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,entry);f::putLong(ram.data(),stack,sentinel);m68k_pulse_reset();
    for(unsigned n=0;n<15;++n)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+n),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);m68k_execute(100000000);
    if(failed||!returned)throw std::runtime_error("Original opponent car failed pc="+std::to_string(pc));
}
unsigned read(unsigned a){if(a<ram.size())return ram[a];failed=true;return 0;}
void write(unsigned a,unsigned v){if(a<ram.size())ram[a]=std::uint8_t(v);else failed=true;}
int sw(unsigned v){return v<32768?int(v):int(v)-65536;}
using EdgeKey=std::array<int,5>;
EdgeKey key(unsigned packed){const auto p=packed&0xffffff;if(!p)return {-1,0,0,0,0};return {int(packed>>24),int(f::word(ram.data(),p+4)),int(f::word(ram.data(),p)),int(f::word(ram.data(),p+6)),int(f::word(ram.data(),p+2))};}
EdgeKey key(const scr::NearTrackEdge& e){if(!e.present)return {-1,0,0,0,0};return {e.clipFlags,e.clippedFrom.x,e.clippedFrom.y,e.clippedTo.x,e.clippedTo.y};}
std::uint32_t seed=0x434152;unsigned next(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
void check(const Ram& baseline,const scr::OpponentCarInputs& input,const scr::RenderProjectionTables& tables){
    ram=baseline;f::writeInputs(ram.data(),input);const auto native=scr::buildOpponentCarFrame(input,tables);run(0x67aa6);
    if(!native.admitted){if(!commands.empty())throw std::runtime_error("Rejected native opponent drew original commands");++cases;return;}
    for(unsigned n=0;n<4;++n){if(native.clampedHeights[n]!=f::word(ram.data(),0x1bf88+2*n))throw std::runtime_error("Opponent clamped height mismatch");
        if(std::uint16_t(native.footprintPoints[n].x)!=f::word(ram.data(),0x1c0a4+2*n)||std::uint16_t(native.footprintPoints[n].y)!=f::word(ram.data(),0x1c1e4+2*n))throw std::runtime_error("Opponent projected footprint mismatch");}
    for(unsigned n=0;n<16;++n)if(std::uint16_t(native.vertices[n].x)!=f::word(ram.data(),0x1c0b0+2*n)||std::uint16_t(native.vertices[n].y)!=f::word(ram.data(),0x1c1f0+2*n)){
        std::ostringstream out;out<<"Opponent vertex "<<n<<" mismatch native="<<native.vertices[n].x<<','<<native.vertices[n].y<<" original="<<sw(f::word(ram.data(),0x1c0b0+2*n))<<','<<sw(f::word(ram.data(),0x1c1f0+2*n));throw std::runtime_error(out.str());}
    for(unsigned n=0;n<36;++n){const auto expected=key(f::longword(ram.data(),0x7b08a+0x5e0+4*n)),actual=key(native.edges[n]);if(expected!=actual){std::ostringstream out;out<<"Opponent edge "<<n<<" mismatch ";for(auto v:actual)out<<v<<',';out<<" / ";for(auto v:expected)out<<v<<',';throw std::runtime_error(out.str());}++comparedEdges;}
    if(commands.size()!=native.commands.size())throw std::runtime_error("Opponent command count mismatch original="+std::to_string(commands.size())+" native="+std::to_string(native.commands.size()));
    for(unsigned n=0;n<commands.size();++n){const auto& a=commands[n];const auto& b=native.commands[n];std::array<EdgeKey,4> expected{},actual{};for(unsigned e=0;e<4;++e){expected[e]=key(a.pointers[e]);actual[e]=key(b.edges[e]);expected[e][0]=actual[e][0]=0;}std::sort(expected.begin(),expected.end());std::sort(actual.begin(),actual.end());
        if(a.color!=b.color||actual!=expected)throw std::runtime_error("Opponent ordered command mismatch");++comparedCommands;}
    if(f::longword(ram.data(),0x68144)!=0x3d80+native.lineBytes)throw std::runtime_error("Opponent edge allocation mismatch");++cases;
}
} // namespace
extern "C" {
unsigned m68k_read_memory_8(unsigned a){return read(a);}unsigned m68k_read_memory_16(unsigned a){return (read(a)<<8)|read(a+1);}unsigned m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){write(a,v);}void m68k_write_memory_16(unsigned a,unsigned v){write(a,v>>8);write(a+1,v);}void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::invalid_argument("Usage: opponent_car_oracle local.scr-assets");auto source=scr::viewer::startNativeGame(argv[1]);const auto tables=scr::test::renderProjection::readTables(source.memory->data());const auto trig=scr::test::renderProjection::readTrig(source.memory->data());
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    scr::OpponentCarInputs base{};base.relativeX={{std::uint16_t(-120),120,std::uint16_t(-120),120}};base.relativeZ={{1100,1100,1250,1250}};base.footprint={{900,940,1000,980}};base.bounds={{850,930,900}};
    base.camera.heightBaseline=3000;base.camera.cosRoll=0x7fff;base.distance=1100;base.roadOffset=128;
    for(unsigned distance:{0u,9u,10u,3199u,3200u,32767u,32768u,65535u})for(unsigned ahead:{0u,1u,127u,128u,255u}){auto in=base;in.distance=std::uint16_t(distance);in.aheadFlag=std::uint8_t(ahead);check(*source.memory,in,tables);}
    for(unsigned n=0;n<12000;++n){auto in=base;const int x=int(next()%9000)-4500,z=int(next()%6000)-1500,width=50+int(next()%300),length=80+int(next()%500);
        in.relativeX={{std::uint16_t(x-width),std::uint16_t(x+width),std::uint16_t(x-width),std::uint16_t(x+width)}};in.relativeZ={{std::uint16_t(z-length),std::uint16_t(z-length),std::uint16_t(z+length),std::uint16_t(z+length)}};
        for(auto& v:in.footprint)v=std::uint16_t(500+next()%5000);for(auto& v:in.bounds)v=std::uint16_t(500+next()%5000);
        in.camera.heightBaseline=std::uint16_t(1000+next()%8000);in.camera.yawResidual=std::uint16_t(int(next()%32000)-16000);in.camera.viewportCenterY=std::uint16_t(int(next()%10000)-5000);
        const auto roll=std::uint16_t(next());in.camera.sinRoll=scr::gameSine(roll,trig);in.camera.cosRoll=scr::gameCosine(roll,trig);in.roadOffset=std::uint16_t(next()%300);in.curveSmoothing=std::uint8_t(n%3==0?128:0);in.scaledPerspective=std::uint8_t(n%5==0?128:0);
        in.lineBytes=std::uint16_t(n%7==0?0x2300:n%11==0?0x2330:0);check(*source.memory,in,tables);}
    unsigned slots=0;for(unsigned relative:{0u,1u,127u,128u,255u})for(unsigned current:{0u,32u,64u,96u,0x5e0u})for(unsigned marker:{0u,32u,64u,96u,0x5e0u,0xffffu})for(unsigned slot=0;slot<3;++slot){
        ram=*source.memory;ram[0x57c69]=std::uint8_t(relative);f::putWord(ram.data(),0x69ae6,current);f::putWord(ram.data(),0x1bc12,marker);ram[0x1bbb8]=128;auto nativeMarker=std::uint16_t(marker);
        const bool native=scr::consumeOpponentCarPainterSlot(static_cast<scr::OpponentCarPainterSlot>(slot),std::uint8_t(relative),std::uint16_t(current),nativeMarker);run(slot==0?0x6916c:slot==1?0x6917c:0x69188);
        if(nativeMarker!=f::word(ram.data(),0x1bc12)||native!=(nativeMarker!=marker))throw std::runtime_error("Opponent painter marker mismatch");++slots;}
    std::cout<<"{\"cases\":"<<cases<<",\"edges\":"<<comparedEdges<<",\"commands\":"<<comparedCommands<<",\"painter_slots\":"<<slots<<",\"instructions\":"<<totalInstructions<<",\"mismatches\":0}\n";return 0;
}catch(const std::exception& e){std::cerr<<"opponent_car_oracle: "<<e.what()<<" case="<<cases<<'\n';return 1;}}
