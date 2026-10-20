#include "native_startup.h"
#include "replay_scene.h"
#include "track_mesh_capture.h"
#include "driving_replay_io.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
namespace replay=scr::test::replay;
using scr::viewer::ReplayScene;
using scr::viewer::ReplaySceneFrame;
using scr::viewer::ReplayTrackSnapshot;
void require(bool condition,const std::string& text){if(!condition)throw std::runtime_error(text);}
void write(const std::filesystem::path& path,const std::uint8_t* bytes,std::size_t count){
    std::ofstream stream(path,std::ios::binary);stream.write(reinterpret_cast<const char*>(bytes),std::streamsize(count));
    require(bool(stream),"Cannot write Draw Bridge test artifact "+path.string());
}
std::string readText(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);require(bool(stream),"Cannot read trace "+path.string());return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
struct Hash {
    std::uint64_t value=14695981039346656037ULL;
    void add(std::uint64_t word){for(unsigned i=0;i<8;++i){value^=word&255;value*=1099511628211ULL;word>>=8;}}
    void floating(double number){std::uint64_t bits{};std::memcpy(&bits,&number,sizeof(bits));add(bits);}
};
std::uint64_t pixelHash(const std::vector<std::uint32_t>& pixels){Hash h;for(auto pixel:pixels)h.add(pixel);return h.value;}
std::uint64_t geometryHash(const scr::TrackGeometryData& track){Hash h;for(auto byte:track.geometry)h.add(byte);return h.value;}
std::uint64_t meshHash(const scr::TrackMesh& mesh){
    Hash h;for(const auto& vertex:mesh.vertices){h.floating(vertex.x);h.floating(vertex.y);h.floating(vertex.z);}
    for(const auto& face:mesh.faces){for(auto index:face.indices)h.add(index);h.add(face.segment);h.add(face.flags);}return h.value;
}
std::uint64_t nearHash(const scr::NearTrackPresentation& near){
    Hash h;h.add(near.queueEntries);h.add(near.farStartSegment);for(auto covered:near.coveredSegments)h.add(covered);
    for(const auto& command:near.commands){h.add(unsigned(command.kind));h.add(unsigned(command.surface));h.add(command.color);h.add(command.originQueueOffset);h.add(command.firstQueueOffset);
        for(const auto& edge:command.edges){for(const auto point:{edge.from,edge.to,edge.clippedFrom,edge.clippedTo}){h.add(unsigned(point.x));h.add(unsigned(point.y));}
            h.add(edge.present);h.add(edge.clipFlags);h.add(edge.originQueueOffset);h.add(edge.originEdge);h.add(edge.storageBytes);}
    }return h.value;
}
std::uint64_t surfaceHash(const scr::TrackProgressData& data){
    // A fixed camera over segment51 removes car movement from this check. The
    // actual dense-near projector must see the changing bridge surface heights.
    scr::NearTrackGeometryState geometry;geometry.progress.currentSegment=0x33;
    const auto coordinate=data.segmentCoordinates[0x33];geometry.progress.viewX=coordinate&15;geometry.progress.viewZ=coordinate>>4;
    geometry.progress.cameraHeight=0x400;geometry.camera.cosRoll=0x7fff;geometry.preview=0x80;geometry.displayUpdate=0x80;
    scr::projectNearTrackSegment(data,geometry);Hash h;for(const auto& vertex:geometry.upper)h.add(vertex.height);return h.value;
}
bool onBridge(unsigned segment){return segment>=0x33&&segment<0x38;}
std::filesystem::path liveTrace(const ReplayScene& scene){const auto name=scene.liveRecording().string();return name.substr(0,name.size()-std::string("-controls.csv").size())+"-native.csv";}
struct Counts {
    unsigned ticks{},occupied{},phaseHeld{},released{},rawChanges{},meshChanges{},surfaceChanges{},rendered{},queries{},retainedQueries{};
    unsigned firstEntry=~0U,firstExit=~0U;std::set<unsigned> visited;
};
void runScenario(const scr::viewer::NativeStartupData& startup,const std::filesystem::path& output,Counts& counts){
    std::filesystem::create_directories(output);
    auto placement=std::make_unique<replay::NativeReplay>(*startup.memory);
    placement->restartStage(0x31);
    const auto seed=std::make_unique<replay::Ram>(placement->ram);
    write(output/"seed.bin",seed->data(),seed->size());
    std::ofstream description(output/"scenario.txt");description<<"Synthetic verification placement authored by the recovered native restart initializer at segment49.\n"
        <<"No RAM capture and no claim of a completed lap. Hold original digital accelerate input for512ticks.\n";description.close();
    auto state=std::make_unique<replay::NativeReplay>(*seed);
    ReplayScene scene;scene.loadNativeState(*seed,startup.payload,output/"recordings");
    std::ofstream controls(output/"controls.csv"),trace(output/"native.csv");controls<<"tick,controls\n";replay::traceHeader(trace);
    auto previousRaw=geometryHash(state->progressData.track);
    auto previousMesh=meshHash(scr::buildTrackMesh(scr::viewer::decodeTrackMesh(state->ram)));
    auto previousSurface=surfaceHash(state->progressData);
    std::shared_ptr<const ReplayTrackSnapshot> frozen;std::uint64_t frozenRaw{},frozenMesh{},frozenSurface{};
    std::vector<std::uint64_t> liveExact,liveMidpoints;
    bool entered=false,left=false;
    for(unsigned tick=0;tick<512;++tick){
        const unsigned mask=1;controls<<tick<<','<<mask<<'\n';state->begin(mask);state->controlsStage();state->physicsStage();state->engineStage();state->progressStage();
        const auto phase=state->ram[0x1bbb0],player=state->ram[0x1bb1c],opponent=state->ram[0x1bb1d],active=state->ram[0x1bb3f],throttle=state->ram[0x1bbcd];
        const auto frameRaw=geometryHash(state->progressData.track);const auto frameMesh=meshHash(scr::buildTrackMesh(scr::viewer::decodeTrackMesh(state->ram)));
        const auto framePosition=std::array<std::uint32_t,3>{{scr_test::readLong(state->ram,0x1bcd8),scr_test::readLong(state->ram,0x1bcdc),scr_test::readLong(state->ram,0x1bce0)}};
        state->groundStage();state->wheelsStage();state->bridgeStage();
        const bool held=onBridge(player)||onBridge(opponent)||(opponent>=0x30&&opponent<0x38&&active);
        require(state->ram[0x1bbb0]==std::uint8_t(phase+((held||(throttle&0x80))?0:1)),"Driving bridge phase does not match occupancy/throttle lifecycle");
        if(onBridge(player)){++counts.occupied;++counts.phaseHeld;require(state->ram[0x1bbb0]==phase,"Player occupancy failed to hold phase");if(!entered){entered=true;counts.firstEntry=tick;}}
        else if(entered&&!left&&player>=0x38&&player<0x40){left=true;counts.firstExit=tick;}
        if(left&&!held&&!(throttle&0x80)){++counts.released;require(state->ram[0x1bbb0]!=phase,"Bridge failed to resume after release");}
        counts.visited.insert(player);
        require(state->physicsData.track.geometry==state->progressData.track.geometry&&state->restartData.track.geometry==state->progressData.track.geometry,"Gameplay consumers retained stale bridge geometry");
        const auto raw=geometryHash(state->progressData.track);const auto mesh=meshHash(scr::buildTrackMesh(scr::viewer::decodeTrackMesh(state->ram)));const auto surface=surfaceHash(state->progressData);
        if(held)require(raw==frameRaw&&mesh==frameMesh,"Occupied bridge geometry/cache moved while phase was held");
        counts.rawChanges+=raw!=previousRaw;counts.meshChanges+=mesh!=previousMesh;counts.surfaceChanges+=surface!=previousSurface;
        previousRaw=raw;previousMesh=mesh;previousSurface=surface;
        state->damageStage();state->timingStage();state->outerStage();
        replay::traceRow(trace,state->ram,tick,mask,0,unsigned(state->outerAction),state->restarts);
        require(scene.stepLive(mask),"Scene stopped during synthetic bridge approach");
        const auto& frame=scene.frames().back();require(frame.movingTrack!=nullptr,"Moving track frame lacks an immutable snapshot");
        require(frame.pose.worldPosition==framePosition,"Scene physics diverged from independent native driving");
        require(frame.movingTrack->bridgePhase==phase,"Scene snapshots used a later bridge phase");
        require(geometryHash(frame.movingTrack->progress.track)==frameRaw&&meshHash(frame.movingTrack->mesh)==frameMesh,"Scene consumers retained stale track geometry");
        auto nearSeed=frame.nearSeed;std::array<std::uint8_t,100> renderFlags{};std::array<bool,100> known{};
        for(const auto& section:scr::viewer::decodeTrackMesh(state->ram).sections)if(!known[section.segment]){renderFlags[section.segment]=section.flags;known[section.segment]=true;}
        const auto near=scr::buildNearTrackFrame(frame.movingTrack->progress,renderFlags,frame.nearInputs,nearSeed);
        require(nearHash(near.presentation)==nearHash(frame.nearTrack),"Scene near geometry used a later bridge state");
        if(!frozen){frozen=frame.movingTrack;frozenRaw=frameRaw;frozenMesh=frameMesh;frozenSurface=surfaceHash(frozen->progress);}
        require(geometryHash(frozen->progress.track)==frozenRaw&&meshHash(frozen->mesh)==frozenMesh&&surfaceHash(frozen->progress)==frozenSurface,"Past moving-track snapshot mutated while driving");
        const auto frameSignature=nearHash(frame.nearTrack)^geometryHash(frame.movingTrack->progress.track)^meshHash(frame.movingTrack->mesh);
        for(double fraction:{0.0,0.25,0.5,0.75,1.0}){
            const auto& pixels=scene.renderLive(fraction);require(pixels.size()==320*200,"Bridge render has wrong dimensions");
            if(fraction==0.5)liveMidpoints.push_back(pixelHash(pixels));if(fraction==1.0)liveExact.push_back(pixelHash(pixels));++counts.queries;
        }
        require(scene.liveTicks()==tick+1&&frameSignature==(nearHash(frame.nearTrack)^geometryHash(frame.movingTrack->progress.track)^meshHash(frame.movingTrack->mesh)),"Interpolation query mutated simulation/frame state");
        ++counts.rendered;++counts.ticks;require(!scene.liveTerminal(),"Synthetic bridge drive reached an unexpected terminal state");
    }
    controls.close();trace.close();require(readText(output/"native.csv")==readText(liveTrace(scene)),"Live scene trace differs from independent native driving");
    std::ostringstream visited;for(auto segment:counts.visited)visited<<segment<<' ';
    require(entered&&left,"Synthetic approach did not enter and leave bridge normally; visited "+visited.str());
    require(counts.occupied>0&&counts.released>0,"Missing occupancy/release coverage");
    require(counts.rawChanges>8&&counts.meshChanges>8&&counts.surfaceChanges>8,"Bridge changes did not reach raw/cache/near-surface consumers");
    // Reuse the authored native trace as a consistency input here. The parent
    // runner independently verifies this exact seed/controls against original
    // code before treating the trace as original-validated evidence.
    ReplayScene retained;retained.load(output/"seed.bin",output.parent_path()/"source-payload.bin",output/"controls.csv",output/"native.csv",output/"scene.csv");
    std::vector<std::uint64_t> exact,interpolated;const auto total=unsigned(retained.frames().size());
    for(unsigned tick=0;tick<total;tick+=7){
        exact.push_back(pixelHash(retained.render(tick)));interpolated.push_back(pixelHash(retained.renderInterpolated(tick+0.5)));counts.retainedQueries+=2;
        require(exact.back()==liveExact.at(tick),"Retained exact frame differs from its live bridge snapshot");
        if(tick+1<total){const auto& from=retained.frames()[tick];const auto& to=retained.frames()[tick+1];
            if(scr::viewer::canInterpolateReplay(from.restarts,to.restarts,from.action,to.action))
                require(interpolated.back()==liveMidpoints.at(tick+1),"Retained interpolation differs from its live bridge snapshot");
        }
    }
    for(unsigned reverse=unsigned(exact.size());reverse>0;--reverse){const auto sample=reverse-1,tick=sample*7;
        require(pixelHash(retained.renderInterpolated(tick+0.5))==interpolated[sample],"Reverse seek changed interpolated bridge frame");
        require(pixelHash(retained.render(tick))==exact[sample],"Reverse seek changed exact bridge frame");
        require(pixelHash(retained.renderInterpolated(tick+0.5))==interpolated[sample],"Repeated seek changed interpolated bridge frame");counts.retainedQueries+=3;
    }
    require(readText(output/"scene.csv")==readText(output/"native.csv"),"Retained scene changed physics trace");
    require(exact.size()>8&&std::set<std::uint64_t>(exact.begin(),exact.end()).size()>8,"Retained bridge frames did not visibly evolve");
}
} // namespace
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("draw_bridge_driving_tests pack output-directory");
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
    auto startup=scr::viewer::startNativeRace(argv[1],5);write(output/"source-payload.bin",startup.payload.data(),startup.payload.size());
    Counts counts;runScenario(startup,output/"synthetic-approach",counts);
    std::cout<<"{\"status\":\"passed\",\"synthetic_placement\":true,\"completed_lap_claim\":false,\"ticks\":"<<counts.ticks
        <<",\"bridge_occupied_ticks\":"<<counts.occupied<<",\"phase_held_ticks\":"<<counts.phaseHeld<<",\"released_ticks\":"<<counts.released
        <<",\"first_entry_tick\":"<<counts.firstEntry<<",\"first_exit_tick\":"<<counts.firstExit<<",\"raw_geometry_changes\":"<<counts.rawChanges
        <<",\"mesh_height_changes\":"<<counts.meshChanges<<",\"near_surface_changes\":"<<counts.surfaceChanges<<",\"rendered_ticks\":"<<counts.rendered
        <<",\"interpolation_queries\":"<<counts.queries<<",\"retained_seek_queries\":"<<counts.retainedQueries<<",\"capture_input\":false,\"cpu_emulation\":false}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
