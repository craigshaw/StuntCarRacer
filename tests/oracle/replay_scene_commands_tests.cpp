#include "replay_scene_commands.h"
#include "native_startup.h"
#include "native_race_initialization.h"
#include "track_initialization.h"
#include "state/track_mesh_adapter.h"
#include "driving_replay_fixture.h"
#include "near_track_visibility_fixture.h"
#include "opponent_car_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
namespace f=scr::test::nearVisibility;
using Ram=scr::viewer::NativeMemory;
Ram ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
bool invalid{},done{},drawing{},inCar{};
unsigned pc{},stopAt{},instructions{},lineColor{},carReturn{},carCalls{};
std::uint64_t cases{},frames{},totalInstructions{},commandsCompared{},verticesCompared{},practiceCases{},carAttempts{},carAdmissions{},arenaCases{},sequentialFrames{},horizonModes[2]{};
using Key=std::array<int,5>;
struct Command {unsigned kind{},color{};std::vector<Key> edges;};
std::vector<Command> commands;
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
unsigned read(unsigned address){
    if(address<ram.size())return ram[address];
    if(address>=stackBase&&address<stackTop)return stack[address-stackBase];
    if(address>=sentinel&&address<sentinel+4)return address&1?0x71:0x4e;
    if(address==0xdff002||address==0xdff003)return 0;
    invalid=true;return 0;
}
void write(unsigned address,unsigned value){
    if(address<ram.size()){ram[address]=std::uint8_t(value);return;}
    if(address>=stackBase&&address<stackTop){stack[address-stackBase]=std::uint8_t(value);return;}
    if(address>=0xdff000&&address<0xdff200)return;
    invalid=true;
}
unsigned readLong(unsigned address){return (read(address)<<24)|(read(address+1)<<16)|(read(address+2)<<8)|read(address+3);}
void ret(){const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);m68k_set_reg(M68K_REG_PC,readLong(sp));m68k_set_reg(M68K_REG_SP,sp+4);}
Key key(unsigned packed){const auto p=packed&0xffffff;if(!p)return {-1,0,0,0,0};return {int(packed>>24),int(f::word(ram.data(),p+4)),int(f::word(ram.data(),p)),int(f::word(ram.data(),p+6)),int(f::word(ram.data(),p+2))};}
Key key(const scr::NearTrackEdge& e){if(!e.present)return {-1,0,0,0,0};return {e.clipFlags,e.clippedFrom.x,e.clippedFrom.y,e.clippedTo.x,e.clippedTo.y};}
void hook(unsigned address){
    pc=address;
    if(address==stopAt||address==sentinel){done=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;}
    ++instructions;++totalInstructions;
    if(drawing){
        if(inCar&&address==carReturn)inCar=false;
        if(address==0x67aa6){++carCalls;inCar=true;carReturn=readLong(m68k_get_reg(nullptr,M68K_REG_SP));}
        if(address==0x66348){lineColor=m68k_get_reg(nullptr,M68K_REG_D0)&255;ret();return;}
        if(address==0x66618){
            Command command{};command.kind=inCar?2:0;command.color=ram[0x66475]>>4;
            for(unsigned n=0;n<4;++n)command.edges.push_back(key(m68k_get_reg(nullptr,static_cast<m68k_register_t>(M68K_REG_A0+n))));
            commands.push_back(std::move(command));m68k_set_reg(M68K_REG_D1,0);m68k_set_reg(M68K_REG_D2,0);ret();return;
        }
        if(address==0x6932e){commands.push_back({1,lineColor,{key(m68k_get_reg(nullptr,M68K_REG_A3))}});ret();return;}
        if(address==0x68544){
            Command command{};command.color=ram[0x69aec];const auto a=m68k_get_reg(nullptr,M68K_REG_A1),b=m68k_get_reg(nullptr,M68K_REG_A2);
            for(unsigned p=0x69b7c;p<a;p+=4)command.edges.push_back(key(f::longword(ram.data(),p)));
            for(unsigned p=b;p<0x69b7c;p+=4)command.edges.push_back(key(f::longword(ram.data(),p)));
            commands.push_back(std::move(command));m68k_set_reg(M68K_REG_D1,0);m68k_set_reg(M68K_REG_D2,0);ret();return;
        }
    }
    if(invalid||address<0xe700||address>=0x7abda||instructions>500000){invalid=true;m68k_end_timeslice();}
}
void run(unsigned entry,unsigned stop=0){
    done=invalid=false;instructions=0;stopAt=stop;
    for(unsigned n=0;n<4;++n)stack[4092+n]=std::uint8_t(sentinel>>(24-8*n));
    f::putLong(ram.data(),0,stackTop-4);f::putLong(ram.data(),4,entry);m68k_pulse_reset();
    for(unsigned n=0;n<15;++n)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+n),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_PC,entry);
    for(unsigned n=0;n<8&&!done&&!invalid;++n)m68k_execute(10000000);
    require(done&&!invalid,"Original scene command call failed pc="+std::to_string(pc));
}
void serialize(const scr::NearTrackQueueState& queue){
    f::writeQueueInputs(ram,queue);unsigned cursor=f::lineBase;
    for(unsigned n=0;n<queue.entries.size();++n)for(unsigned edgeIndex=0;edgeIndex<7;++edgeIndex){
        const auto& edge=queue.entries[n].edges[edgeIndex];if(!edge.present)continue;
        f::putLong(ram.data(),f::queueBase+n*32+edgeIndex*4,(std::uint32_t(edge.clipFlags)<<24)|cursor);
        f::putWord(ram.data(),cursor,edge.clippedFrom.y);f::putWord(ram.data(),cursor+2,edge.clippedTo.y);
        f::putWord(ram.data(),cursor+4,edge.clippedFrom.x);f::putWord(ram.data(),cursor+6,edge.clippedTo.x);
        for(unsigned offset=8;offset<edge.storageBytes;offset+=2)f::putWord(ram.data(),cursor+offset,0x8000);
        cursor+=edge.storageBytes;
    }
    require(cursor<=f::lineBase+queue.lineBytes,"Serialized dense queue exceeds allocation");
    f::putLong(ram.data(),0x68144,f::lineBase+queue.lineBytes);
}
void compareHistory(const scr::NearTrackGeometryState& geometry){
    for(unsigned n=0;n<60;++n)for(unsigned lower=0;lower<2;++lower){
        const auto& point=lower?geometry.lower[n]:geometry.upper[n];const auto offset=n*2+lower*120;
        require(point.height==f::word(ram.data(),0x1be70+offset)&&std::uint16_t(point.x)==f::word(ram.data(),0x1bfb0+offset)&&std::uint16_t(point.y)==f::word(ram.data(),0x1c0f0+offset),"Persistent scene vertex differs index="+std::to_string(n)+" lower="+std::to_string(lower));++verticesCompared;
    }
    for(unsigned n=0;n<30;++n)require(geometry.flags[n]==ram[0x1bdd0+4*n],"Persistent dense visibility flag differs");
    for(unsigned n=0;n<2;++n)require(geometry.transformedCoordinates[n]==f::word(ram.data(),0x1c0a0+2*n),"Unrolled road endpoint history differs");
}
void compareCommands(const scr::viewer::ReplaySceneCommands& result){
    require(result.presentation.commands.size()==commands.size(),"Scene command count differs native="+std::to_string(result.presentation.commands.size())+" original="+std::to_string(commands.size()));
    for(unsigned n=0;n<commands.size();++n){
        const auto& actual=result.presentation.commands[n];const auto& expected=commands[n];
        const auto kind=actual.surface==scr::NearTrackSurface::opponent?2u:actual.kind==scr::NearTrackCommandKind::line?1u:0u;
        std::vector<Key> edges;for(const auto& e:actual.edges)edges.push_back(key(e));auto original=expected.edges;
        for(auto& e:edges)e[0]=0;for(auto& e:original)e[0]=0;std::sort(edges.begin(),edges.end());std::sort(original.begin(),original.end());
        require(kind==expected.kind&&actual.color==expected.color&&edges==original,"Scene command material/boundary differs command="+std::to_string(n));++commandsCompared;
    }
}
void check(const Ram& baseline,const scr::NearTrackFrame& near,const scr::NearTrackGeometryState& geometry,
    const scr::NearTrackDrawInputs& drawingInputs,const scr::RenderProjectionTables& tables,
    const scr::OpponentFarTrackData& far,const scr::MountainHorizonData& mountains,
    std::uint16_t yaw,const std::optional<scr::OpponentCarInputs>& opponent){
    const auto result=scr::viewer::buildReplaySceneCommands(near,drawingInputs,tables,far,geometry,mountains,yaw,geometry.camera,opponent);
    ram=baseline;drawing=false;run(0x696fc);scr::test::nearGeometry::writeState(ram,geometry);serialize(near.queue);
    f::putWord(ram.data(),0x660fc,geometry.transformedCoordinates[0]);f::putWord(ram.data(),0x660fe,geometry.transformedCoordinates[1]);
    f::putWord(ram.data(),0x6932c,near.queue.offset);run(0x65ec4);run(0x67460);
    require(result.farLineBytes==f::longword(ram.data(),0x68144)-f::lineBase,"Far allocation differs");
    const auto farQueueEnd=f::word(ram.data(),0x68148);
    ram[0x1ca22]=opponent?0x80:0;ram[0x57c3c]=0;f::putWord(ram.data(),0x1bce6,yaw);
    run(0x6508c,0x650cc);
    require(result.horizonMode==ram[0x1bb68]&&result.presentation.horizonMode==result.horizonMode,"Original horizon mode not preserved");++horizonModes[result.horizonMode>>7];
    require(result.backgroundLineBytes==f::longword(ram.data(),0x68144)-f::lineBase,"Shared horizon/mountain allocation differs");
    require(farQueueEnd==f::word(ram.data(),0x68148),"Original background did not preserve road queue end");compareHistory(result.geometry);
    if(opponent){auto inputs=*opponent;inputs.camera=geometry.camera;inputs.lineBytes=result.backgroundLineBytes;scr::test::opponentCar::writeInputs(ram.data(),inputs);f::putWord(ram.data(),0x68148,farQueueEnd);}
    ram[0x1bb9a]=drawingInputs.renderingOrderMode;f::putWord(ram.data(),0x1bc5c,drawingInputs.offsetFromRoadCenter);
    ram[0x1bbdf]=drawingInputs.raceStartTimer;ram[0x1bbd1]=drawingInputs.offRoadSideFlags;
    ram[0x691b3]=drawingInputs.sideEdgeColor;ram[0x691b2]=drawingInputs.alternateSideColor;
    ram[0x1bb66]=drawingInputs.previewColorOverride?0x80:0;f::putWord(ram.data(),0x1bc1e,drawingInputs.minimumQueueOffset);
    commands.clear();carCalls=0;inCar=false;lineColor=0;drawing=true;run(0x690bc);f::putWord(ram.data(),0x68148,near.queue.offset);run(0x68f28);drawing=false;
    compareCommands(result);
    require(result.remainingOpponentOffset==f::word(ram.data(),0x1bc12),"Final optional-car marker differs");
    require(result.lineBytes==f::longword(ram.data(),0x68144)-f::lineBase,"Final shared line allocation differs");
    require(result.opponentAttempted==(carCalls!=0),"Optional car admission slot differs");
    if(result.opponentAttempted)++carAttempts;if(result.opponentAdmitted)++carAdmissions;if(!opponent)++practiceCases;
    if(near.queue.lineBytes>=0x2328)++arenaCases;
    require(std::all_of(result.presentation.coveredSegments.begin(),result.presentation.coveredSegments.end(),[](bool value){return value;}),"Generic far mesh remains enabled");
    require(result.presentation.queueEntries==near.presentation.queueEntries&&result.presentation.farStartSegment==near.presentation.farStartSegment,"Dense frame metadata was lost");++cases;
}
void sequentialPractice(const Ram& baseline,const scr::RenderProjectionTables& tables,
    const scr::OpponentFarTrackData& far,const scr::MountainHorizonData& mountains,
    const std::array<std::uint8_t,100>& flags){
    auto playback=std::make_unique<scr::test::replay::NativeReplay>(baseline);
    auto geometry=scr::test::nearGeometry::readState(baseline);
    ram=baseline;drawing=false;run(0x696fc);
    for(unsigned frame=0;frame<24;++frame){
        playback->restartStage(std::uint8_t((frame*7)%far.mesh.segmentCount));
        const auto yaw=std::uint16_t(scr_test::readWord(playback->ram,0x1bce6)+(frame%4)*0x4000);
        scr_test::writeWord(playback->ram,0x1bce6,yaw);playback->progressStage();
        const auto camera=scr::test::nearGeometry::readState(playback->ram).camera;
        scr::NearTrackFrameInputs inputs{};inputs.progress=scr::test::trackProgress::readState(playback->ram);inputs.camera=camera;
        inputs.displayUpdate=playback->ram[0x5d724];inputs.opponentSegment=playback->ram[0x1bba6];inputs.opponentSubSegment=playback->ram[0x1bc24];inputs.minimumBoundaryDistance=f::word(playback->ram.data(),0x63ede);
        inputs.drawing.minimumQueueOffset=f::word(playback->ram.data(),0x1bc1e);inputs.drawing.renderingOrderMode=playback->ram[0x1bb9a];inputs.drawing.raceStartTimer=playback->ram[0x1bbdf];inputs.drawing.offRoadSideFlags=inputs.progress.offRoadSide;inputs.drawing.offsetFromRoadCenter=inputs.progress.lateralOffset;inputs.drawing.sideEdgeColor=baseline[0x691b3];inputs.drawing.alternateSideColor=baseline[0x691b2];
        const auto near=scr::buildNearTrackFrame(playback->progressData,flags,inputs,geometry);
        const auto result=scr::viewer::buildReplaySceneCommands(near,inputs.drawing,tables,far,geometry,mountains,yaw,camera);
        // Refresh only gameplay-derived render inputs. Original projected vertex
        // and endpoint history survives independently from the preceding frame.
        scr::test::trackProgress::writeState(ram,inputs.progress);
        f::putWord(ram.data(),0x1bc42,camera.viewportCenterY);f::putWord(ram.data(),0x1c250,camera.sinRoll);f::putWord(ram.data(),0x1c252,camera.cosRoll);
        f::putWord(ram.data(),0x1bce6,yaw);ram[0x5d724]=inputs.displayUpdate;ram[0x1bba6]=inputs.opponentSegment;ram[0x1bc24]=inputs.opponentSubSegment;f::putWord(ram.data(),0x63ede,inputs.minimumBoundaryDistance);
        ram[0x1ca22]=0;ram[0x57c3c]=0;ram[0x1bbae]=ram[0x1bb7f]=0;f::putWord(ram.data(),0x1bc12,0xffff);
        ram[0x1bb9a]=inputs.drawing.renderingOrderMode;ram[0x1bbdf]=inputs.drawing.raceStartTimer;ram[0x691b3]=inputs.drawing.sideEdgeColor;ram[0x691b2]=inputs.drawing.alternateSideColor;ram[0x1bb66]=0;f::putWord(ram.data(),0x1bc1e,inputs.drawing.minimumQueueOffset);
        drawing=false;run(0x66fb8);run(0x64f80,0x650cc);
        require(result.farLineBytes<=result.backgroundLineBytes,"Background arena moved backwards");
        require(result.backgroundLineBytes==f::longword(ram.data(),0x68144)-f::lineBase,"Sequential full caller background allocation differs frame="+std::to_string(frame));
        require(near.queue.offset==f::word(ram.data(),0x6932c),"Sequential full caller dense queue differs");compareHistory(result.geometry);
        commands.clear();carCalls=0;inCar=false;lineColor=0;drawing=true;run(0x690bc);f::putWord(ram.data(),0x68148,near.queue.offset);run(0x68f28);drawing=false;
        compareCommands(result);require(!carCalls&&result.remainingOpponentOffset==f::word(ram.data(),0x1bc12),"Practice full caller attempted opponent");
        require(result.lineBytes==f::longword(ram.data(),0x68144)-f::lineBase,"Sequential final arena differs");geometry=result.geometry;++sequentialFrames;
    }
}
} // namespace
extern "C" {
unsigned m68k_read_memory_8(unsigned a){return read(a);}unsigned m68k_read_memory_16(unsigned a){return (read(a)<<8)|read(a+1);}unsigned m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){write(a,v);}void m68k_write_memory_16(unsigned a,unsigned v){write(a,v>>8);write(a+1,v);}void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv){try{
    require(argc==2,"Usage: replay_scene_commands_tests local.scr-assets");m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned context=0;context<2;++context)for(unsigned track=0;track<8;++track){
        auto source=scr::viewer::startNativeGame(argv[1]);(*source.memory)[0x1c9d0]=std::uint8_t(context?11:0);scr::initializeNativeTrack(*source.memory,track);scr::viewer::initializeNativeRace(*source.memory);
        const auto tables=scr::test::renderProjection::readTables(source.memory->data());const auto mountains=scr::decodeMountainHorizonData(source.payload.data(),source.payload.size());
        scr::OpponentFarTrackData far{};far.mesh=scr::viewer::decodeTrackMesh(*source.memory);std::copy_n(source.memory->begin()+0x65e70,100,far.projectionLimits.begin());
        std::array<std::uint8_t,100> flags{};for(unsigned n=0;n<far.mesh.segmentCount;++n)flags[n]=(*source.memory)[scr_test::readLong(*source.memory,0x7aa1a+4*n)];
        for(unsigned sample=0;sample<24;++sample){
            auto playback=std::make_unique<scr::test::replay::NativeReplay>(*source.memory);playback->restartStage(std::uint8_t((sample*7)%far.mesh.segmentCount));
            const auto yaw=std::uint16_t(scr_test::readWord(playback->ram,0x1bce6)+(sample%4)*0x4000);scr_test::writeWord(playback->ram,0x1bce6,yaw);playback->progressStage();
            auto geometry=scr::test::nearGeometry::readState(playback->ram);scr::NearTrackFrameInputs inputs{};
            inputs.progress=scr::test::trackProgress::readState(playback->ram);inputs.camera=geometry.camera;inputs.camera.viewportCenterY=std::uint16_t(int(inputs.camera.viewportCenterY)+(int(sample%3)-1)*512);
            inputs.displayUpdate=playback->ram[0x5d724];inputs.opponentSegment=std::uint8_t((inputs.progress.playerSegment+sample%5)%far.mesh.segmentCount);inputs.opponentSubSegment=std::uint8_t(sample%8);inputs.minimumBoundaryDistance=std::uint16_t(sample%3==0?0xff80:sample%3==1?0xfe00:0x100);
            auto near=scr::buildNearTrackFrame(playback->progressData,flags,inputs,geometry);geometry.edgeVisibilityMode=std::uint8_t(sample&1?0x80:0);
            scr::NearTrackDrawInputs drawingInputs{};drawingInputs.renderingOrderMode=std::uint8_t(sample%3);drawingInputs.offsetFromRoadCenter=std::uint16_t(sample&1?-512:512);drawingInputs.raceStartTimer=std::uint8_t(sample&1);drawingInputs.offRoadSideFlags=std::uint8_t(sample%3==0?0:sample%3==1?2:128);drawingInputs.sideEdgeColor=9;drawingInputs.alternateSideColor=10;drawingInputs.previewColorOverride=sample%7==0;
            for(unsigned variant=0;variant<5;++variant){
                auto dense=near;if(variant==4)dense.queue.lineBytes=std::uint16_t(std::max(unsigned(dense.queue.lineBytes),0x2328u+2*(sample%4)));
                std::optional<scr::OpponentCarInputs> opponent;
                if(variant){scr::OpponentCarInputs car{};car.relativeX={{std::uint16_t(-120),120,std::uint16_t(-120),120}};car.relativeZ={{1100,1100,1250,1250}};car.footprint.fill(std::uint16_t(geometry.camera.heightBaseline-1000));car.bounds.fill(std::uint16_t(geometry.camera.heightBaseline-1100));car.distance=std::uint16_t(variant==3?9:variant==4?3200:1100);car.roadOffset=128;car.aheadFlag=std::uint8_t(variant==2?128:0);car.curveSmoothing=std::uint8_t(sample%3==0?128:0);opponent=car;}
                check(playback->ram,dense,geometry,drawingInputs,tables,far,mountains,yaw,opponent);
            }++frames;
        }
        sequentialPractice(*source.memory,tables,far,mountains,flags);
    }
    require(practiceCases&&carAttempts&&carAdmissions&&arenaCases&&horizonModes[0]&&horizonModes[1],"Required scene branches were not exercised");
    std::cout<<"{\"frames\":"<<frames<<",\"cases\":"<<cases<<",\"practice_cases\":"<<practiceCases<<",\"sequential_practice_frames\":"<<sequentialFrames<<",\"car_attempts\":"<<carAttempts<<",\"car_admissions\":"<<carAdmissions<<",\"arena_limit_cases\":"<<arenaCases<<",\"commands\":"<<commandsCompared<<",\"history_vertices\":"<<verticesCompared<<",\"horizon_modes\":["<<horizonModes[0]<<','<<horizonModes[1]<<"],\"original_instructions\":"<<totalInstructions<<",\"mismatches\":0,\"original_code_modified\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<"replay_scene_commands_tests: "<<e.what()<<" frame="<<frames<<" case="<<cases<<'\n';return 1;}}
