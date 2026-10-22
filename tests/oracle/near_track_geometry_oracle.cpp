#include "near_track_geometry_fixture.h"
#include "driving_replay_io.h"
#include "m68k.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
namespace f=scr::test::nearGeometry;
using Ram=scr_test::ChipRam;
Ram ram{},baseline{};std::array<std::uint8_t,4096> stackBytes{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,stopPc=0xa0000;
unsigned pcNow=0,instructions=0,finalSr=0;bool invalid=false,done=false;std::uint64_t cases=0,totalInstructions=0,projectCases=0,shiftCases=0,lowerCases=0,cutCases=0;
std::array<scr::NearTrackCutEdge,4> originalCut{};
std::uint32_t randomState=0x4e454152;
unsigned next(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
unsigned read(unsigned a){if(a<ram.size())return ram[a];if(a>=stackBase&&a<stackTop)return stackBytes[a-stackBase];if(a>=stopPc&&a<stopPc+4){constexpr unsigned char stop[]{0x4e,0x72,0x27,0};return stop[a-stopPc];}invalid=true;return 0;}
void write(unsigned a,unsigned v){
    const bool code=(a>=0x6521c&&a<0x65bec&&!(a>=0x654c0&&a<0x654c2))||
        (a>=0x5a520&&a<0x5a64e&&!(a>=0x5a54e&&a<0x5a550))||(a>=0x62454&&a<0x62574);
    if(code){invalid=true;return;}
    if(a<ram.size()){ram[a]=std::uint8_t(v);return;}if(a>=stackBase&&a<stackTop){stackBytes[a-stackBase]=std::uint8_t(v);return;}invalid=true;
}
unsigned reg(m68k_register_t r){return m68k_get_reg(nullptr,r);}
void hook(unsigned pc){
    pcNow=pc;++instructions;++totalInstructions;
    if(pc==stopPc){finalSr=reg(M68K_REG_SR);done=true;m68k_end_timeslice();return;}
    if(pc==0x668a8){
        const auto offset=scr_test::readWord(ram,0x68148);if(offset>=32&&offset<48){auto& edge=originalCut[(offset-32)/4];edge={true,std::uint8_t(reg(M68K_REG_D1)),std::uint8_t(reg(M68K_REG_D2))};}
        const unsigned sp=reg(M68K_REG_SP);const auto target=(read(sp)<<24)|(read(sp+1)<<16)|(read(sp+2)<<8)|read(sp+3);
        m68k_set_reg(M68K_REG_SP,sp+4);m68k_set_reg(M68K_REG_PC,target);m68k_set_reg(M68K_REG_D1,0);m68k_set_reg(M68K_REG_D2,0);return;
    }
    if(invalid||instructions>300000){invalid=true;m68k_end_timeslice();return;}
    if(pc<0xe700||pc>=0x7aa1a){invalid=true;m68k_end_timeslice();}
}
void execute(unsigned entry,unsigned d1=0,unsigned d2=0,unsigned d0=0){
    invalid=done=false;instructions=0;originalCut={};m68k_pulse_reset();for(unsigned n=0;n<15;++n)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+n),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_D1,d1);m68k_set_reg(M68K_REG_D2,d2);m68k_set_reg(M68K_REG_D0,d0);m68k_set_reg(M68K_REG_A6,0x1be70);
    stackBytes[4092]=std::uint8_t((stopPc>>24)&255);stackBytes[4093]=std::uint8_t((stopPc>>16)&255);stackBytes[4094]=std::uint8_t((stopPc>>8)&255);stackBytes[4095]=std::uint8_t(stopPc&255);
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(100000000);
    if(!done||invalid){std::ostringstream text;text<<"Original near geometry failed PC0x"<<std::hex<<pcNow;throw std::runtime_error(text.str());}
}
void compare(const scr::NearTrackGeometryState& expected,const char* stage){
    auto actual=f::readState(ram);
    auto mismatch=[&](const char* field,unsigned n,unsigned a,unsigned e){std::ostringstream text;text<<stage<<" mismatch "<<field<<"["<<n<<"] actual="<<a<<" expected="<<e<<" case="<<cases;throw std::runtime_error(text.str());};
    for(unsigned n=0;n<60;++n)for(unsigned lower=0;lower<2;++lower){const auto& a=lower?actual.lower[n]:actual.upper[n];const auto& e=lower?expected.lower[n]:expected.upper[n];
        if(a.height!=e.height)mismatch(lower?"lowerHeight":"upperHeight",n,a.height,e.height);
        if(a.x!=e.x)mismatch(lower?"lowerX":"upperX",n,unsigned(a.x),unsigned(e.x));if(a.y!=e.y)mismatch(lower?"lowerY":"upperY",n,unsigned(a.y),unsigned(e.y));}
    for(unsigned n=0;n<30;++n)if(actual.flags[n]!=expected.flags[n])mismatch("flags",n,actual.flags[n],expected.flags[n]);
    for(unsigned n=0;n<2;++n){if(actual.processedIndices[n]!=expected.processedIndices[n])mismatch("processed",n,actual.processedIndices[n],expected.processedIndices[n]);
        if(actual.savedHeights[n]!=expected.savedHeights[n])mismatch("savedHeight",n,actual.savedHeights[n],expected.savedHeights[n]);
        if(actual.transformedCoordinates[n]!=expected.transformedCoordinates[n])mismatch("transformedCoordinates",n,actual.transformedCoordinates[n],expected.transformedCoordinates[n]);}
    if(actual.progress.visibility!=expected.progress.visibility)mismatch("visibility",0,actual.progress.visibility,expected.progress.visibility);
    if(actual.startIndex!=expected.startIndex)mismatch("startIndex",0,actual.startIndex,expected.startIndex);
    if(actual.processingIndex!=expected.processingIndex)mismatch("processingIndex",0,actual.processingIndex,expected.processingIndex);
    if(actual.trackOffsetBase!=expected.trackOffsetBase)mismatch("trackOffsetBase",0,actual.trackOffsetBase,expected.trackOffsetBase);
    if(actual.lastProcessed!=expected.lastProcessed)mismatch("lastProcessed",0,actual.lastProcessed,expected.lastProcessed);
    if(actual.perspectiveDepth!=expected.perspectiveDepth)mismatch("perspectiveDepth",0,actual.perspectiveDepth,expected.perspectiveDepth);
    ++cases;
}
scr::NearTrackGeometryState project(const scr::TrackProgressData& data,scr::NearTrackGeometryState native){
    f::writeState(ram,native);execute(0x6521c);scr::projectNearTrackSegment(data,native);compare(native,"project");++projectCases;return native;
}
void carry(scr::NearTrackGeometryState& native){f::writeState(ram,native);execute(0x65564);const bool expected=(finalSr&1)!=0;const bool actual=scr::shiftNearTrackCoordinates(native);if(actual!=expected)throw std::runtime_error("Shift comparison flag differs");compare(native,"shift");++shiftCases;}
void cut(const scr::TrackProgressData& data,scr::NearTrackGeometryState& native){
    f::writeState(ram,native);execute(0x65a18);const auto edges=scr::prepareNearTrackCutEdges(data,native);compare(native,"cut");
    for(unsigned n=0;n<4;++n)if(edges[n].valid!=originalCut[n].valid||(edges[n].valid&&(edges[n].fromIndex!=originalCut[n].fromIndex||edges[n].toIndex!=originalCut[n].toIndex)))throw std::runtime_error("Cut edge command differs");++cutCases;
}
void lower(const scr::TrackProgressData& data,scr::NearTrackGeometryState& native,unsigned index){
    f::writeState(ram,native);execute(0x65720,index);scr::materializeNearTrackLower(data,native,std::uint8_t(index));compare(native,"lower");++lowerCases;
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
    if(argc!=4)throw std::runtime_error("Usage: near_track_geometry_oracle payload chipram controls.csv");
    const auto capture=scr::test::replay::loadCapture(argv[2]);baseline=*capture;ram=baseline;const auto data=scr::test::trackProgress::readData(ram);
    std::ifstream file(argv[1],std::ios::binary);const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    for(const auto span:std::array<std::array<unsigned,2>,5>{{{0x6521c,0x654c0},{0x654c2,0x655ea},{0x65756,0x65bec},{0x5a520,0x5a54e},{0x5a550,0x5a64e}}})if(!std::equal(payload.begin()+span[0]-0xe700,payload.begin()+span[1]-0xe700,baseline.begin()+span[0]))throw std::runtime_error("Captured original code differs");
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    auto playback=std::make_unique<scr::test::replay::NativeReplay>(baseline);const auto inputs=scr::test::replay::loadInputs(argv[3]);
    auto state=f::readState(baseline);
    for(unsigned tick=0;tick<inputs.size();++tick){
        playback->begin(inputs[tick].controls);playback->controlsStage();playback->physicsStage();playback->engineStage();playback->progressStage();
        ram=playback->ram;const auto input=f::readState(ram);state.progress=input.progress;state.camera=input.camera;state.displayUpdate=input.displayUpdate;
        state.startIndex=0;state.renderingLoopIndex=state.progress.renderIndex;state.continuation=0;state.viewReverse=0;state.processedIndices={0x80,0x80};
        state.camera.viewportCenterY=std::uint16_t(-int(scr_test::readWord(ram,0x1bce4)));
        state.progress.currentSegment=state.progress.playerSegment;if(state.progress.progressOffset&0x8000){state.progress.currentSegment=std::uint8_t((state.progress.currentSegment+1)%data.track.segmentCount);state.progress.progressOffset&=0xff;}
        state=project(data,state);cut(data,state);state.renderingLoopIndex=0;state.startIndex=4;carry(state);
        for(unsigned pass=0;pass<2;++pass){state.progress.currentSegment=std::uint8_t((state.progress.currentSegment+1)%data.track.segmentCount);state.continuation=std::uint8_t(pass);state=project(data,state);carry(state);}
        playback->groundStage();playback->wheelsStage();playback->damageStage();playback->timingStage();playback->outerStage();
    }
    for(unsigned n=0;n<500;++n){ram=baseline;auto synthetic=f::readState(ram);synthetic.progress.currentSegment=std::uint8_t(next()%data.track.segmentCount);synthetic.progress.visibility=std::uint16_t(next()%64);synthetic.startIndex=0;synthetic.renderingLoopIndex=0;synthetic.continuation=std::uint8_t(next()&1);synthetic.processedIndices={0x80,0x80};synthetic.flags.fill(0);synthetic.edgeVisibilityMode=std::uint8_t(next());synthetic.progress.offRoadSide=std::uint8_t(next()%3?0:2);synthetic.preview=std::uint8_t(next()&0x80);
        synthetic.progress.offTrackFlags=std::uint8_t(next()&0xc0);synthetic.progress.cameraYaw=std::uint16_t(next());synthetic.progress.cameraHeight=std::uint16_t(next()%8192);synthetic.camera.viewportCenterY=std::uint16_t(next());
        const auto roll=std::uint16_t(next());synthetic.camera.sinRoll=scr::gameSine(roll,playback->physicsData.trig);synthetic.camera.cosRoll=scr::gameCosine(roll,playback->physicsData.trig);
        synthetic=project(data,synthetic);for(unsigned i=0;i<synthetic.progress.segment.maximumDoubled;i+=2)lower(data,synthetic,i);synthetic.viewReverse=std::uint8_t(next()&0x80);carry(synthetic);}
    for(unsigned n=0;n<64;++n){
        auto wide=data;wide.track.primaryGeometry[0]=0x80;wide.track.alternateGeometry[0]=1;
        wide.track.geometry[32]=0;wide.track.geometry[33]=0xa1;wide.track.geometry[34]=0;wide.track.geometry[35]=0xa2;
        const auto config=scr::loadTrackSegmentConfiguration(wide.track,0);
        for(unsigned row=0;row<unsigned(config.limit)/2;++row)for(unsigned side=0;side<2;++side){
            const auto raw=std::uint16_t(0x100+next()%0x1800);const auto address=0xf000+side*256+row*2;
            wide.track.geometry[address]=std::uint8_t((raw>>8)|((row==0||row+1==unsigned(config.limit)/2)?0x80:0));wide.track.geometry[address+1]=std::uint8_t(raw&255);
        }
        ram=baseline;ram[0x1c4c0]=0x80;ram[0x1c524]=1;std::copy(wide.track.geometry.begin(),wide.track.geometry.end(),ram.begin()+0x1ef82);
        auto synthetic=f::readState(ram);synthetic.progress.currentSegment=0;synthetic.progress.visibility=std::uint16_t(next()%64);synthetic.startIndex=0;synthetic.renderingLoopIndex=0;synthetic.processedIndices={0x80,0x80};synthetic.flags.fill(0);synthetic=project(wide,synthetic);
        for(unsigned index=0;index<config.maximumDoubled;index+=2)lower(wide,synthetic,index);
        carry(synthetic);synthetic.startIndex=4;synthetic.continuation=1;synthetic=project(wide,synthetic);
    }
    std::cout<<"{\"total_cases\":"<<cases<<",\"projection_cases\":"<<projectCases<<",\"shift_cases\":"<<shiftCases<<",\"cut_cases\":"<<cutCases<<",\"lower_cases\":"<<lowerCases<<",\"mismatches\":0,\"original_instructions\":"<<totalInstructions<<",\"replay_ticks\":"<<inputs.size()<<",\"original_code_unmodified\":true,\"cut_line_pixel_backend_intercepted\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"case="<<cases<<" "<<error.what()<<'\n';return 1;}
