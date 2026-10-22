#include "opponent_car.h"
#include "native_startup.h"
#include "near_track_visibility_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace {
namespace f=scr::test::nearVisibility;using Ram=scr::viewer::NativeMemory;
Ram ram{};constexpr unsigned stack=0x7f000,sentinel=0x7e000;
bool failed{},returned{};unsigned pc{},instructions{},primitiveCount{},carDraws{};std::uint64_t cases{},totalInstructions{},slotCalls{},admissions{},emptySlots{};
using Event=std::tuple<unsigned,unsigned,unsigned>;std::vector<Event> events;
void ret(){const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);m68k_set_reg(M68K_REG_PC,f::longword(ram.data(),sp));m68k_set_reg(M68K_REG_SP,sp+4);}
void hook(unsigned address){
    pc=address;++instructions;++totalInstructions;
    if(address==sentinel){returned=true;m68k_end_timeslice();return;}
    if(address==0x6916c||address==0x6917c||address==0x69188)events.emplace_back(address==0x6916c?0:address==0x6917c?1:2,f::word(ram.data(),0x69ae6),primitiveCount);
    if(address==0x67aa6){++primitiveCount;++carDraws;ret();return;}
    if(address==0x66348||address==0x6932e||address==0x68544){if(address!=0x66348)++primitiveCount;if(address==0x68544){m68k_set_reg(M68K_REG_D1,0);m68k_set_reg(M68K_REG_D2,0);}ret();return;}
    if(address<0xe700||address>=0x7abda||instructions>100000){failed=true;m68k_end_timeslice();}
}
void run(){
    failed=returned=false;instructions=primitiveCount=carDraws=0;events.clear();f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,0x68f28);f::putLong(ram.data(),stack,sentinel);m68k_pulse_reset();
    for(unsigned n=0;n<15;++n)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+n),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,0x68f28);m68k_execute(100000000);
    if(failed||!returned)throw std::runtime_error("Original opponent painter failed pc="+std::to_string(pc));
}
unsigned read(unsigned a){if(a<ram.size())return ram[a];failed=true;return 0;}
void write(unsigned a,unsigned v){if(a<ram.size())ram[a]=std::uint8_t(v);else failed=true;}
std::uint32_t seed=0x5041494e;unsigned next(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
void serialize(const scr::NearTrackQueueState& q){
    f::writeQueueInputs(ram,q);unsigned cursor=f::lineBase;
    for(unsigned n=0;n<q.entries.size();++n)for(unsigned e=0;e<7;++e){const auto& edge=q.entries[n].edges[e];if(!edge.present)continue;
        f::putLong(ram.data(),f::queueBase+n*32+e*4,(std::uint32_t(edge.clipFlags)<<24)|cursor);
        f::putWord(ram.data(),cursor,edge.clippedFrom.y);f::putWord(ram.data(),cursor+2,edge.clippedTo.y);f::putWord(ram.data(),cursor+4,edge.clippedFrom.x);f::putWord(ram.data(),cursor+6,edge.clippedTo.x);
        for(unsigned a=8;a<edge.storageBytes;a+=2)f::putWord(ram.data(),cursor+a,0x8000);cursor+=edge.storageBytes;
    }
    f::putLong(ram.data(),0x68144,cursor);
}
scr::NearTrackQueueState queue(unsigned n){
    scr::NearTrackQueueState q{};const unsigned count=2+n%12;q.offset=std::uint16_t((count+1)*32);
    for(unsigned s=1;s<=count;++s){auto& v=q.entries[s];const int upperY=int(18+s*6),lowerY=upperY+7;
        int currentLeft=100-int(s*4),currentRight=156+int(s*4),previousLeft=currentLeft+4,previousRight=currentRight-4;
        if(n%7==0){currentLeft-=100;currentRight+=100;previousLeft-=100;previousRight+=100;}
        const scr::NearTrackPoint ur{currentRight,upperY},ul{currentLeft,upperY},lr{currentRight,lowerY},ll{currentLeft,lowerY};
        v.edges[0]=scr::makeNearTrackEdge(ur,{previousRight,upperY-6});v.edges[1]=scr::makeNearTrackEdge(ul,{previousLeft,upperY-6});
        v.edges[2]=scr::makeNearTrackEdge(lr,{previousRight,lowerY-6});v.edges[3]=scr::makeNearTrackEdge(ll,{previousLeft,lowerY-6});
        v.edges[4]=scr::makeNearTrackEdge(ur,lr);v.edges[5]=scr::makeNearTrackEdge(ll,ul);v.edges[6]=scr::makeNearTrackEdge(ul,ur);
        v.edgeColor=std::uint8_t(n%4==0?0x80:n%4==1?3:15);v.processedFlags=std::uint8_t((n%3==0&&s%3)?0x80:n%5==0?1:0);v.segment=std::uint8_t(s);
        // A missing vertical or road chain reproduces hill occlusion/empty
        // fill phases while the original painter still visits its car slots.
        if(n%11==0)for(auto& edge:v.edges)edge={};
        else if(n%5==0)v.edges[(s+n)%7]={};
        for(unsigned e=0;e<7;++e){v.edges[e].originQueueOffset=std::uint16_t(s*32);v.edges[e].originEdge=std::uint8_t(e);if(v.edges[e].present)q.lineBytes=std::uint16_t(q.lineBytes+v.edges[e].storageBytes);}
    }
    return q;
}
void check(const Ram& baseline,const scr::NearTrackQueueState& q,const scr::NearTrackDrawInputs& in,unsigned relative,unsigned marker){
    std::vector<Event> actual;auto nativeMarker=std::uint16_t(marker);unsigned nativeDraws=0;
    const auto native=scr::drawNearTrackQueue(q,in,[&](scr::NearTrackPainterSlot slot,std::uint16_t first,std::vector<scr::NearTrackDrawCommand>& out){
        actual.emplace_back(unsigned(slot),first,unsigned(out.size()));if(out.empty())++emptySlots;
        if(scr::consumeOpponentCarPainterSlot(static_cast<scr::OpponentCarPainterSlot>(slot),std::uint8_t(relative),first,nativeMarker)){scr::NearTrackDrawCommand c{};c.surface=scr::NearTrackSurface::opponent;out.push_back(std::move(c));++nativeDraws;}
    });
    ram=baseline;serialize(q);ram[0x1bb9a]=in.renderingOrderMode;f::putWord(ram.data(),0x1bc5c,in.offsetFromRoadCenter);ram[0x1bbdf]=in.raceStartTimer;ram[0x1bbd1]=in.offRoadSideFlags;
    ram[0x691b3]=in.sideEdgeColor;ram[0x691b2]=in.alternateSideColor;ram[0x1bb66]=in.previewColorOverride?0x80:0;f::putWord(ram.data(),0x1bc1e,in.minimumQueueOffset);ram[0x57c69]=std::uint8_t(relative);f::putWord(ram.data(),0x1bc12,marker);run();
    if(actual!=events){std::ostringstream out;out<<"Opponent painter slot/order mismatch native="<<actual.size()<<" original="<<events.size();const auto count=std::min(actual.size(),events.size());for(unsigned n=0;n<count;++n)if(actual[n]!=events[n]){out<<" event="<<n<<" native="<<std::get<0>(actual[n])<<','<<std::get<1>(actual[n])<<','<<std::get<2>(actual[n])<<" original="<<std::get<0>(events[n])<<','<<std::get<1>(events[n])<<','<<std::get<2>(events[n]);break;}throw std::runtime_error(out.str());}
    if(nativeDraws!=carDraws||nativeMarker!=f::word(ram.data(),0x1bc12)||native.size()!=primitiveCount)throw std::runtime_error("Opponent painter admission/marker mismatch");
    slotCalls+=actual.size();admissions+=nativeDraws;++cases;
}
} // namespace
extern "C" {
unsigned m68k_read_memory_8(unsigned a){return read(a);}unsigned m68k_read_memory_16(unsigned a){return (read(a)<<8)|read(a+1);}unsigned m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){write(a,v);}void m68k_write_memory_16(unsigned a,unsigned v){write(a,v>>8);write(a+1,v);}void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::invalid_argument("Usage: opponent_painter_oracle local.scr-assets");const auto source=scr::viewer::startNativeGame(argv[1]);m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned n=0;n<180;++n){const auto q=queue(n);for(unsigned mode:{0u,1u,0x80u})for(int lateral:{-512,-1,0,1,511})for(unsigned relative:{0u,1u,128u}){
        scr::NearTrackDrawInputs in{};in.renderingOrderMode=std::uint8_t(mode);in.offsetFromRoadCenter=std::uint16_t(lateral);in.raceStartTimer=std::uint8_t(n&1);in.offRoadSideFlags=std::uint8_t(n%3==0?0:n%3==1?2:128);in.sideEdgeColor=9;in.alternateSideColor=10;
        in.minimumQueueOffset=std::uint16_t(n%4==0?(n%((q.offset/32)+1))*32:0);const unsigned marker=n%6==0?0xffff:(next()%(q.offset/32+2))*32;check(*source.memory,q,in,relative,marker);
    }}
    std::cout<<"{\"cases\":"<<cases<<",\"painter_slots\":"<<slotCalls<<",\"car_admissions\":"<<admissions<<",\"empty_command_slots\":"<<emptySlots<<",\"instructions\":"<<totalInstructions<<",\"mismatches\":0}\n";return 0;
}catch(const std::exception& e){std::cerr<<"opponent_painter_oracle: "<<e.what()<<" case="<<cases<<'\n';return 1;}}
