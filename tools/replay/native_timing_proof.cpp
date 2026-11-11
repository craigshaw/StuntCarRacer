// Development-only crossing investigation. Never linked into the player.
#include "timing_crossing_probe.h"
#include "assets/native_startup.h"
#include "race/native_race_initialization.h"
#include "state/replay_io.h"
#include "track/track_initialization.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

namespace {
using namespace scr;
using namespace scr::runtime;
namespace replay=scr::state::replay;
namespace probe=scr::timing;
using scr::state::memory::readLong;
using scr::state::memory::readWord;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Stats {
    unsigned geometries{},synthetic{},nativeTicks{},recoveries{},recoveryObservations{},duplicateTimestamps{},crossings{},reliable{},compared{},capturedCrossings{};
    std::int64_t syntheticProgressError{},observedProgressError{};
    std::map<std::string,unsigned> reasons;
};
probe::Observation observation(const replay::NativeReplay& state,std::int64_t us,bool recovery){
    probe::Observation o;o.us=us;o.recovery=recovery;o.found=state.trackFound;o.restarts=state.restarts;
    o.segment=state.ram[0x1bb1c];o.progress=readWord(state.ram,0x1bb0a);o.offTrack=state.ram[0x1bb9c];
    for(unsigned axis=0;axis<3;++axis)o.position[axis]=readLong(state.ram,0x1bcd8+4*axis);return o;
}
void advance(replay::NativeReplay& state,unsigned controls,bool quit=false){
    state.begin(controls);state.ram[0xead6+0x45]=quit?0xb3:0;
    state.controlsStage();state.physicsStage();state.engineStage();state.progressStage();
    state.groundStage();state.wheelsStage();state.bridgeStage();state.speedometerStage();
    state.damageStage();state.timingStage();state.distanceStage();state.outerStage();
}
void synthetic(const NativeMemory& seed,const probe::Gate& g,unsigned track,bool super,Stats& stats,std::ostream& out){
    const auto data=scr::state::trackProgress::readData(seed);
    const auto initial=scr::state::trackProgress::readState(seed);
    const auto inputs=scr::state::trackProgress::readInputs(seed);
    for(auto distance:{std::int64_t(64),std::int64_t(128),std::int64_t(4096),std::int64_t(65536),std::int64_t(900000),probe::cellSize/2}){
        for(unsigned fraction=1;fraction<64;++fraction)for(unsigned yaw=0;yaw<4;++yaw){
            auto a=initial,b=initial;auto in=inputs;in.yaw=std::uint16_t(yaw*0x4000);
            const auto before=distance*fraction/64,after=distance-before;
            a.worldPosition[g.axis]=std::uint32_t(g.coordinate-g.direction*before);
            b.worldPosition[g.axis]=std::uint32_t(g.coordinate+g.direction*after);
            check(updateTrackProgress(data,in,a)&&updateTrackProgress(data,in,b),"Synthetic crossing left native track lookup");
            check(a.playerSegment==g.previous&&b.playerSegment==g.lap,"Synthetic crossing did not straddle native segments");
            probe::Observation from,to;from.position=a.worldPosition;to.position=b.worldPosition;
            from.segment=a.playerSegment;to.segment=b.playerSegment;from.progress=a.progression;to.progress=b.progression;
            from.offTrack=a.offTrackFlags;to.offTrack=b.offTrackFlags;to.us=probe::tickUs;
            RaceTimingInputs timing;timing.segmentIndex[0]=std::uint8_t(to.segment);timing.trackProgress[0]=std::uint16_t(to.progress);
            timing.lapLineSegment=std::uint8_t(g.lap);timing.offTrackStateFlags=std::uint8_t(to.offTrack);timing.maximumLaps=4;
            RaceTimingState clock;clock.lapCounter[0]=1;
            const auto accepted=bool(updateRaceTiming(timing,clock).humanLapCrossing);
            const auto measured=probe::world(g,from,to,accepted);
            const auto old=scr::tools::timing::progress(g,from,to);
            check(measured.reason==probe::Reason::reliable&&old.has_value(),"Synthetic native-accepted crossing was not measurable");
            const auto d0=probe::signedDistance(g,from),d1=probe::signedDistance(g,to),span=d1-d0;
            check(std::abs(measured.offsetUs*span-probe::tickUs*(-d0))<=span/2,"World estimator differs from exact rational intersection");
            check(std::abs((std::int64_t(from.progress)-2048)*4096-d0)<4096&&std::abs(std::int64_t(to.progress)*4096-d1)<4096,
                "World gate does not match native progress quantisation");
            const auto error=std::abs(*old-measured.offsetUs);
            stats.syntheticProgressError=std::max(stats.syntheticProgressError,error);++stats.synthetic;
            if(yaw==0&&(fraction==1||fraction==32||fraction==63))
                out<<"synthetic,"<<track<<','<<super<<','<<g.lap<<','<<g.axis<<','<<g.direction<<','<<distance<<','<<fraction<<','<<measured.offsetUs<<','<<*old<<','<<error<<'\n';
        }
    }
    // Reject geometry changes instead of silently applying the cardinal method.
    auto changed=data;changed.track.properties[g.lap]|=0x10;
    check(!probe::gate(changed,g.lap).valid,"Reversed geometry was admitted without validation");
    changed=data;changed.segmentCoordinates[g.previous]=changed.segmentCoordinates[g.lap];
    check(!probe::gate(changed,g.lap).valid,"Nonadjacent cells were admitted as a gate");
}
void drive(const NativeMemory& seed,const std::vector<replay::ReplayInput>& controls,const std::string& label,
           bool captured,Stats& stats,std::ostream& observations,std::ostream& crossings){
    auto state=std::make_unique<replay::NativeReplay>(seed),baseline=std::make_unique<replay::NativeReplay>(seed);
    const auto gate=probe::gate(state->progressData,seed[0x1ca32]);check(gate.valid,"Unsupported live timing gate");
    std::optional<probe::Observation> previous,current;
    unsigned tick=0,outerRestarts=0,normalWheels=0,recoveryWheels=0,timingCalls=0,normalCalls=0;
    std::optional<std::int64_t> recoveryTime;
    state->presentation=[&](auto stage,const auto& ram,const auto& effects){
        if(stage==replay::ReplayPresentationStage::wheels){
            const bool recovery=state->restarts!=outerRestarts;
            const auto now=observation(*state,(std::int64_t(tick)+(recovery?1:0))*probe::tickUs,recovery);
            previous=current;current=now;
            if(recovery){
                ++recoveryWheels;++stats.recoveryObservations;
                if(recoveryTime)check(*recoveryTime==now.us,"Recovery observations acquired extra movement time");
                recoveryTime=now.us;
            }else {++normalWheels;if(recoveryTime){check(*recoveryTime==now.us,"Next normal observation changed recovery boundary");recoveryTime.reset();}}
            if(previous&&previous->us==now.us)++stats.duplicateTimestamps;
            observations<<label<<','<<tick<<','<<now.us<<','<<recovery<<','<<now.restarts<<','<<now.segment<<','<<now.progress<<','<<now.found<<','<<now.offTrack;
            for(auto position:now.position)observations<<','<<position;observations<<'\n';
        }
        if(stage!=replay::ReplayPresentationStage::timing)return;
        ++timingCalls;if(state->restarts==outerRestarts)++normalCalls;
        check(current.has_value(),"Timing ran before a movement observation");
        const auto now=observation(*state,current->us,current->recovery);
        check(now.position==current->position&&now.progress==current->progress&&now.segment==current->segment,
            "Position or progress changed between wheels and timing stages");
        if(!effects.humanLapCrossing)return;
        ++stats.crossings;if(captured)++stats.capturedCrossings;
        const auto measured=probe::world(gate,previous,*current,true);
        const auto old=scr::tools::timing::progress(gate,previous,*current);
        ++stats.reasons[probe::name(measured.reason)];
        crossings<<label<<','<<tick<<','<<current->us<<','<<unsigned(effects.humanLapCrossing->counter)<<','<<probe::name(measured.reason)<<',';
        if(measured.reason==probe::Reason::reliable){
            ++stats.reliable;crossings<<measured.offsetUs;
            if(old){++stats.compared;stats.observedProgressError=std::max(stats.observedProgressError,std::abs(*old-measured.offsetUs));}
        }
        crossings<<',';if(old)crossings<<*old;
        crossings<<',';if(previous)crossings<<probe::signedDistance(gate,*previous);
        crossings<<','<<probe::signedDistance(gate,*current)<<'\n';
        // Reading the event snapshot does not substitute the later cleared clock.
        check(ram[0x1bb20]==effects.humanLapCrossing->counter,"Crossing snapshot disagrees with native counter");
        if(captured)check(measured.reason==probe::Reason::reliable,"Authored replay lost a reliable crossing");
    };
    for(const auto input:controls){
        outerRestarts=state->restarts;advance(*state,input.controls,input.quit);advance(*baseline,input.controls,input.quit);
        check(state->ram==baseline->ram&&state->restarts==baseline->restarts&&state->trackFound==baseline->trackFound&&
              state->outerAction==baseline->outerAction,"Crossing observer changed native state or future evolution");
        ++tick;++stats.nativeTicks;
    }
    check(normalWheels==tick&&normalCalls==tick,"Ordinary stage observations did not match outer ticks");
    check(recoveryWheels==2*state->restarts&&timingCalls==tick+recoveryWheels,"Recovery stage callbacks were not two per restart");
    stats.recoveries+=state->restarts;
    if(captured)check(state->ram[0x1bb20]==4&&state->restarts==0,"Authored fixture no longer completes three laps without recovery");
}
}
int main(int argc,char** argv)try{
    if(argc!=3&&argc!=5)throw std::runtime_error("Usage: native_timing_proof pack output [chip controls]");
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
    std::ofstream geometry(output/"geometry.csv");
    std::ofstream observations(output/"observations.csv"),crossings(output/"crossings.csv"),sweep(output/"synthetic.csv");
    check(bool(geometry)&&bool(observations)&&bool(crossings)&&bool(sweep),"Cannot create timing evidence files");
    geometry<<"track,super,lap_segment,previous_segment,cell,previous_cell,axis,direction,coordinate,transverse_minimum,type,steering,slope,reverse,length\n";
    observations<<"run,tick,movement_us,recovery,restarts,segment,progress,track_found,off_track,x,y,z\n";
    crossings<<"run,tick,observed_us,counter,confidence,world_offset_us,progress_offset_us,previous_signed_distance,current_signed_distance\n";
    sweep<<"kind,track,super,lap_segment,axis,direction,displacement,fraction_over_64,world_offset_us,progress_offset_us,difference_us\n";
    Stats stats;
    for(bool super:{false,true})for(unsigned track=0;track<8;++track){
        auto game=startNativeGame(argv[1]);(*game.memory)[0x1c9d0]=super?11:0;
        initializeNativeTrack(*game.memory,track);
        if(scr::state::drawBridge::animate(*game.memory).geometryUpdated)rebuildNativeTrackGeometryCache(*game.memory);
        initializeNativeRace(*game.memory);
        const auto data=scr::state::trackProgress::readData(*game.memory);
        const auto lap=(*game.memory)[0x1ca32];const auto g=probe::gate(data,lap);check(g.valid,"Native track has an unsupported timing gate");
        const auto a=loadTrackSegmentConfiguration(data.track,lap);
        geometry<<track<<','<<super<<','<<unsigned(lap)<<','<<g.previous<<','<<unsigned(data.segmentCoordinates[lap])<<','<<unsigned(data.segmentCoordinates[g.previous])
            <<','<<g.axis<<','<<g.direction<<','<<g.coordinate<<','<<g.transverseMinimum<<','<<unsigned(a.geometryType)<<','<<unsigned(a.steeringFlags)<<','<<unsigned(a.slopeFlags)<<','<<unsigned(a.reverseDirection)<<','<<(unsigned(a.mode)<<8)<<'\n';
        ++stats.geometries;const auto saved=std::make_unique<NativeMemory>(*game.memory);
        synthetic(*game.memory,g,track,super,stats,sweep);check(*game.memory==*saved,"Synthetic geometry evaluation mutated source state");
        drive(*game.memory,std::vector<replay::ReplayInput>(512,{1,false}),"native-"+std::to_string(track)+(super?"-super":"-normal"),false,stats,observations,crossings);
    }
    if(argc==5){const auto seed=replay::loadCapture(argv[3]);drive(*seed,replay::loadInputs(argv[4]),"authored-three-lap",true,stats,observations,crossings);check(stats.capturedCrossings==4,"Authored fixture omitted its start or finish boundaries");}
    check(stats.recoveries&&stats.recoveryObservations&&stats.duplicateTimestamps,"Native drive omitted recovery/duplicate timestamp audit");
    for(auto* stream:{&geometry,&observations,&crossings,&sweep}){stream->flush();check(bool(*stream),"Cannot finish timing evidence CSV");}
    std::ofstream report(output/"results.json");
    report<<"{\n  \"status\": \"passed\",\n  \"method\": \"fixed-point-world-plane-v1\",\n  \"geometry_contexts\": "<<stats.geometries
        <<",\n  \"synthetic_native_brackets\": "<<stats.synthetic<<",\n  \"synthetic_max_progress_difference_us\": "<<stats.syntheticProgressError
        <<",\n  \"native_ticks\": "<<stats.nativeTicks<<",\n  \"recoveries\": "<<stats.recoveries<<",\n  \"recovery_observations\": "<<stats.recoveryObservations
        <<",\n  \"duplicate_timestamps\": "<<stats.duplicateTimestamps<<",\n  \"accepted_crossings\": "<<stats.crossings<<",\n  \"reliable_crossings\": "<<stats.reliable
        <<",\n  \"compared_crossings\": "<<stats.compared<<",\n  \"observed_max_progress_difference_us\": "<<stats.observedProgressError
        <<",\n  \"captured_crossings\": "<<stats.capturedCrossings<<",\n  \"simulation_memory_identical\": true,\n  \"oracle_cpu_executed\": false,\n  \"confidence_counts\": {";
    bool comma=false;for(const auto& reason:stats.reasons){if(comma)report<<',';comma=true;report<<'\n'<<"    \""<<reason.first<<"\": "<<reason.second;}
    report<<"\n  }\n}\n";report.close();check(bool(report),"Cannot write timing results");
    std::cout<<"{\"status\":\"passed\",\"method\":\"fixed-point-world-plane-v1\",\"native_ticks\":"<<stats.nativeTicks<<",\"synthetic_brackets\":"<<stats.synthetic<<"}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
