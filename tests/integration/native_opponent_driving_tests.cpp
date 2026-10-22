#include "native_startup.h"
#include "native_race_initialization.h"
#include "native_menu_flow.h"
#include "replay_scene.h"
#include "opponent_car_fixture.h"
#include "driving_replay_io.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>

namespace {
namespace replay=scr::test::replay;
using namespace scr::viewer;
void require(bool condition,const std::string& text){if(!condition)throw std::runtime_error(text);}
void write(const std::filesystem::path& path,const std::uint8_t* data,std::size_t size){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(data),std::streamsize(size));require(bool(out),"Cannot write "+path.string());}
std::string readText(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);require(bool(file),"Cannot read "+path.string());return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
std::uint64_t hash(const std::vector<std::uint32_t>& pixels){std::uint64_t value=14695981039346656037ULL;for(auto pixel:pixels){value^=pixel;value*=1099511628211ULL;}return value;}
std::filesystem::path liveTrace(const ReplayScene& scene){auto path=scene.liveRecording().string();return path.substr(0,path.size()-std::string("-controls.csv").size())+"-native.csv";}
NativeStartupData freshSeasonRace(const std::filesystem::path& pack,unsigned division,unsigned parity){
    auto startup=startNativeGame(pack);auto& m=*startup.memory;initializeNativeMenuSession(m);
    m[0x1c9ce]=std::uint8_t(division);m[0x1ca22]=0x80;
    // Authored league placement makes every division reproducible without a
    // captured season or promotion cheats during the simulated race.
    const unsigned target=m[0x5ab42+division]+2;
    unsigned local=0;while(local<12&&m[0x1c9c2+local]!=11)++local;require(local<12,"Missing local league driver");std::swap(m[0x1c9c2+target],m[0x1c9c2+local]);
    initializeNativeSeason(m);require(seekNativeSeasonRace(m),"Season did not select a human race");require(m[0x1ca35]==2,"Authored third driver did not start in round two");
    if(parity){advanceNativeSeasonRound(m);require(seekNativeSeasonRace(m),"Season did not select its odd track");}
    require((m[0x1ca35]&1)==parity,"Season track parity differs");m[0x57c68]=0x80;
    initializeNativePlayerConfiguration(m);scr::initializeNativeTrack(m,m[0x1ca33]);if(scr::test::drawBridge::animate(m).geometryUpdated)scr::rebuildNativeTrackGeometryCache(m);initializeNativeRace(m);
    require(m[0x1ca22]&0x80,"Competitive race flag lost");require(!m[0x57c3c],"Competitive scenario entered network mode");return startup;
}
struct Counts {unsigned ticks{},visible{},pixels{},polygons{},moved{},airborne{},patterns{},contacts{},opponentLap{},restarts{},renderQueries{},seekQueries{},outcome{},track{};bool terminal{};};
Counts runScenario(const NativeStartupData& startup,const std::filesystem::path& folder,const std::filesystem::path& payload,bool idle,const std::vector<unsigned>* contactPlan=nullptr){
    std::filesystem::create_directories(folder);const auto& seed=*startup.memory;write(folder/"seed.bin",seed.data(),seed.size());
    std::ofstream description(folder/"scenario.txt");description<<"Fresh native season. Local driver11 is synthetically placed in the selected division's third slot before original season initialization.\nNo captured state and no opponent state injection during driving.\n"<<(contactPlan?"171 ticks: accelerate and boost, steer left on ticks110 through133, then continue straight.\n":idle?"Idle controls until the original opponent completes the race.\n":"512 original-rate digital-control ticks, or an earlier original terminal transition.\n");description.close();
    auto state=std::make_unique<replay::NativeReplay>(seed);ReplayScene scene;scene.loadNativeState(seed,startup.payload,folder/"recordings");
    std::ofstream controls(folder/"controls.csv"),trace(folder/"native.csv"),observations(folder/"opponent.csv");controls<<"tick,controls,quit\n";replay::traceHeader(trace);observations<<"tick,player_lateral,opponent_lateral,distance,player_height,opponent_height,wheel_movement,start_timer,start_complete,ai_enabled\n";
    Counts counts;counts.track=seed[0x1ca33];std::set<unsigned> positions;std::vector<unsigned> samples;std::vector<std::uint64_t> exact,midpoints;
    std::unique_ptr<ReplaySceneFrame> frozen;std::array<std::uint16_t,4> frozenX{},frozenZ{},frozenFootprint{};
    for(unsigned tick=0;tick<(contactPlan?unsigned(contactPlan->size()):idle?6000U:512U);++tick){
        unsigned mask=idle?0:1;
        if(!idle&&tick>=256){if((tick%96)>=32&&(tick%96)<40)mask|=4;if((tick%96)>=64&&(tick%96)<72)mask|=8;if(tick%64<8)mask|=16;}
        if(contactPlan)mask=contactPlan->at(tick);
        controls<<tick<<','<<mask<<",0\n";const auto previous=scr_test::readWord(state->ram,0x1bb0c);const auto previousSegment=state->ram[0x1bb1d];
        state->begin(mask);state->controlsStage();state->physicsStage();state->engineStage();state->progressStage();
        observations<<tick<<','<<unsigned(state->ram[0x1bba1])<<','<<unsigned(state->ram[0x1bbed])<<','<<scr_test::readWord(state->ram,0x1bc38)<<','<<scr_test::readWord(state->ram,0x1bcd0)<<','<<scr_test::readWord(state->ram,0x1bd66)<<','<<unsigned(state->ram[0x1bb7e])<<','<<unsigned(state->ram[0x1bbdf])<<','<<unsigned(state->ram[0x1bbc4])<<','<<unsigned(state->ram[0x1bb44])<<'\n';
        const auto expected=scr::test::opponentCar::readInputs(state->ram.data());const auto world=std::array<std::uint32_t,3>{{scr_test::readLong(state->ram,0x1bcd8),scr_test::readLong(state->ram,0x1bcdc),scr_test::readLong(state->ram,0x1bce0)}};
        counts.moved+=previous!=scr_test::readWord(state->ram,0x1bb0c)||previousSegment!=state->ram[0x1bb1d];counts.airborne+=state->ram[0x1bb44]==0;counts.patterns+=state->ram[0x1bbc0]!=0;counts.contacts+=state->ram[0x1bb46]!=0;positions.insert(state->ram[0x1bb1d]);
        state->groundStage();state->wheelsStage();state->bridgeStage();state->damageStage();state->timingStage();state->outerStage();
        replay::traceRow(trace,state->ram,tick,mask,0,unsigned(state->outerAction),state->restarts);
        require(scene.stepLive(mask),"Scene stopped before independent native race");const auto& frame=scene.frames().back();require(frame.opponentEnabled,"Competitive frame omitted opponent");require(frame.pose.worldPosition==world,"Scene player physics differs");
        require(frame.opponent.relativeX==expected.relativeX&&frame.opponent.relativeZ==expected.relativeZ&&frame.opponent.footprint==expected.footprint&&frame.opponent.bounds==expected.bounds,"Scene opponent snapshot differs from native geometry");
        if(!frozen){frozen=std::make_unique<ReplaySceneFrame>(frame);frozenX=frame.opponent.relativeX;frozenZ=frame.opponent.relativeZ;frozenFootprint=frame.opponent.footprint;}
        require(frozen->opponent.relativeX==frozenX&&frozen->opponent.relativeZ==frozenZ&&frozen->opponent.footprint==frozenFootprint,"Retained opponent snapshot mutated");
        if(tick%8==0||scene.liveTerminal()){
            samples.push_back(tick);const auto signature=frame.opponent.relativeX;
            scene.renderLive(0);const auto midpoint=hash(scene.renderLive(0.5));const auto image=hash(scene.renderLive(1));midpoints.push_back(midpoint);exact.push_back(image);counts.renderQueries+=3;
            const auto& statistics=scene.statistics();counts.visible+=statistics.opponentPixels>0;counts.pixels+=statistics.opponentPixels;counts.polygons+=statistics.opponentPolygons;
            require(scene.liveTicks()==tick+1&&frame.opponent.relativeX==signature,"Interpolation changed verified simulation");
        }
        ++counts.ticks;counts.opponentLap=state->ram[0x1bb21];counts.restarts=state->restarts;counts.outcome=state->ram[0x1bbb4];
        const bool terminal=state->outerAction==scr::RaceTransitionAction::raceEnd||state->outerAction==scr::RaceTransitionAction::quit;
        require(scene.liveTerminal()==terminal,"Scene and independent race ended on different ticks");if(terminal){counts.terminal=true;break;}
    }
    controls.close();trace.close();observations.close();require(readText(folder/"native.csv")==readText(liveTrace(scene)),"Scene trace differs from independent native race");require(counts.moved>(contactPlan?30U:100U)&&positions.size()>2,"Original opponent did not move through track");if(!contactPlan)require(counts.visible>0,"Opponent was not visibly rendered in race");if(contactPlan)require(counts.contacts>0,"Authored controls did not produce a natural car collision");
    if(idle){require(counts.terminal,"Idle opponent race failed to finish within6000ticks");require(counts.opponentLap==4&&state->ram[0x1bbaf]==4,"Opponent did not naturally complete three laps");require(state->ram[0x1bb20]<4&&(counts.outcome&0x80),"Idle player did not lose the completed race");}
    // Retained replay and reverse seeking must rebuild presentation from each
    // tick's immutable opponent/bridge snapshot, preserving the verified trace.
    ReplayScene retained;retained.load(folder/"seed.bin",payload,folder/"controls.csv",folder/"native.csv",folder/"scene.csv");
    for(unsigned i=0;i<samples.size();++i){const auto tick=samples[i];require(hash(retained.render(tick))==exact[i],"Retained exact opponent frame differs from live frame");++counts.seekQueries;
        if(tick&&scr::viewer::canInterpolateReplay(retained.frames()[tick-1].restarts,retained.frames()[tick].restarts,retained.frames()[tick-1].action,retained.frames()[tick].action)){
            require(hash(retained.renderInterpolated(tick-0.5))==midpoints[i],"Retained opponent interpolation differs from live frame");++counts.seekQueries;
        }
    }
    for(unsigned i=unsigned(samples.size());i>0;--i){const auto sample=i-1,tick=samples[sample];const auto interpolated=hash(retained.renderInterpolated(tick+0.5));require(hash(retained.render(tick))==exact[sample],"Reverse seek changed opponent frame");require(hash(retained.renderInterpolated(tick+0.5))==interpolated,"Repeated seek changed opponent interpolation");counts.seekQueries+=3;
        if(tick+1<retained.frames().size()&&scr::viewer::canInterpolateReplay(retained.frames()[tick].restarts,retained.frames()[tick+1].restarts,retained.frames()[tick].action,retained.frames()[tick+1].action)){
            // Live query tick+1 is sampled only every eighth tick, so compare
            // endpoints above and use deterministic repeated midpoint queries.
            require(!retained.renderInterpolated(tick+0.5).empty(),"Missing interpolated opponent image");
        }
    }
    require(readText(folder/"native.csv")==readText(folder/"scene.csv"),"Retained opponent replay changed physics");return counts;
}
void print(const Counts& c){std::cout<<"{\"track\":"<<c.track<<",\"ticks\":"<<c.ticks<<",\"visible_frames\":"<<c.visible<<",\"opponent_pixels\":"<<c.pixels<<",\"opponent_polygons\":"<<c.polygons<<",\"moving_ticks\":"<<c.moved<<",\"airborne_ticks\":"<<c.airborne<<",\"pattern_ticks\":"<<c.patterns<<",\"contact_ticks\":"<<c.contacts<<",\"opponent_lap_counter\":"<<c.opponentLap<<",\"restarts\":"<<c.restarts<<",\"render_queries\":"<<c.renderQueries<<",\"seek_queries\":"<<c.seekQueries<<",\"outcome\":"<<c.outcome<<",\"terminal\":"<<(c.terminal?"true":"false")<<'}';}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("native_opponent_driving_tests pack output-directory");const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
    std::array<Counts,8> tracks{};std::set<unsigned> visited;unsigned total=0;
    for(unsigned division=0;division<4;++division)for(unsigned parity=0;parity<2;++parity){auto startup=freshSeasonRace(argv[1],division,parity);const auto id=(*startup.memory)[0x1ca33];visited.insert(id);const auto payload=output/"source-payload.bin";write(payload,startup.payload.data(),startup.payload.size());tracks[id]=runScenario(startup,output/("track-"+std::to_string(id)),payload,false);total+=tracks[id].ticks;}
    require(visited.size()==8,"Competitive division routes did not cover eight tracks");auto startup=freshSeasonRace(argv[1],0,0);const auto idle=runScenario(startup,output/"idle-opponent-finish",output/"source-payload.bin",true);total+=idle.ticks;
    std::vector<unsigned> contactPlan(171,17);for(unsigned tick=110;tick<134;++tick)contactPlan[tick]|=4;auto contactStartup=freshSeasonRace(argv[1],0,0);const auto contact=runScenario(contactStartup,output/"natural-car-contact",output/"source-payload.bin",false,&contactPlan);total+=contact.ticks;
    std::cout<<"{\"status\":\"passed\",\"tracks\":[";for(unsigned i=0;i<8;++i){if(i)std::cout<<',';print(tracks[i]);}std::cout<<"],\"idle_loss\":";print(idle);std::cout<<",\"natural_contact\":";print(contact);std::cout<<",\"ticks\":"<<total<<",\"synthetic_division_placement\":true,\"midrace_opponent_state_injection\":false,\"capture_input\":false,\"cpu_emulation\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
