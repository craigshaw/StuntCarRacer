#include "opponent_far_track.h"
#include "opponent_car.h"
#include "native_startup.h"
#include "track_mesh_capture.h"
#include "near_track_visibility_fixture.h"
#include "driving_replay_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace {
namespace f=scr::test::nearVisibility;using Ram=scr::viewer::NativeMemory;
Ram ram{};constexpr unsigned stack=0x7f000,sentinel=0x7e000;bool failed{},returned{},drawing{};unsigned pc{},instructions{};
std::uint64_t cases{},totalInstructions{},edgesCompared{},commandsCompared{},farAdmissions{},farMarkers{},denseMissingMarkers{},backgroundAllocations{};
using Key=std::array<int,5>;struct Command{unsigned kind{},color{};std::vector<Key> edges;};std::vector<Command> commands;using Event=std::tuple<unsigned,unsigned,unsigned>;std::vector<Event> events;
Key key(unsigned packed){const auto p=packed&0xffffff;if(!p)return {-1,0,0,0,0};return {int(packed>>24),int(f::word(ram.data(),p+4)),int(f::word(ram.data(),p)),int(f::word(ram.data(),p+6)),int(f::word(ram.data(),p+2))};}
Key key(const scr::NearTrackEdge& e){if(!e.present)return {-1,0,0,0,0};return {e.clipFlags,e.clippedFrom.x,e.clippedFrom.y,e.clippedTo.x,e.clippedTo.y};}
void ret(){const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);m68k_set_reg(M68K_REG_PC,f::longword(ram.data(),sp));m68k_set_reg(M68K_REG_SP,sp+4);}
void hook(unsigned address){pc=address;++instructions;++totalInstructions;if(address==sentinel){returned=true;m68k_end_timeslice();return;}
    if(drawing){
        if(address==0x6916c||address==0x6917c||address==0x69188)events.emplace_back(address==0x6916c?0:address==0x6917c?1:2,f::word(ram.data(),0x69ae6),unsigned(commands.size()));
        if(address==0x67aa6){commands.push_back({2,0,{}});ret();return;}
        if(address==0x66618){Command c{};c.color=ram[0x66475]>>4;for(unsigned n=0;n<4;++n)c.edges.push_back(key(m68k_get_reg(nullptr,static_cast<m68k_register_t>(M68K_REG_A0+n))));commands.push_back(std::move(c));m68k_set_reg(M68K_REG_D1,0);m68k_set_reg(M68K_REG_D2,0);ret();return;}
        if(address==0x66348){ret();return;}
        if(address==0x6932e){commands.push_back({1,15,{key(m68k_get_reg(nullptr,M68K_REG_A3))}});ret();return;}
        if(address==0x68544){Command c{};c.color=ram[0x69aec];const auto a=m68k_get_reg(nullptr,M68K_REG_A1),b=m68k_get_reg(nullptr,M68K_REG_A2);for(unsigned p=0x69b7c;p<a;p+=4)c.edges.push_back(key(f::longword(ram.data(),p)));for(unsigned p=b;p<0x69b7c;p+=4)c.edges.push_back(key(f::longword(ram.data(),p)));commands.push_back(std::move(c));m68k_set_reg(M68K_REG_D1,0);m68k_set_reg(M68K_REG_D2,0);ret();return;}
    }
    if(address<0xe700||address>=0x7abda||instructions>200000){failed=true;m68k_end_timeslice();}
}
void run(unsigned entry){failed=returned=false;instructions=0;commands.clear();events.clear();f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,entry);f::putLong(ram.data(),stack,sentinel);m68k_pulse_reset();for(unsigned n=0;n<15;++n)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+n),0);m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);m68k_execute(100000000);if(failed||!returned)throw std::runtime_error("Original far track failed pc="+std::to_string(pc));}
unsigned read(unsigned a){if(a<ram.size())return ram[a];if(a==0xdff002||a==0xdff003)return 0;failed=true;return 0;}void write(unsigned a,unsigned v){if(a<ram.size())ram[a]=std::uint8_t(v);else if(a<0xdff000||a>=0xdff200)failed=true;}
void serialize(const scr::NearTrackQueueState& q){f::writeQueueInputs(ram,q);unsigned cursor=f::lineBase;
    for(unsigned n=0;n<q.entries.size();++n)for(unsigned e=0;e<7;++e){const auto& edge=q.entries[n].edges[e];if(!edge.present)continue;f::putLong(ram.data(),f::queueBase+n*32+e*4,(std::uint32_t(edge.clipFlags)<<24)|cursor);
        f::putWord(ram.data(),cursor,edge.clippedFrom.y);f::putWord(ram.data(),cursor+2,edge.clippedTo.y);f::putWord(ram.data(),cursor+4,edge.clippedFrom.x);f::putWord(ram.data(),cursor+6,edge.clippedTo.x);for(unsigned a=8;a<edge.storageBytes;a+=2)f::putWord(ram.data(),cursor+a,0x8000);cursor+=edge.storageBytes;}
    if(cursor>f::lineBase+q.lineBytes)throw std::runtime_error("Serialized dense lines exceed original allocation");f::putLong(ram.data(),0x68144,f::lineBase+q.lineBytes);
}
void compareGeometry(const scr::OpponentFarTrackFrame& native){if(native.vertexEnd!=ram[0x660fa])throw std::runtime_error("Far projection end differs native="+std::to_string(native.vertexEnd)+" original="+std::to_string(ram[0x660fa]));
    for(unsigned n=2;n<native.vertexEnd/2;++n){for(unsigned lower=0;lower<2;++lower){const auto& p=lower?native.geometry.lower[n]:native.geometry.upper[n];const unsigned a=n*2+lower*120;
        if(p.height!=f::word(ram.data(),0x1be70+a)||std::uint16_t(p.x)!=f::word(ram.data(),0x1bfb0+a)||std::uint16_t(p.y)!=f::word(ram.data(),0x1c0f0+a)){std::ostringstream s;s<<"Far projected point differs n="<<n<<" lower="<<lower<<" native="<<p.height<<','<<p.x<<','<<p.y<<" original="<<f::word(ram.data(),0x1be70+a)<<','<<f::word(ram.data(),0x1bfb0+a)<<','<<f::word(ram.data(),0x1c0f0+a);throw std::runtime_error(s.str());}}
        if(!(n&1)){const auto& h=native.headers[n/2];if(h[0]!=ram[0x7ab5a+n*2]||h[1]!=ram[0x7ab5b+n*2])throw std::runtime_error("Far projected header differs");}
    }
}
void compareQueue(const scr::OpponentFarTrackFrame& native){const auto& q=native.queue;if(q.offset!=f::word(ram.data(),0x68148)||q.opponentOffset!=f::word(ram.data(),0x1bc12)||f::lineBase+q.lineBytes!=f::longword(ram.data(),0x68144))throw std::runtime_error("Far queue scalar differs native="+std::to_string(q.offset)+" original="+std::to_string(f::word(ram.data(),0x68148)));
    for(unsigned n=native.denseEnd/32;n<q.offset/32;++n){const auto& e=q.entries[n];if(e.segment!=ram[f::queueBase+n*32+28]||e.processedFlags!=ram[f::queueBase+n*32+30])throw std::runtime_error("Far queue header differs");for(unsigned j=0;j<7;++j){const auto actual=key(e.edges[j]),expected=key(f::longword(ram.data(),f::queueBase+n*32+4*j));if(actual!=expected){std::ostringstream s;s<<"Far edge differs entry="<<n<<" edge="<<j;throw std::runtime_error(s.str());}++edgesCompared;}}
}
void compareDraw(const scr::OpponentFarTrackFrame& frame,scr::NearTrackDrawInputs in,unsigned relative){std::vector<Event> actualEvents;auto marker=frame.queue.opponentOffset;
    const auto actual=scr::drawOpponentFarTrack(frame,in,[&](scr::NearTrackPainterSlot slot,std::uint16_t first,std::vector<scr::NearTrackDrawCommand>& out){actualEvents.emplace_back(unsigned(slot),first,unsigned(out.size()));if(scr::consumeOpponentCarPainterSlot(static_cast<scr::OpponentCarPainterSlot>(slot),std::uint8_t(relative),first,marker)){scr::NearTrackDrawCommand c{};c.surface=scr::NearTrackSurface::opponent;out.push_back(std::move(c));++farAdmissions;}});
    ram[0x1bb9a]=in.renderingOrderMode;f::putWord(ram.data(),0x1bc5c,in.offsetFromRoadCenter);ram[0x1bbdf]=in.raceStartTimer;ram[0x1bbd1]=in.offRoadSideFlags;ram[0x691b3]=in.sideEdgeColor;ram[0x691b2]=in.alternateSideColor;ram[0x57c69]=std::uint8_t(relative);f::putWord(ram.data(),0x1bc12,frame.queue.opponentOffset);f::putWord(ram.data(),0x68148,frame.queue.offset);f::putWord(ram.data(),0x6932c,frame.denseEnd);drawing=true;run(0x690bc);drawing=false;
    if(actualEvents!=events||marker!=f::word(ram.data(),0x1bc12)||commands.size()!=actual.size())throw std::runtime_error("Far painter events or count differ native="+std::to_string(actual.size())+" original="+std::to_string(commands.size()));
    for(unsigned n=0;n<actual.size();++n){const auto& a=actual[n];const auto& c=commands[n];const unsigned kind=a.surface==scr::NearTrackSurface::opponent?2:a.kind==scr::NearTrackCommandKind::line?1:0;std::vector<Key> keys;for(const auto& e:a.edges)keys.push_back(key(e));auto expected=c.edges;for(auto& e:keys)e[0]=0;for(auto& e:expected)e[0]=0;std::sort(keys.begin(),keys.end());std::sort(expected.begin(),expected.end());if(kind!=c.kind||(kind!=2&&a.color!=c.color)||keys!=expected)throw std::runtime_error("Far painter material/boundary differs command="+std::to_string(n));++commandsCompared;}
}
} // namespace
extern "C" {unsigned m68k_read_memory_8(unsigned a){return read(a);}unsigned m68k_read_memory_16(unsigned a){return (read(a)<<8)|read(a+1);}unsigned m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}void m68k_write_memory_8(unsigned a,unsigned v){write(a,v);}void m68k_write_memory_16(unsigned a,unsigned v){write(a,v>>8);write(a+1,v);}void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}}
int main(int argc,char** argv){try{if(argc!=2)throw std::invalid_argument("Usage: opponent_far_track_oracle local.scr-assets");m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned track=0;track<8;++track){auto source=scr::viewer::startNativeRace(argv[1],track);const auto tables=scr::test::renderProjection::readTables(source.memory->data());const auto mountains=scr::decodeMountainHorizonData(source.payload.data(),source.payload.size());scr::OpponentFarTrackData data{};data.mesh=scr::viewer::decodeTrackMesh(*source.memory);std::copy_n(source.memory->begin()+0x65e70,100,data.projectionLimits.begin());
        for(unsigned sample=0;sample<48;++sample){auto playback=std::make_unique<scr::test::replay::NativeReplay>(*source.memory);playback->restartStage(std::uint8_t((sample*7)%data.mesh.segmentCount));scr_test::writeWord(playback->ram,0x1bce6,std::uint16_t(scr_test::readWord(playback->ram,0x1bce6)+(sample%4)*0x4000));playback->progressStage();
            auto geometry=scr::test::nearGeometry::readState(playback->ram);scr::NearTrackFrameInputs in{};in.progress=scr::test::trackProgress::readState(playback->ram);in.camera=geometry.camera;in.displayUpdate=playback->ram[0x5d724];in.opponentSegment=std::uint8_t((in.progress.playerSegment+sample%7)%data.mesh.segmentCount);in.opponentSubSegment=std::uint8_t(sample%8);in.minimumBoundaryDistance=std::uint16_t(sample%3==0?0xff80:sample%3==1?0xfe00:0x100);
            std::array<std::uint8_t,100> flags{};for(unsigned n=0;n<data.mesh.segmentCount;++n)flags[n]=playback->ram[scr_test::readLong(playback->ram,0x7aa1a+4*n)];auto dense=scr::buildNearTrackFrame(playback->progressData,flags,in,geometry);const auto native=scr::buildOpponentFarTrack(data,tables,geometry,dense.queue);
            ram=playback->ram;run(0x696fc);scr::test::nearGeometry::writeState(ram,geometry);serialize(dense.queue);f::putWord(ram.data(),0x660fc,geometry.transformedCoordinates[0]);f::putWord(ram.data(),0x660fe,geometry.transformedCoordinates[1]);run(0x65ec4);compareGeometry(native);run(0x67460);compareQueue(native);
            if(native.queue.opponentOffset!=0xffff)++farMarkers;if(dense.queue.opponentOffset==0xffff&&native.queue.opponentOffset!=0xffff)++denseMissingMarkers;
            const auto drawn=std::make_unique<Ram>(ram);
            const auto allocation=scr::allocateOpponentBackgroundLines(native.queue.lineBytes,mountains,scr_test::readWord(playback->ram,0x1bce6),geometry.camera,geometry.edgeVisibilityMode);
            run(0x67888);run(0x6953e);const auto actualAllocation=f::longword(ram.data(),0x68144)-f::lineBase;
            if(allocation!=actualAllocation)throw std::runtime_error("Background arena allocation differs native="+std::to_string(allocation)+" original="+std::to_string(actualAllocation)+" before="+std::to_string(native.queue.lineBytes));++backgroundAllocations;
            for(unsigned mode:{0u,1u})for(int lateral:{-512,512})for(unsigned relative:{0u,1u,128u}){ram=*drawn;scr::NearTrackDrawInputs drawingInputs{};drawingInputs.renderingOrderMode=std::uint8_t(mode);drawingInputs.offsetFromRoadCenter=std::uint16_t(lateral);drawingInputs.sideEdgeColor=9;drawingInputs.alternateSideColor=10;compareDraw(native,drawingInputs,relative);}++cases;
        }
    }
    std::cout<<"{\"frames\":"<<cases<<",\"edges\":"<<edgesCompared<<",\"commands\":"<<commandsCompared<<",\"far_car_admissions\":"<<farAdmissions<<",\"frames_with_opponent_marker\":"<<farMarkers<<",\"markers_missing_from_dense_queue\":"<<denseMissingMarkers<<",\"background_allocations\":"<<backgroundAllocations<<",\"instructions\":"<<totalInstructions<<",\"mismatches\":0}\n";return 0;
}catch(const std::exception& e){std::cerr<<"opponent_far_track_oracle: "<<e.what()<<" frame="<<cases<<'\n';return 1;}}
