// Full original main-loop execution, including its actual renderer. Only
// asynchronous device waits are serviced by the harness; no drawing call is
// replaced. RaceScene evolves independently from the same native race seed.
#include "race/race_scene.h"
#include "state/track_mesh_adapter.h"
#include "opponent_car_fixture.h"
#include "near_track_geometry_fixture.h"
#include "driving_replay_io.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
namespace f=scr::test::opponentCar;namespace replay=scr::test::replay;using Ram=scr_test::ChipRam;
Ram ram{};std::array<std::uint8_t,4096> stack{};constexpr unsigned stackBase=0x8f000,stackTop=0x90000,stopPc=0xa0000,entry=0x5d47e;
std::vector<std::uint8_t> savedCpu;unsigned pc{},badAddress{},instructions{},joy{},cia=255,ddra=255,currentTick{},currentTrack{};bool invalid{},done{},firstEntry{},outerRunning{},restarting{},inCar{},frameCompared{},leaf{};unsigned carReturn{},farLineBytes{},mountainLineBytes{},horizonLineBytes{};
std::uint64_t ticks{},originalInstructions{},commandFrames{},carCalls{},carCommands{},inputComparisons{},allocationComparisons{},mountainAllocations{},horizonAllocations{};std::string failure;
using Key=std::array<int,4>;struct Command{unsigned color{};std::array<Key,4> edges{};};std::vector<Command> originalCommands;
const scr::runtime::RaceFrame* frame{};std::uint16_t expectedLineBytes{},expectedFarBytes{},expectedBackgroundBytes{},expectedHorizonBytes{};unsigned horizonMode{};
const scr::RenderProjectionTables* currentTables{};unsigned directCarCommands{},carD1{},carD2{},carLineMode{};
std::unique_ptr<scr::OpponentCarFrame> directCar;
std::unique_ptr<scr::NearTrackFrame> expectedNear;
std::string historyDifference;
unsigned read(unsigned address);
Key key(unsigned pointer){pointer&=0xffffff;return {int(f::word(ram.data(),pointer+4)),int(f::word(ram.data(),pointer)),int(f::word(ram.data(),pointer+6)),int(f::word(ram.data(),pointer+2))};}
Key key(const scr::NearTrackEdge& e){return {e.clippedFrom.x,e.clippedFrom.y,e.clippedTo.x,e.clippedTo.y};}
void compareInputs(){const auto actual=f::readInputs(ram.data());const auto& expected=frame->opponent;
    directCar=std::make_unique<scr::OpponentCarFrame>(scr::buildOpponentCarFrame(actual,*currentTables));directCarCommands=unsigned(directCar->commands.size());carD1=m68k_get_reg(nullptr,M68K_REG_D1);carD2=m68k_get_reg(nullptr,M68K_REG_D2);carLineMode=ram[0x1bb77];
    const auto check=[&](unsigned a,unsigned b,const char* name){++inputComparisons;if(a!=b){std::ostringstream s;s<<"Scene opponent input "<<name<<" differs native="<<a<<" original="<<b;throw std::runtime_error(s.str());}};
    for(unsigned n=0;n<4;++n){check(expected.footprint[n],actual.footprint[n],"footprint");check(expected.relativeX[n],actual.relativeX[n],"relativeX");check(expected.relativeZ[n],actual.relativeZ[n],"relativeZ");}for(unsigned n=0;n<3;++n)check(expected.bounds[n],actual.bounds[n],"bounds");
    check(expected.distance,actual.distance,"distance");check(expected.roadOffset,actual.roadOffset,"roadOffset");check(expected.aheadFlag,actual.aheadFlag,"aheadFlag");check(expected.curveSmoothing,actual.curveSmoothing,"curveSmoothing");check(expected.scaledPerspective,actual.scaledPerspective,"scaledPerspective");check(expected.camera.heightBaseline,actual.camera.heightBaseline,"cameraHeight");check(expected.camera.yawResidual,actual.camera.yawResidual,"cameraYaw");check(expected.camera.viewportCenterY,actual.camera.viewportCenterY,"cameraPitch");check(expected.camera.sinRoll,actual.camera.sinRoll,"sinRoll");check(expected.camera.cosRoll,actual.camera.cosRoll,"cosRoll");
    ++allocationComparisons;if(expectedLineBytes!=actual.lineBytes){std::ostringstream s;s<<"Scene car line allocation differs native="<<expectedLineBytes<<" original="<<actual.lineBytes<<" far="<<farLineBytes<<" nativeFar="<<expectedFarBytes<<" mountain="<<mountainLineBytes<<" horizon="<<horizonLineBytes<<" nativeHorizon="<<expectedHorizonBytes<<" nativeBackground="<<expectedBackgroundBytes<<" horizonMode="<<horizonMode<<" nativeMode="<<unsigned(frame->nearSeed.edgeVisibilityMode)<<" yaw="<<frame->cameraYaw<<'/'<<f::word(ram.data(),0x1bce6);throw std::runtime_error(s.str());}
}
void compareCommands(){std::vector<Command> actual;for(const auto& c:frame->nearTrack.commands)if(c.surface==scr::NearTrackSurface::opponent){Command a{};a.color=c.color;if(c.edges.size()!=4)throw std::runtime_error("Scene car command isn't a quad");for(unsigned n=0;n<4;++n)a.edges[n]=key(c.edges[n]);std::sort(a.edges.begin(),a.edges.end());actual.push_back(a);}
    if(actual.size()!=originalCommands.size())throw std::runtime_error("Scene car command count differs native="+std::to_string(actual.size())+" original="+std::to_string(originalCommands.size())+" directModel="+std::to_string(directCarCommands)+" D1="+std::to_string(carD1)+" D2="+std::to_string(carD2)+" lineMode="+std::to_string(carLineMode));
    for(unsigned n=0;n<actual.size();++n)if(actual[n].color!=originalCommands[n].color||actual[n].edges!=originalCommands[n].edges)throw std::runtime_error("Scene car command material/boundary differs n="+std::to_string(n));
    ++commandFrames;carCommands+=actual.size();frameCompared=true;
}
void hook(unsigned address){pc=address;if(invalid){m68k_end_timeslice();return;}try{
    if(leaf&&address==stopPc){done=true;m68k_end_timeslice();return;}
    if(address==0x5dac0)ram[0x616d8]=0;
    if(outerRunning&&restarting&&(address==0x5ba42||address==0x64470))ram[0x616d8]=ram[0x616da]=0;
    if(address==entry){if(firstEntry)firstEntry=false;else{if(m68k_get_reg(nullptr,M68K_REG_SP)!=stackTop)throw std::runtime_error("Original scene loop stack imbalance");m68k_get_context(savedCpu.data());done=true;m68k_set_reg(M68K_REG_PC,stopPc);m68k_end_timeslice();return;}}
    if(inCar&&address==carReturn){inCar=false;if(directCar&&directCar->admitted){for(unsigned n=0;n<16;++n){const auto& p=directCar->vertices[n];if(std::uint16_t(p.x)!=f::word(ram.data(),0x1c0b0+2*n)||std::uint16_t(p.y)!=f::word(ram.data(),0x1c1f0+2*n)){std::ostringstream s;s<<"Scene original car model point "<<n<<" differs native="<<p.x<<','<<p.y<<" original="<<f::word(ram.data(),0x1c0b0+2*n)<<','<<f::word(ram.data(),0x1c1f0+2*n);throw std::runtime_error(s.str());}}
        for(unsigned n=0;n<36;++n){const auto packed=f::longword(ram.data(),0x7b08a+0x5e0+4*n);const auto& e=directCar->edges[n];if(e.present!=bool(packed&0xffffff)||(e.present&&(e.clipFlags!=(packed>>24)||key(e)!=key(packed)))){std::ostringstream s;s<<"Scene original car edge "<<n<<" differs nativePresent="<<e.present<<" flags="<<unsigned(e.clipFlags)<<" originalPacked="<<packed;throw std::runtime_error(s.str());}}
    }}
    if(!outerRunning){
        if(address==0x64f80){std::ostringstream s;const auto& seed=frame->nearSeed;for(unsigned n=0;n<30;++n)if(seed.flags[n]!=ram[0x1bdd0+n*4]){s<<" flags"<<n<<'='<<unsigned(seed.flags[n])<<'/'<<unsigned(ram[0x1bdd0+n*4]);break;}for(unsigned n=0;n<2;++n)if(seed.transformedCoordinates[n]!=f::word(ram.data(),0x1c0a0+2*n))s<<" unrolled"<<n<<'='<<seed.transformedCoordinates[n]<<'/'<<f::word(ram.data(),0x1c0a0+2*n);s<<" renderIndex="<<unsigned(frame->nearInputs.progress.renderIndex)<<'/'<<unsigned(ram[0x1bb60])<<" progressOffset="<<frame->nearInputs.progress.progressOffset<<'/'<<f::word(ram.data(),0x1bb0e)<<" visibility="<<frame->nearInputs.progress.visibility<<'/'<<f::word(ram.data(),0x1bc26)<<" depth="<<unsigned(frame->nearInputs.progress.depth)<<'/'<<unsigned(ram[0x1bb58]);historyDifference=s.str();}
        if(address==0x65062&&expectedNear){const auto& q=expectedNear->queue;const auto actualBytes=f::longword(ram.data(),0x68144)-0x3d80;
            if(q.lineBytes!=actualBytes||q.offset!=f::word(ram.data(),0x68148)){std::ostringstream s;s<<"Scene dense arena differs native="<<q.lineBytes<<" original="<<actualBytes<<" queue="<<q.offset<<'/'<<f::word(ram.data(),0x68148)<<historyDifference;throw std::runtime_error(s.str());}}
        if(address==0x6508c)farLineBytes=f::longword(ram.data(),0x68144)-0x3d80;
        if(address==0x67888)horizonMode=ram[0x1bb68];
        if(address==0x650ba){horizonLineBytes=f::longword(ram.data(),0x68144)-0x3d80;if(horizonLineBytes!=farLineBytes)++horizonAllocations;}
        if(address==0x650c0){mountainLineBytes=f::longword(ram.data(),0x68144)-0x3d80;if(mountainLineBytes!=horizonLineBytes)++mountainAllocations;}
        if(address==0x67aa6){++carCalls;compareInputs();inCar=true;const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);carReturn=(read(sp)<<24)|(read(sp+1)<<16)|(read(sp+2)<<8)|read(sp+3);}
        if(inCar&&address==0x66618){Command c{};c.color=ram[0x66475]>>4;for(unsigned n=0;n<4;++n)c.edges[n]=key(m68k_get_reg(nullptr,static_cast<m68k_register_t>(M68K_REG_A0+n)));std::sort(c.edges.begin(),c.edges.end());originalCommands.push_back(c);}
        if(address==0x5d49c)compareCommands();
    }
    if(address==0x5d4ba)outerRunning=true;
    if(outerRunning){if(address==0x5d402)restarting=true;if(address==0x6488a||address==0x5d674){done=true;m68k_set_reg(M68K_REG_PC,stopPc);m68k_end_timeslice();return;}}
}catch(const std::exception& e){failure=e.what();invalid=true;m68k_end_timeslice();return;}
    if(address<0xe700||address>=0x724dc||++instructions>3000000){invalid=true;m68k_end_timeslice();}
}
unsigned read(unsigned address){if(address<ram.size())return ram[address];if(address>=stackBase&&address<stackTop)return stack[address-stackBase];if(address==0xbfe001)return cia;if(address==0xbfe201)return ddra;if(address==0xdff00c)return joy>>8;if(address==0xdff00d)return joy&255;if(address>=stopPc&&address<stopPc+4)return address&1?0x71:0x4e;if(address==0xdff002||address==0xdff003)return 0;badAddress=address;invalid=true;return 0;}
void write(unsigned address,unsigned value){if(address<ram.size()){ram[address]=std::uint8_t(value);return;}if(address>=stackBase&&address<stackTop){stack[address-stackBase]=std::uint8_t(value);return;}if(address==0xbfe201){ddra=value;return;}if(address>=0xdff000&&address<0xdff200)return;badAddress=address;invalid=true;}
std::vector<std::uint8_t> readFile(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Missing scene input "+path.string());return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void prepareExpectedAllocation(const scr::runtime::RaceFrame& snapshot,const scr::TrackProgressData& initial,const scr::OpponentFarTrackData& farData,const scr::RenderProjectionTables& tables,const std::array<std::uint8_t,100>& renderFlags,const scr::MountainHorizonData& mountains){
    auto geometry=snapshot.nearSeed;const auto& data=snapshot.movingTrack?snapshot.movingTrack->progress:initial;const auto near=scr::buildNearTrackFrame(data,renderFlags,snapshot.nearInputs,geometry);expectedNear=std::make_unique<scr::NearTrackFrame>(near);const auto far=scr::buildOpponentFarTrack(snapshot.movingTrack?snapshot.movingTrack->farTrack:farData,tables,geometry,near.queue);
    auto lineBytes=scr::allocateOpponentBackgroundLines(far.queue.lineBytes,mountains,snapshot.cameraYaw,snapshot.camera,geometry.edgeVisibilityMode);const scr::MountainHorizonData emptyMountains{};expectedHorizonBytes=scr::allocateOpponentBackgroundLines(far.queue.lineBytes,emptyMountains,snapshot.cameraYaw,snapshot.camera,geometry.edgeVisibilityMode);expectedFarBytes=far.queue.lineBytes;expectedBackgroundBytes=lineBytes;auto marker=far.queue.opponentOffset;expectedLineBytes=0;
    const scr::NearTrackPainter painter=[&](scr::NearTrackPainterSlot slot,std::uint16_t first,std::vector<scr::NearTrackDrawCommand>&){if(scr::consumeOpponentCarPainterSlot(static_cast<scr::OpponentCarPainterSlot>(slot),0,first,marker)){expectedLineBytes=lineBytes;auto inputs=snapshot.opponent;inputs.lineBytes=lineBytes;lineBytes=scr::buildOpponentCarFrame(inputs,tables).lineBytes;}};
    (void)scr::drawOpponentFarTrack(far,snapshot.nearInputs.drawing,painter);(void)scr::drawNearTrackQueue(near.queue,snapshot.nearInputs.drawing,painter,&lineBytes);
}
} // namespace
extern "C" {unsigned m68k_read_memory_8(unsigned a){return read(a);}unsigned m68k_read_memory_16(unsigned a){return (read(a)<<8)|read(a+1);}unsigned m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}void m68k_write_memory_8(unsigned a,unsigned v){write(a,v);}void m68k_write_memory_16(unsigned a,unsigned v){write(a,v>>8);write(a+1,v);}void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}}
int main(int argc,char** argv){try{
    if(argc<2||argc>3)throw std::invalid_argument("Usage: opponent_scene_oracle integration-directory [maximum-ticks-per-track]");const std::filesystem::path directory=argv[1];const unsigned maximum=argc==3?unsigned(std::stoul(argv[2])):256;const auto payload=readFile(directory/"source-payload.bin");const auto mountains=scr::decodeMountainHorizonData(payload.data(),payload.size());
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);savedCpu.resize(m68k_context_size());
    for(unsigned track=0;track<8;++track){currentTrack=track;const auto base=directory/("track-"+std::to_string(track));auto seed=replay::loadCapture((base/"seed.bin").string().c_str());const auto masks=replay::loadInputs((base/"controls.csv").string().c_str());ram=*seed;auto scene=std::make_unique<scr::runtime::RaceScene>();scene->loadNativeState(*seed,payload,base/"scene-oracle-recordings");
        // The native renderer decodes these presentation assets directly.
        // Initialize their original RAM form through the untouched loader so
        // the CPU reference includes mountains, as the original menu does.
        leaf=true;done=invalid=false;instructions=0;m68k_pulse_reset();for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);for(unsigned n=0;n<4;++n)stack[4092+n]=std::uint8_t(stopPc>>(24-8*n));m68k_set_reg(M68K_REG_PC,0x696fc);m68k_execute(100000);leaf=false;if(invalid||!done)throw std::runtime_error("Original mountain loader failed");
        const auto progress=scr::test::trackProgress::readData(*seed);scr::OpponentFarTrackData far{};far.mesh=scr::state::mesh::decodeTrackMesh(*seed);std::copy_n(seed->begin()+0x65e70,100,far.projectionLimits.begin());const auto tables=scr::test::renderProjection::readTables(seed->data());currentTables=&tables;std::array<std::uint8_t,100> renderFlags{};std::array<bool,100> known{};for(const auto& section:far.mesh.sections)if(!known[section.segment]){known[section.segment]=true;renderFlags[section.segment]=section.flags;}
        cia=ddra=255;for(unsigned tick=0;tick<std::min(maximum,unsigned(masks.size()));++tick){currentTick=tick;scene->stepLive(masks[tick].controls,masks[tick].quit);frame=&scene->frames().back();prepareExpectedAllocation(*frame,progress,far,tables,renderFlags,mountains);const auto hardware=replay::hardware(masks[tick].controls,std::uint8_t(ddra));joy=hardware.joy1dat;cia=hardware.ciaPortA;std::fill_n(ram.begin()+0xead6,128,std::uint8_t{});ram[0xead6+0x45]=masks[tick].quit?0xb3:0;
            if(!tick){m68k_pulse_reset();for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop);m68k_set_reg(M68K_REG_PC,entry);}else m68k_set_context(savedCpu.data());
            instructions=badAddress=0;invalid=done=outerRunning=restarting=inCar=frameCompared=false;firstEntry=true;failure.clear();originalCommands.clear();m68k_execute(80000000);if(invalid||!done||!frameCompared){std::ostringstream detail;detail<<"Scene original loop failed track="<<track<<" tick="<<tick<<" pc=0x"<<std::hex<<pc<<" address=0x"<<badAddress<<std::dec<<" instructions="<<instructions<<" "<<failure;throw std::runtime_error(detail.str());}originalInstructions+=instructions;++ticks;if(scene->liveTerminal())break;
        }
    }
    std::cout<<"{\"ticks\":"<<ticks<<",\"command_frames\":"<<commandFrames<<",\"car_calls\":"<<carCalls<<",\"car_commands\":"<<carCommands<<",\"car_input_values\":"<<inputComparisons<<",\"line_allocations\":"<<allocationComparisons<<",\"mountain_pointer_changes\":"<<mountainAllocations<<",\"horizon_pointer_changes\":"<<horizonAllocations<<",\"original_instructions\":"<<originalInstructions<<",\"mismatches\":0,\"original_renderer_calls_replaced\":false,\"original_code_modified\":false,\"independent_evolving_native_scene\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<"opponent_scene_oracle: "<<e.what()<<" track="<<currentTrack<<" tick="<<currentTick<<'\n';return 1;}}
