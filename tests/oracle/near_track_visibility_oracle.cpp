#include "near_track_visibility_fixture.h"
#include "render/near_track_frame.h"
#include "driving_replay_io.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
namespace {
namespace f=scr::test::nearVisibility;using Ram=f::Ram;
Ram ram{},baseline{};constexpr unsigned stack=0x7f000,sentinel=0x7e000;
unsigned boundary=0,pcNow=0,instructions=0,lastColor=0;bool invalid=false,returned=false,executing=false,visibilityMode=false,drawMode=false,frameMode=false;
std::uint64_t cases=0,totalInstructions=0,lineCases=0,visibilityCases=0,queueCases=0,drawCases=0,drawCommands=0;
std::uint64_t frameCases=0,framePasses=0;unsigned frameTick=0;
std::uint32_t seed=0x4e454152;std::vector<unsigned> requests;
struct OriginalCommand {bool line{};unsigned surface{},color{};std::vector<unsigned> pointers;};
std::vector<OriginalCommand> commands;
std::uint32_t next(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int sw(unsigned v){v&=65535;return v<32768?int(v):int(v)-65536;}
std::vector<std::uint8_t> readFile(const char* p){std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("Cannot read input");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
bool inside(unsigned a,unsigned n,unsigned first,unsigned end){return a>=first&&a<end&&n<=end-a;}
bool valid(unsigned a,unsigned n){return inside(a,n,0,unsigned(ram.size()))&&(n==1||!(a&1));}
void stop(){returned=true;m68k_end_timeslice();}
void ret(){const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);m68k_set_reg(M68K_REG_PC,f::longword(ram.data(),sp));m68k_set_reg(M68K_REG_SP,sp+4);}
void hook(unsigned pc){
    pcNow=pc;++instructions;++totalInstructions;
    if(pc==boundary||pc==sentinel){stop();return;}
    if(visibilityMode&&pc==0x65720){
        const auto index=m68k_get_reg(nullptr,M68K_REG_D1)&255;requests.push_back(index);
        f::putWord(ram.data(),0x1be70+index+120,512);f::putWord(ram.data(),0x1bfb0+index+120,std::uint16_t(f::word(ram.data(),0x1bfb0+index)+1));
        f::putWord(ram.data(),0x1c0f0+index+120,std::uint16_t(f::word(ram.data(),0x1c0f0+index)+20));ret();return;
    }
    if(drawMode){
        if(pc==0x66348){lastColor=m68k_get_reg(nullptr,M68K_REG_D0)&15;ret();return;}
        if(pc==0x6932e){commands.push_back({true,0,lastColor,{m68k_get_reg(nullptr,M68K_REG_A3)}});ret();return;}
        if(pc==0x68544){
            OriginalCommand c{};c.surface=ram[0x684a9];c.color=ram[0x69aec];
            const auto a=m68k_get_reg(nullptr,M68K_REG_A1),b=m68k_get_reg(nullptr,M68K_REG_A2);
            for(unsigned p=0x69b7c;p<a;p+=4)c.pointers.push_back(f::longword(ram.data(),p));
            for(unsigned p=b;p<0x69b7c;p+=4)c.pointers.push_back(f::longword(ram.data(),p));
            commands.push_back(std::move(c));m68k_set_reg(M68K_REG_D1,0);m68k_set_reg(M68K_REG_D2,0);ret();return;
        }
        if(pc==0x6916c||pc==0x6917c||pc==0x69188){ret();return;}
    }
    bool allowed=false;for(auto range:std::array<std::pair<unsigned,unsigned>,9>{{{0x655ea,0x65756},{0x617d0,0x6181e},{0x668a8,0x66fb8},
        {0x66ff0,0x6770a},{0x67740,0x67888},{0x688fc,0x690bc},{0x6814a,0x684a6},{0x68544,0x68546},{0x1ca42,0x1ec46}}})allowed|=inside(pc,2,range.first,range.second);
    if(frameMode)allowed|=inside(pc,2,0x58000,0x65bec);
    if(!allowed||instructions>1000000){invalid=true;stop();}
}
void begin(unsigned entry,unsigned end=sentinel){
    executing=false;f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,entry);f::putLong(ram.data(),stack,sentinel);m68k_pulse_reset();
    for(unsigned n=0;n<15;++n)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+n),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);
    boundary=end;instructions=0;invalid=returned=false;
}
void execute(){executing=true;m68k_execute(50000000);executing=false;if(invalid||!returned)throw std::runtime_error("Original near execution failed PC "+std::to_string(pcNow));++cases;}
void compareEdge(const scr::NearTrackEdge& edge,unsigned packed,const std::string& label,bool compareBytes=false){
    const auto pointer=packed&0xffffff;
    if(edge.present!=(pointer!=0))throw std::runtime_error(label+" edge presence differs");if(!pointer)return;
    const auto flags=packed>>24;
    if(edge.clipFlags!=flags||edge.clippedFrom.x!=f::word(ram.data(),pointer+4)||edge.clippedFrom.y!=f::word(ram.data(),pointer)||
       edge.clippedTo.x!=f::word(ram.data(),pointer+6)||edge.clippedTo.y!=f::word(ram.data(),pointer+2)){
        std::ostringstream out;out<<label<<" clip differs "<<edge.from.x<<','<<edge.from.y<<"=>"<<edge.to.x<<','<<edge.to.y<<" flags "<<unsigned(edge.clipFlags)<<'/'<<flags
            <<" header "<<edge.clippedFrom.x<<','<<edge.clippedFrom.y<<'/'<<f::word(ram.data(),pointer+4)<<','<<f::word(ram.data(),pointer)<<" ; "<<edge.clippedTo.x<<','<<edge.clippedTo.y<<'/'<<f::word(ram.data(),pointer+6)<<','<<f::word(ram.data(),pointer+2);throw std::runtime_error(out.str());}
    if(compareBytes&&edge.storageBytes!=f::longword(ram.data(),0x68144)-f::lineBase){std::ostringstream out;out<<label<<" storage differs "<<edge.storageBytes<<'/'<<f::longword(ram.data(),0x68144)-f::lineBase<<" for "<<edge.from.x<<','<<edge.from.y<<" =>"<<edge.to.x<<','<<edge.to.y;throw std::runtime_error(out.str());}
}
void checkLine(scr::NearTrackPoint from,scr::NearTrackPoint to){
    ram=baseline;f::putWord(ram.data(),0x1bfb0,std::uint16_t(from.x));f::putWord(ram.data(),0x1c0f0,std::uint16_t(from.y));
    f::putWord(ram.data(),0x1bfb2,std::uint16_t(to.x));f::putWord(ram.data(),0x1c0f2,std::uint16_t(to.y));
    f::writeQueueInputs(ram,{});begin(0x668a8);m68k_set_reg(M68K_REG_D1,0);m68k_set_reg(M68K_REG_D2,2);execute();
    compareEdge(scr::makeNearTrackEdge(from,to),f::longword(ram.data(),f::queueBase+32),"Line",true);++lineCases;
}
scr::NearTrackGeometryState synthetic(unsigned n){
    auto s=scr::test::nearGeometry::readState(baseline);const unsigned count=2+next()%14;s.progress.segment.limitDoubled=std::uint8_t(count*4);
    s.renderingLoopIndex=std::uint8_t((next()%count)*4);s.progress.currentSegment=std::uint8_t(next()%44);s.progress.segment.alternateFlag=std::uint8_t(next()%4);
    s.progress.segment.steeringFlags=std::uint8_t(next());s.progress.depth=std::uint8_t(next()%44);s.progress.offRoadSide=std::uint8_t(next()%3==0?0:next()&1?2:0x80);
    s.progress.lateralOffset=std::uint16_t(next());s.processedIndices={{0,2}};
    for(unsigned i=0;i<60;++i){const int x=n%3?int(next()%256):int(next()%800)-250,y=n%3?int(next()%128):int(next()%400)-150;
        s.upper[i]={std::uint16_t(next()%4?1024:0x8000),x,y};s.lower[i]={std::uint16_t(next()%3?512:0x8000),x,y+20};}
    for(auto& flags:s.flags)flags=std::uint8_t(next()&0xe1);return s;
}
void compareQueue(const scr::NearTrackGeometryState& s,const scr::NearTrackQueueState& q){
    if(q.offset!=f::word(ram.data(),0x68148)||q.opponentOffset!=f::word(ram.data(),0x1bc12)||q.previousOffset!=ram[0x1bb7c]||
       s.progress.depth!=ram[0x1bb58]||s.processedIndices[0]!=ram[0x1bc14]){
        std::ostringstream out;out<<"Near queue scalar differs offset="<<q.offset<<'/'<<f::word(ram.data(),0x68148)
            <<" opponent="<<q.opponentOffset<<'/'<<f::word(ram.data(),0x1bc12)<<" previous="<<unsigned(q.previousOffset)<<'/'<<unsigned(ram[0x1bb7c])
            <<" depth="<<unsigned(s.progress.depth)<<'/'<<unsigned(ram[0x1bb58])<<" processed="<<unsigned(s.processedIndices[0])<<'/'<<unsigned(ram[0x1bc14]);
        throw std::runtime_error(out.str());}
    if(q.lineBytes!=f::longword(ram.data(),0x68144)-f::lineBase)throw std::runtime_error("Near queue allocation differs");
    for(unsigned n=0;n<30;++n)if(s.flags[n]!=ram[0x1bdd0+n*4])throw std::runtime_error("Near queue flags differ");
    for(unsigned n=0;n<60;++n)if(std::uint16_t(s.upper[n].y)!=f::word(ram.data(),0x1c0f0+2*n))throw std::runtime_error("Near queue bounds adjustment differs");
    for(unsigned n=0;n<q.offset/32;++n){const auto& entry=q.entries[n];const unsigned base=f::queueBase+n*32;
        if(entry.segment!=ram[base+28]||entry.edgeColor!=ram[base+29]||entry.processedFlags!=ram[base+30]||entry.steeringFlags!=ram[base+31])throw std::runtime_error("Near queue header differs");
        for(unsigned e=0;e<7;++e)compareEdge(entry.edges[e],f::longword(ram.data(),base+4*e),"Queue "+std::to_string(n)+"/"+std::to_string(e));
    }
}
using EdgeKey=std::array<int,5>;
EdgeKey edgeKey(unsigned pointer){
    for(unsigned n=0;n<48;++n)for(unsigned e=0;e<7;++e)if((f::longword(ram.data(),f::queueBase+n*32+e*4)&0xffffff)==pointer)return {int(n*32),int(e),0,0,0};
    return {-1,int(f::word(ram.data(),pointer+4)),int(f::word(ram.data(),pointer)),int(f::word(ram.data(),pointer+6)),int(f::word(ram.data(),pointer+2))};
}
EdgeKey edgeKey(const scr::NearTrackEdge& edge){if(edge.originQueueOffset!=0xffff)return {edge.originQueueOffset,edge.originEdge,0,0,0};return {-1,edge.clippedFrom.x,edge.clippedFrom.y,edge.clippedTo.x,edge.clippedTo.y};}
void checkDraw(const scr::NearTrackQueueState& q,unsigned n,const scr::NearTrackDrawInputs* supplied=nullptr){
    scr::NearTrackDrawInputs input{};input.renderingOrderMode=std::uint8_t(n%3?0xe0:0);input.offsetFromRoadCenter=std::uint16_t(int(next()%1200)-600);
    input.raceStartTimer=std::uint8_t(n&1);input.offRoadSideFlags=std::uint8_t(n%3==0?0:n%3==1?2:0x80);input.sideEdgeColor=9;input.alternateSideColor=10;
    if(supplied)input=*supplied;
    ram[0x1bb9a]=input.renderingOrderMode;f::putWord(ram.data(),0x1bc5c,input.offsetFromRoadCenter);ram[0x1bbdf]=input.raceStartTimer;ram[0x1bbd1]=input.offRoadSideFlags;
    ram[0x691b3]=input.sideEdgeColor;ram[0x691b2]=input.alternateSideColor;ram[0x1bb66]=input.previewColorOverride?0x80:0;f::putWord(ram.data(),0x1bc1e,input.minimumQueueOffset);commands.clear();
    auto native=scr::drawNearTrackQueue(q,input);drawMode=true;begin(0x68f28);execute();drawMode=false;
    if(commands.size()!=native.size()){
        std::cerr<<"DRAW CASE "<<n<<" end="<<q.offset<<" order="<<unsigned(input.renderingOrderMode)<<" lateral="<<sw(input.offsetFromRoadCenter)<<"\n";
        for(unsigned i=0;i<commands.size();++i){const auto& c=commands[i];std::cerr<<"O"<<i<<" kind="<<c.line<<" s="<<c.surface<<" color="<<c.color<<" edges=";for(auto p:c.pointers){const auto k=edgeKey(p);std::cerr<<k[0]<<'/'<<k[1]<<',';}std::cerr<<'\n';}
        for(unsigned i=0;i<native.size();++i){const auto& c=native[i];std::cerr<<"N"<<i<<" kind="<<unsigned(c.kind==scr::NearTrackCommandKind::line)<<" s="<<unsigned(c.surface)<<" color="<<unsigned(c.color)<<" edges=";for(const auto& e:c.edges){const auto k=edgeKey(e);std::cerr<<k[0]<<'/'<<k[1]<<',';}std::cerr<<'\n';}
        throw std::runtime_error("Near painter command count differs original="+std::to_string(commands.size())+" native="+std::to_string(native.size()));}
    for(unsigned i=0;i<native.size();++i){const auto& a=commands[i];const auto& b=native[i];if(a.line!=(b.kind==scr::NearTrackCommandKind::line)||a.color!=b.color||(!a.line&&a.surface!=(b.surface==scr::NearTrackSurface::rightSide?0x80:b.surface==scr::NearTrackSurface::leftSide?2:0)))throw std::runtime_error("Near painter command material/order differs");
        std::vector<EdgeKey> expected,actual;for(auto p:a.pointers)expected.push_back(edgeKey(p));for(const auto& e:b.edges)actual.push_back(edgeKey(e));std::sort(expected.begin(),expected.end());std::sort(actual.begin(),actual.end());
        if(expected!=actual)throw std::runtime_error("Near painter boundary differs command "+std::to_string(i));++drawCommands;
    }
    ++drawCases;
}
void checkQueue(unsigned n,bool first){
    auto state=synthetic(n),native=state;scr::NearTrackQueueState queue{};queue.offset=std::uint16_t(first?32:64);queue.opponentSegment=std::uint8_t(next()%44);queue.opponentSubSegment=std::uint8_t(next()%10);queue.minimumBoundaryDistance=std::uint16_t(next());
    ram=baseline;scr::test::nearGeometry::writeState(ram,state);f::writeQueueInputs(ram,queue);
    scr::appendNearTrackSegment(native,queue,first);begin(first?0x6702e:0x67092);m68k_set_reg(M68K_REG_A6,0x1be70);m68k_set_reg(M68K_REG_A1,f::queueBase);execute();compareQueue(native,queue);++queueCases;
    if(queue.offset>=96)checkDraw(queue,n);
}
void checkVisibility(unsigned n){
    auto state=synthetic(n),native=state;scr::NearTrackQueueState queue{};queue.offset=std::uint16_t(n&1?32:64);const auto flags=std::uint8_t(next());
    ram=baseline;scr::test::nearGeometry::writeState(ram,state);f::writeQueueInputs(ram,queue);
    f::putLong(ram.data(),0x7aa1a+4*state.progress.currentSegment,0x77000);f::putWord(ram.data(),0x77000,std::uint16_t(flags)<<8);
    std::vector<unsigned> expected;scr::processNearTrackVisibility(native,flags,queue,[&](std::uint8_t index){expected.push_back(index);const auto& p=native.upper[index/2];native.lower[index/2]={512,sw(unsigned(p.x+1)),sw(unsigned(p.y+20))};});
    requests.clear();visibilityMode=true;begin(0x655ea);execute();visibilityMode=false;
    if(requests!=expected)throw std::runtime_error("Near visibility lower materialization order differs");
    for(unsigned i=0;i<30;++i)if(native.flags[i]!=ram[0x1bdd0+4*i])throw std::runtime_error("Near visibility flags differ");
    for(unsigned i=0;i<60;++i){const auto& p=native.lower[i];if(p.height!=f::word(ram.data(),0x1be70+120+2*i)||std::uint16_t(p.x)!=f::word(ram.data(),0x1bfb0+120+2*i)||std::uint16_t(p.y)!=f::word(ram.data(),0x1c0f0+120+2*i))throw std::runtime_error("Near visibility lower output differs");}
    if(queue.offset!=f::word(ram.data(),0x68148))throw std::runtime_error("Near visibility queue offset differs");++visibilityCases;
}
void originalStage(unsigned entry){begin(entry);m68k_set_reg(M68K_REG_A6,0x1be70);m68k_set_reg(M68K_REG_A1,f::queueBase);execute();}
void replayFrames(const char* inputPath){
    namespace replay=scr::test::replay;const auto inputs=replay::loadInputs(inputPath);
    auto playback=std::make_unique<replay::NativeReplay>(baseline);auto geometry=scr::test::nearGeometry::readState(baseline);
    std::array<std::uint8_t,100> renderFlags{};
    for(unsigned n=0;n<playback->progressData.track.segmentCount;++n)renderFlags[n]=baseline[scr_test::readLong(baseline,0x7aa1a+4*n)];
    frameMode=true;
    for(frameTick=0;frameTick<inputs.size();++frameTick){
        const auto& control=inputs[frameTick];playback->begin(control.controls);playback->ram[0xead6+0x45]=control.quit?0xb3:0;
        playback->controlsStage();playback->physicsStage();playback->engineStage();playback->progressStage();
        scr::NearTrackFrameInputs in{};in.progress=scr::test::trackProgress::readState(playback->ram);
        scr::RenderCameraInputs camera{};for(unsigned n=0;n<3;++n)camera.worldPosition[n]=scr_test::readLong(playback->ram,0x1bcd8+4*n);
        camera.pitch=scr_test::readWord(playback->ram,0x1bce4);camera.yaw=scr_test::readWord(playback->ram,0x1bce6);camera.roll=scr_test::readWord(playback->ram,0x1bce8);camera.averageWheelVelocity=scr_test::readWord(playback->ram,0x1bd38);
        in.camera=scr::makeRenderProjectionCamera(camera,playback->physicsData.trig);in.camera.viewportCenterY=scr_test::readWord(playback->ram,0x1bc42);
        in.camera.sinRoll=scr_test::readWord(playback->ram,0x1c250);in.camera.cosRoll=scr_test::readWord(playback->ram,0x1c252);
        in.displayUpdate=playback->ram[0x5d724];in.opponentSegment=playback->ram[0x1bba6];in.opponentSubSegment=playback->ram[0x1bc24];in.minimumBoundaryDistance=scr_test::readWord(playback->ram,0x63ede);
        in.drawing.minimumQueueOffset=scr_test::readWord(playback->ram,0x1bc1e);in.drawing.renderingOrderMode=playback->ram[0x1bb9a];in.drawing.raceStartTimer=playback->ram[0x1bbdf];
        in.drawing.offRoadSideFlags=in.progress.offRoadSide;in.drawing.offsetFromRoadCenter=in.progress.lateralOffset;in.drawing.sideEdgeColor=baseline[0x691b3];in.drawing.alternateSideColor=baseline[0x691b2];
        auto initial=geometry;const auto native=scr::buildNearTrackFrame(playback->progressData,renderFlags,in,geometry);
        initial.progress=in.progress;initial.camera=in.camera;initial.displayUpdate=in.displayUpdate;initial.startIndex=0;initial.renderingLoopIndex=in.progress.renderIndex;
        initial.continuation=initial.viewReverse=0;initial.processedIndices={0x80,0x80};initial.progress.currentSegment=in.progress.playerSegment;
        if(initial.progress.progressOffset&0x8000){initial.progress.currentSegment=std::uint8_t((initial.progress.currentSegment+1)%playback->progressData.track.segmentCount);initial.progress.progressOffset&=255;}
        ram=playback->ram;scr::test::nearGeometry::writeState(ram,initial);scr::NearTrackQueueState empty{};empty.opponentSegment=in.opponentSegment;empty.opponentSubSegment=in.opponentSubSegment;empty.minimumBoundaryDistance=in.minimumBoundaryDistance;f::writeQueueInputs(ram,empty);
        // The caller can seed marker zero for an opponent on the preceding
        // segment before any dense pass. Execute that branch independently.
        begin(0x64fba,0x64fe2);execute();
        for(unsigned pass=0;pass<3;++pass){
            originalStage(0x6521c);originalStage(0x655ea);if(!pass)originalStage(0x65a18);originalStage(pass?0x67092:0x6702e);
            if(!pass){ram[0x1bb60]=0;ram[0x1bbc5]=4;}
            originalStage(0x65564);ram[0x1bb85]=std::uint8_t((ram[0x1bb85]+1)%playback->progressData.track.segmentCount);if(pass==1)ram[0x1bb7f]=1;++framePasses;
        }
        compareQueue(geometry,native.queue);
        for(unsigned n=0;n<60;++n)for(unsigned lower=0;lower<2;++lower){const auto& p=lower?geometry.lower[n]:geometry.upper[n];const unsigned index=2*n+lower*120;
            if(p.height!=f::word(ram.data(),0x1be70+index)||std::uint16_t(p.x)!=f::word(ram.data(),0x1bfb0+index)||std::uint16_t(p.y)!=f::word(ram.data(),0x1c0f0+index))throw std::runtime_error("Full near frame geometry differs");}
        checkDraw(native.queue,frameTick,&in.drawing);++frameCases;
        playback->groundStage();playback->wheelsStage();playback->damageStage();playback->timingStage();playback->outerStage();
    }
    frameMode=false;
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
    if(argc!=3&&argc!=4)throw std::runtime_error("Usage: near_track_visibility_oracle payload.bin chipram.bin [controls.csv]");const auto payload=readFile(argv[1]),capture=readFile(argv[2]);
    if(payload.size()!=408540||capture.size()!=ram.size())throw std::runtime_error("Input size differs");std::copy(capture.begin(),capture.end(),baseline.begin());
    for(auto range:std::array<std::pair<unsigned,unsigned>,9>{{{0x655ea,0x65756},{0x617d0,0x6181e},{0x668a8,0x66fb8},{0x66ff0,0x6770a},{0x67740,0x67888},{0x6892a,0x690bc},{0x64fba,0x64fe2},{0x5c51a,0x5c554},{0x6847c,0x684a6}}})
        if(!std::equal(payload.begin()+range.first-0xe700,payload.begin()+range.second-0xe700,baseline.begin()+range.first))throw std::runtime_error("Captured near code differs");
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    constexpr std::array<int,9> x{-600,-1,0,1,128,255,256,257,800},y{-300,-1,0,1,64,127,128,129,400};
    for(auto ax:x)for(auto ay:y)for(unsigned n=0;n<9;++n)checkLine({ax,ay},{x[n],y[(n+3)%9]});
    for(unsigned n=0;n<3000;++n)checkLine({int(next()%1400)-600,int(next()%700)-300},{int(next()%1400)-600,int(next()%700)-300});
    for(unsigned n=0;n<2000;++n)checkVisibility(n);
    for(unsigned n=0;n<1000;++n)checkQueue(n,n&1);
    if(argc==4)replayFrames(argv[3]);
    std::cout<<"{\"total_cases\":"<<cases<<",\"line_clipping_cases\":"<<lineCases<<",\"visibility_cases\":"<<visibilityCases<<",\"queue_cases\":"<<queueCases<<",\"painter_cases\":"<<drawCases<<",\"painter_commands\":"<<drawCommands<<",\"full_near_frames\":"<<frameCases<<",\"full_near_segment_passes\":"<<framePasses<<",\"original_instructions\":"<<totalInstructions<<",\"mismatches\":0,\"original_code_unmodified\":true,\"leaf_visibility_projection_callback_is_test_seam\":true,\"full_frame_projection_code_executed\":"<<(frameCases?"true":"false")<<",\"native_pixel_identity_claimed\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<"case="<<cases<<" line="<<lineCases<<" visibility="<<visibilityCases<<" queue="<<queueCases<<" frame="<<frameTick<<' '<<e.what()<<'\n';return 1;}
