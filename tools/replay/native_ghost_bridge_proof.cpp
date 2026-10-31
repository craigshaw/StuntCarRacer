#include "assets/native_startup.h"
#include "menus/native_menu_flow.h"
#include "race/native_race_initialization.h"
#include "race/race_scene.h"
#include "presentation/ghost_bridge.h"
#include "state/replay_io.h"
#include "state/opponent_driving_adapter.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
using namespace scr;using namespace scr::runtime;
namespace replay=scr::state::replay;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void write(const std::filesystem::path& path,const std::uint8_t* data,std::size_t size){
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(data),std::streamsize(size));require(bool(out),"Cannot write "+path.string());
}
std::uint64_t pixelsHash(const std::vector<std::uint32_t>& pixels){std::uint64_t h=14695981039346656037ULL;for(auto p:pixels){h^=p;h*=1099511628211ULL;}return h;}
std::string regionName(GhostBridgeRegion region){switch(region){case GhostBridgeRegion::approach:return "approach";case GhostBridgeRegion::crossing:return "crossing";case GhostBridgeRegion::landing:return "landing";default:return "outside";}}
bool onBridge(unsigned segment){return segment>=0x33&&segment<0x38;}
NativeStartupData leagueBridge(const std::filesystem::path& pack){
    for(unsigned division=0;division<4;++division)for(unsigned parity=0;parity<2;++parity){
        auto game=startNativeGame(pack);auto& m=*game.memory;initializeNativeMenuSession(m);m[0x1c9ce]=std::uint8_t(division);m[0x1ca22]=0x80;
        const auto target=unsigned(m[0x5ab42+division])+2;unsigned local=0;while(local<12&&m[0x1c9c2+local]!=11)++local;
        require(local<12,"Missing native human league driver");std::swap(m[0x1c9c2+target],m[0x1c9c2+local]);
        initializeNativeSeason(m);require(seekNativeSeasonRace(m),"No human league fixture");
        if(parity){advanceNativeSeasonRound(m);require(seekNativeSeasonRace(m),"No odd league fixture");}
        if(m[0x1ca33]!=5)continue;
        m[0x57c68]=0x80;initializeNativePlayerConfiguration(m);initializeNativeTrack(m,5);
        if(scr::state::drawBridge::animate(m).geometryUpdated)rebuildNativeTrackGeometryCache(m);
        initializeNativeRace(m);return game;
    }throw std::runtime_error("Native league routes did not select Draw Bridge");
}
NativeStartupData fixture(const std::filesystem::path& pack,unsigned phase,bool league){
    auto game=league?leagueBridge(pack):startNativeRace(pack,5);
    auto& m=*game.memory;
    // All authored state is prepared before the measured run. There is no
    // phase/occupancy/vehicle injection while driving or querying the ghost.
    const auto data=scr::state::drawBridge::readData(m);auto bridge=scr::state::drawBridge::readState(m,data);
    bridge.animationPhase=std::uint8_t(phase);bridge.active=0;bridge.updateFlag=0x80;
    const auto update=animateDrawBridge(data,{5,0,0,0x80},bridge);
    // Use the normal bridge cache update. Rebuilding the complete track after
    // race entry would interpret race-local projection scratch as cache inputs.
    scr::state::drawBridge::writeState(m,data,bridge,update);
    auto placement=std::make_unique<replay::NativeReplay>(m);placement->restartStage(0x30);m=placement->ram;
    if(league){
        auto opponentData=std::make_unique<OpponentDrivingData>(scr::state::opponent::readData(m));
        auto opponent=scr::state::opponent::readState(m);opponent.segment=0x33;opponent.subSegment=0x100;opponent.lateral=128;
        initializeOpponentCoordinates(*opponentData,scr::state::opponent::readInputs(m),opponent);scr::state::opponent::writeState(m,opponent);
    }
    return game;
}
struct Advance {unsigned phase{},nextPhase{},player{},opponent{};bool held{},opponentOnly{};};
Advance advance(replay::NativeReplay& state){
    state.begin(1);state.controlsStage();state.physicsStage();state.engineStage();state.progressStage();
    Advance result;result.phase=state.ram[0x1bbb0];result.player=state.ram[0x1bb1c];result.opponent=state.ram[0x1bb1d];
    result.held=onBridge(result.player)||onBridge(result.opponent)||(result.opponent>=0x30&&result.opponent<0x38&&state.ram[0x1bb3f]);
    result.opponentOnly=onBridge(result.opponent)&&!onBridge(result.player);
    state.groundStage();state.wheelsStage();state.bridgeStage();result.nextPhase=state.ram[0x1bbb0];
    state.speedometerStage();state.damageStage();state.timingStage();state.distanceStage();state.outerStage();return result;
}
GhostBridgeShape shape(const RaceFrame& frame,unsigned offset){
    require(bool(frame.movingTrack),"Bridge frame lacks a retained track snapshot");
    return captureGhostBridgeShape(frame.movingTrack->progress.track,frame.movingTrack->farTrack.mesh,offset);
}
double roadHeight(const TrackMesh& mesh,const RenderWorldPoint& p){
    double best=std::numeric_limits<double>::quiet_NaN(),error=1e9;
    for(const auto& face:mesh.faces)for(unsigned n=1;n<3;++n){
        const auto& a=mesh.vertices[face.indices[0]];const auto& b=mesh.vertices[face.indices[n]];const auto& c=mesh.vertices[face.indices[n+1]];
        const auto d=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);if(std::abs(d)<1e-8)continue;
        const auto u=((b.z-c.z)*(p.x-c.x)+(c.x-b.x)*(p.z-c.z))/d;
        const auto v=((c.z-a.z)*(p.x-c.x)+(a.x-c.x)*(p.z-c.z))/d,w=1-u-v;
        if(std::min({u,v,w})<-1e-5)continue;
        const auto y=u*a.y+v*b.y+w*c.y;if(std::abs(y-p.y)<error){best=y;error=std::abs(y-p.y);}
    }return best;
}
struct Recorded {
    PresentationPose presentation;
    GhostBridgeShape bridge;
    GhostBridgeRegion region{};
    unsigned segment{},phase{},restarts{},action{};
    double clearance{};bool cut{},grounded{};
};
struct Recording {
    std::vector<Recorded> samples;
    unsigned approach=~0u,entry=~0u,exit=~0u,opponentHolds{};
};
Recording record(const NativeStartupData& game,const std::filesystem::path& output,unsigned offset){
    std::filesystem::create_directories(output);write(output/"seed.bin",game.memory->data(),game.memory->size());
    auto scene=std::make_unique<RaceScene>();scene->loadNativeState(*game.memory,game.payload,{});
    auto native=std::make_unique<replay::NativeReplay>(*game.memory);Recording result;GhostBridgeRegionTracker guard;
    std::ofstream controls(output/"controls.csv"),trace(output/"native.csv"),observations(output/"recording.csv");
    controls<<"tick,controls\n";replay::traceHeader(trace);observations<<"tick,segment,phase,region,grounded,clearance,opponent_only_hold\n";
    for(unsigned tick=0;tick<512;++tick){
        const auto update=advance(*native);require(scene->stepLive(1),"Recorded league approach ended early");const auto& frame=scene->frames().back();
        require(frame.movingTrack->bridgePhase==update.phase,"Recording captured bridge after its update");
        if(update.held)require(update.phase==update.nextPhase,"Real occupancy failed to hold bridge");
        result.opponentHolds+=update.opponentOnly;
        Recorded sample;sample.presentation=frame;sample.bridge=shape(frame,offset);sample.segment=frame.nearInputs.progress.playerSegment;
        sample.phase=frame.movingTrack->bridgePhase;sample.restarts=frame.restarts;sample.action=frame.action;
        sample.cut=tick&&!canInterpolatePresentation(result.samples.back().restarts,sample.restarts,result.samples.back().action,sample.action);
        const auto pose=makeGhostCarPose(frame.pose);sample.clearance=pose.position.y-roadHeight(frame.movingTrack->mesh,pose.position);
        // The native suspension's contact value belongs to this same pose.
        // Mesh clearance is diagnostic: sparse cached ramps need not coincide
        // with the dense surface used by the suspension sampler.
        sample.grounded=frame.pose.averageWheelVelocity!=0&&!frame.nearInputs.progress.offRoadSide;
        sample.region=guard.observe(sample.segment,sample.grounded,sample.cut);
        if(sample.region==GhostBridgeRegion::approach&&result.approach==~0u)result.approach=tick;
        if(sample.region==GhostBridgeRegion::crossing&&result.entry==~0u)result.entry=tick;
        if(tick&&sample.region==GhostBridgeRegion::outside&&result.samples.back().region==GhostBridgeRegion::landing&&!sample.cut&&result.exit==~0u)result.exit=tick;
        observations<<tick<<','<<sample.segment<<','<<sample.phase<<','<<regionName(sample.region)<<','<<sample.grounded<<','<<sample.clearance<<','<<update.opponentOnly<<'\n';
        result.samples.push_back(std::move(sample));controls<<tick<<",1\n";
        replay::traceRow(trace,native->ram,tick,1,0,unsigned(native->outerAction),native->restarts);
    }
    require(result.entry!=~0u&&result.exit!=~0u,"Recorded trajectory did not cross and land normally");
    require(result.opponentHolds>0,"League recording did not include a real opponent geometry hold");return result;
}
CameraViewSettings observer(const PresentationPose& live,const PresentationPose& target,NativeView view){
    constexpr double radians=6.28318530717958647692/65536;CameraViewSettings settings;settings.showCockpit=false;settings.view=view;
    const auto from=makeGhostCarPose(live.pose).position,to=makeGhostCarPose(target.pose).position;
    // All scenarios view the recorded trajectory from the same observer. Only
    // the private camera moves; the ghost and live car keep their actual poses.
    settings.worldOffset={to.x-from.x-36*std::sin(target.pose.yaw*radians),to.y-from.y+12,to.z-from.z-36*std::cos(target.pose.yaw*radians)};
    settings.yawOffset=std::int16_t(std::uint16_t(int(target.pose.yaw)-live.pose.yaw));
    settings.pitchOffset=std::int16_t(std::uint16_t(-int(live.pose.pitch)-0x500));settings.rollOffset=std::int16_t(std::uint16_t(-int(live.pose.roll)));return settings;
}
struct Query {double position{},seconds{};CameraViewSettings camera;GhostCarPose ghost;double alpha{};std::uint64_t hash{};};
struct Counts {unsigned queries{},hidden{},fading{},shown{},mismatches{},sameShapeDifferentPhase{},held{},released{},missing{},reappeared{},seekChecks{},visibleQueries{},equalPhaseCrossingShown{};};
Counts scenario(const NativeStartupData& game,const Recording& recording,const TrigTable& trig,unsigned offset,
    const std::filesystem::path& output,const std::filesystem::path& payload,bool preview){
    std::filesystem::create_directories(output);write(output/"seed.bin",game.memory->data(),game.memory->size());
    auto normal=std::make_unique<RaceScene>(),drawn=std::make_unique<RaceScene>();
    normal->loadNativeState(*game.memory,game.payload,{});drawn->loadNativeState(*game.memory,game.payload,{});
    auto native=std::make_unique<replay::NativeReplay>(*game.memory);GhostBridgeVisibility visibility;Counts counts;std::vector<Query> retained;
    std::ofstream controls(output/"controls.csv"),trace(output/"native.csv"),timeline(output/"timeline.csv");
    controls<<"tick,controls\n";replay::traceHeader(trace);timeline<<"frame,source_tick,seconds,region,shapes_match,alpha,conditions_differ,ghost_x,ghost_y,ghost_z,phase_live,phase_recorded\n";
    unsigned imageIndex=0;
    for(unsigned tick=0;tick<recording.samples.size();++tick){
        const auto update=advance(*native);require(normal->stepLive(1)&&drawn->stepLive(1),"Live bridge scenario ended early");
        const auto& frame=drawn->frames().back();const auto& source=recording.samples[tick];const auto actual=shape(frame,offset);
        require(frame.movingTrack->bridgePhase==update.phase,"Live query used a future bridge shape");
        const bool matches=actual==source.bridge;
        counts.mismatches+=!matches;counts.sameShapeDifferentPhase+=matches&&update.phase!=source.phase;
        counts.held+=update.held;counts.released+=!update.held&&update.phase!=update.nextPhase;
        visibility.observe(tick*gameplayTickSeconds,source.region,matches,source.cut);
        for(auto view:{NativeView::Original,NativeView::Extended,NativeView::Widescreen,NativeView::Ultrawide})for(double fraction:{0.5,1.0}){
            const bool cut=tick==0||source.cut||!canInterpolatePresentation(drawn->frames()[0].restarts,frame.restarts,drawn->frames()[0].action,frame.action);
            const double displayed=cut?double(tick):tick-1+fraction;const auto index=unsigned(displayed);const double mix=displayed-index;
            const auto& a=recording.samples[index];const auto& b=recording.samples[std::min(index+1,unsigned(recording.samples.size()-1))];
            const auto target=mix>0&&!b.cut?interpolatePresentation(a.presentation,b.presentation,mix,trig):a.presentation;
            const auto ghost=makeGhostCarPose(target.pose);
            const auto visual=cut?static_cast<const PresentationPose&>(frame):interpolatePresentation(drawn->frames()[0],frame,fraction,trig);
            const auto camera=observer(visual,target,view);const auto seconds=displayed*gameplayTickSeconds;const auto alpha=visibility.alphaAt(seconds);
            GhostCarStyle style;style.opacity*=alpha;style.fadeNear=0;style.fadeFar=1;
            const auto baseline=normal->renderView(cut?1:fraction,camera);
            const auto image=drawn->renderGhostView(cut?1:fraction,camera,ghost,style);const auto stats=drawn->ghostStatistics();++counts.queries;
            counts.hidden+=alpha==0;counts.fading+=alpha>0&&alpha<1;counts.shown+=alpha==1;
            counts.visibleQueries+=stats.visiblePixels>0&&stats.effectiveOpacity>0;
            counts.equalPhaseCrossingShown+=a.region==GhostBridgeRegion::crossing&&a.phase!=update.phase&&matches&&alpha==1;
            counts.missing+=stats.trackPixelsWithoutDepth;
            if(alpha==0)require(image==baseline,"Suppressed ghost changed world pixels");
            require(drawn->renderView(cut?1:fraction,camera)==baseline,"Ghost changed the live world or future image");
            if(view==NativeView::Widescreen&&fraction==0.5){
                counts.reappeared+=tick>recording.exit+2&&alpha==1;
                if(tick%24==0)retained.push_back({displayed,seconds,camera,ghost,alpha,pixelsHash(image)});
                const bool excerpt=tick+10>=recording.approach&&tick<=recording.exit+20;
                if(preview&&excerpt){
                    drawn->renderGhostView(cut?1:fraction,camera,ghost,style);
                    const auto name=std::to_string(10000+imageIndex).substr(1);std::filesystem::create_directories(output/"frames");
                    drawn->saveBitmap(output/"frames"/(name+".bmp"));
                    const auto& shownFrame=cut||fraction==1?frame:drawn->frames()[0];
                    timeline<<imageIndex<<','<<displayed<<','<<seconds<<','<<regionName(a.region)<<','<<(a.bridge==shape(shownFrame,offset))<<','<<alpha<<','<<visibility.conditionsDifferAt(seconds)
                        <<','<<ghost.position.x<<','<<ghost.position.y<<','<<ghost.position.z<<','<<unsigned(shownFrame.movingTrack->bridgePhase)<<','<<a.phase<<'\n';++imageIndex;
                }
            }
        }
        controls<<tick<<",1\n";replay::traceRow(trace,native->ram,tick,1,0,unsigned(native->outerAction),native->restarts);
    }
    require(!counts.missing,"Moving-world painted-depth coverage has a gap");
    require(counts.visibleQueries>0,"Bridge proof never rendered visible ghost pixels");
    normal->quitLive();drawn->quitLive();auto a=std::make_unique<replay::Ram>(),b=std::make_unique<replay::Ram>();
    normal->copyLiveExit(*a);drawn->copyLiveExit(*b);require(*a==*b,"Bridge ghost changed simulation, bridge evolution or RNG");
    controls.close();trace.close();
    auto replayed=std::make_unique<RaceScene>();replayed->load(output/"seed.bin",payload,output/"controls.csv",output/"native.csv",output/"retained-native.csv");
    for(auto it=retained.rbegin();it!=retained.rend();++it){GhostCarStyle style;style.opacity*=visibility.alphaAt(it->seconds);style.fadeNear=0;style.fadeFar=1;
        require(visibility.alphaAt(it->seconds)==it->alpha,"Reverse/paused query changed fade timeline");
        const auto first=replayed->renderGhostView(it->position,it->camera,it->ghost,style);
        require(pixelsHash(first)==it->hash,"Retained view differs from live bridge snapshot");
        require(replayed->renderGhostView(it->position,it->camera,it->ghost,style)==first,"Paused bridge query changed image");++counts.seekChecks;
    }
    return counts;
}
void print(std::ostream& out,const Counts& c){out<<"{\"queries\":"<<c.queries<<",\"hidden\":"<<c.hidden<<",\"fading\":"<<c.fading<<",\"shown\":"<<c.shown<<",\"mismatching_geometry_ticks\":"<<c.mismatches
    <<",\"same_shape_different_phase_ticks\":"<<c.sameShapeDifferentPhase<<",\"occupancy_holds\":"<<c.held<<",\"released_updates\":"<<c.released
    <<",\"missing_depth_pixels\":"<<c.missing<<",\"shown_after_landing\":"<<c.reappeared<<",\"reverse_pause_checks\":"<<c.seekChecks
    <<",\"visible_ghost_queries\":"<<c.visibleQueries<<",\"equal_phase_crossing_shown_queries\":"<<c.equalPhaseCrossingShown<<",\"simulation_and_rng_identical\":true}";}
}
int main(int argc,char** argv)try{
    if(argc<3||argc>4)throw std::runtime_error("Usage: native_ghost_bridge_proof assets output [--preview]");
    const bool preview=argc==4&&std::string(argv[3])=="--preview";require(argc==3||preview,"Unknown proof option");
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
    auto reference=fixture(argv[1],8,true);const auto data=scr::state::drawBridge::readData(*reference.memory);
    const auto offset=scr::state::drawBridge::geometryDataOffset(data.geometryOffset);
    write(output/"source-payload.bin",reference.payload.data(),reference.payload.size());
    const auto physics=std::make_unique<PhysicsData>(scr::state::physics::readData(*reference.memory));
    const auto recording=record(reference,output/"recording",offset);
    const auto matching=scenario(reference,recording,physics->trig,offset,output/"matching",output/"source-payload.bin",preview);
    require(!matching.mismatches&&!matching.hidden&&!matching.fading,"Identical bridge replay was suppressed");
    auto different=fixture(argv[1],0,false);
    const auto mismatch=scenario(different,recording,physics->trig,offset,output/"mismatch",output/"source-payload.bin",preview);
    require(mismatch.hidden&&mismatch.fading&&mismatch.reappeared,"Mismatching bridge did not hide and reappear after landing");
    auto equal=fixture(argv[1],23,true);
    const auto equalPhase=scenario(equal,recording,physics->trig,offset,output/"same-shape",output/"source-payload.bin",preview);
    require(equalPhase.sameShapeDifferentPhase&&equalPhase.equalPhaseCrossingShown,"Equal geometry with different phase was not shown through the crossing");
    std::ofstream report(output/"results.json");
    report<<"{\"status\":\"passed\",\"source_mode\":\"native league fixture\",\"synthetic_initial_placement\":true,\"midrace_state_injection\":false,\"completed_lap_claim\":false,\"capture_input\":false,\"cpu_emulation\":false"
        <<",\"recorded_updates\":"<<recording.samples.size()<<",\"first_approach_tick\":"<<recording.approach<<",\"first_crossing_tick\":"<<recording.entry<<",\"safe_landing_exit_tick\":"<<recording.exit
        <<",\"recorded_opponent_only_hold_ticks\":"<<recording.opponentHolds<<",\"compared_memory_bytes_per_case\":524288,\"matching\":";print(report,matching);
    report<<",\"mismatch_practice_vs_league\":";print(report,mismatch);report<<",\"same_shape_different_phase\":";print(report,equalPhase);
    report<<",\"limitations\":[\"Synthetic native approach and opponent starting placement; not an earned complete lap\",\"Strict geometry equality and two-update fade; production tolerance remains separate\",\"Observer camera and excerpted previews are development views\"]}\n";
    require(bool(report),"Cannot write bridge proof report");std::cout<<"Draw Bridge ghost proof passed: "<<(output/"results.json").string()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
