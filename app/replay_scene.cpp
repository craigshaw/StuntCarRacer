#include "replay_scene.h"
#include "native_multiplayer_state.h"
#include "replay_scene_commands.h"
#include "state/track_mesh_adapter.h"
#include "live_drive.h"
#include "native_startup.h"
#include "native_menu_render.h"
#include "state/replay_io.h"
#include "state/near_track_geometry_adapter.h"
#include "state/opponent_car_adapter.h"
#include "state/wheel_animation_adapter.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace scr::viewer {
namespace {
std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("Cannot read replay input: "+path.string());
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
std::vector<std::string> columns(std::string line) {
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    std::vector<std::string> values;std::istringstream stream(line);std::string value;
    while(std::getline(stream,value,','))values.push_back(value);return values;
}
void littleWord(std::ostream& out,std::uint16_t value){out.put(char(value));out.put(char(value>>8));}
void littleLong(std::ostream& out,std::uint32_t value){littleWord(out,std::uint16_t(value));littleWord(out,std::uint16_t(value>>16));}
int signedCarWord(std::uint16_t value){return value<32768?int(value):int(value)-65536;}
std::pair<int,int> rotateCarPoint(int x,int z,unsigned quarter){
    switch(quarter&3){case 1:return {-z,x};case 2:return {-x,-z};case 3:return {z,-x};default:return {x,z};}
}
OpponentCarInputs interpolateOpponent(const OpponentCarInputs& from,const OpponentCarInputs& to,
    const RenderProjectionCamera& camera,double fraction){
    auto result=from;result.camera=camera;
    const auto mix=[&](std::uint16_t a,std::uint16_t b){return std::uint16_t(int(a)+int(std::lround(signedCarWord(std::uint16_t(b-a))*fraction)));};
    for(unsigned n=0;n<4;++n){
        const auto a=rotateCarPoint(signedCarWord(from.relativeX[n]),signedCarWord(from.relativeZ[n]),4-(from.camera.quarterTurn>>14));
        const auto b=rotateCarPoint(signedCarWord(to.relativeX[n]),signedCarWord(to.relativeZ[n]),4-(to.camera.quarterTurn>>14));
        const auto p=rotateCarPoint(a.first+int(std::lround((b.first-a.first)*fraction)),a.second+int(std::lround((b.second-a.second)*fraction)),camera.quarterTurn>>14);
        result.relativeX[n]=std::uint16_t(p.first);result.relativeZ[n]=std::uint16_t(p.second);
        result.footprint[n]=mix(from.footprint[n],to.footprint[n]);
    }
    for(unsigned n=0;n<3;++n)result.bounds[n]=mix(from.bounds[n],to.bounds[n]);
    result.distance=mix(from.distance,to.distance);result.roadOffset=mix(from.roadOffset,to.roadOffset);
    return result;
}
} // namespace
struct ReplayScene::Simulation {
    scr::state::replay::Ram initial;
    scr::state::replay::NativeReplay native;
    NearTrackGeometryState geometry;
    NativeCockpitPresentation instruments;
    NativeRaceMessageFrame message;
    std::uint8_t flameFrame{};
    NativeChainState chains;
    NativeChainDraws chainDraws;
    unsigned chainRestarts{};
    WheelAnimationState wheels;
    std::array<WheelAnimationState,7> wheelTimeline{};
    unsigned wheelVblanks{};
    unsigned ticks{};
    std::ofstream controls,trace;
    std::filesystem::path recording;
    std::string lastRow;
    explicit Simulation(const scr::state::replay::Ram& capture):initial(capture),native(capture),geometry(scr::state::nearGeometry::readState(capture)),instruments(capture),message(captureNativeRaceMessage(capture)),flameFrame(capture[0x1bb69]),wheels(scr::state::wheelAnimation::readState(capture.data())){}
};
ReplayScene::ReplayScene()=default;
ReplayScene::~ReplayScene()=default;
void ReplayScene::initialise(const std::filesystem::path& chip,const std::filesystem::path& payload){
    auto capture=scr::state::replay::loadCapture(chip.string().c_str());const auto original=readFile(payload);
    chipPath_=chip;payloadPath_=payload;nativePackPath_.clear();initialiseMemory(*capture,original);
}
void ReplayScene::initialiseMemory(const std::array<std::uint8_t,0x80000>& memory,const std::vector<std::uint8_t>& original){
    assets_=decodeReplayRenderAssets(original.data(),original.size(),memory.data(),memory.size());
    wheelAssets_=decodeNativeWheelSprites(memory.data(),memory.size());
    particleData_=decodeNativeDrivingParticleData(memory);
    const auto meshData=decodeTrackMesh(memory);mesh_=buildTrackMesh(meshData);
    far_.mesh=meshData;std::copy_n(memory.begin()+0x65e70,100,far_.projectionLimits.begin());
    segmentRenderFlags_.fill(0);std::array<bool,100> knownFlags{};
    for(const auto& section:meshData.sections)if(!knownFlags[section.segment]){
        segmentRenderFlags_[section.segment]=section.flags;knownFlags[section.segment]=true;
    }
    sides_=buildTrackSides(mesh_,{memory[0x691b3],memory[0x691b2],false});
    mountains_=decodeMountainHorizonData(original.data(),original.size());
    simulation_=std::make_unique<Simulation>(memory);
    auto& simulation=*simulation_;
    simulation.chains=captureNativeChainState(memory);
    if(memory[0x1bbdf]||simulation.chains.position!=0x60){
        auto initialChains=simulation.chains;
        simulation.chainDraws=advanceNativeChains(1,initialChains,captureCockpitFrame(memory.data(),memory.size()));
    }
    simulation.native.presentation=[this,&simulation](auto stage,const auto& ram,const auto& events){
        simulation.instruments.update(stage,ram,events);
        if(stage==scr::state::replay::ReplayPresentationStage::wheels){
            if(simulation.chainRestarts!=simulation.native.restarts){
                simulation.chains=captureNativeChainState(ram);
                simulation.chainRestarts=simulation.native.restarts;
            }
            simulation.chainDraws=advanceNativeChains(ram[0x1bbdf],simulation.chains,captureCockpitFrame(ram.data(),ram.size()));
            advanceReplayFlames(ram[0x1bb62],simulation.flameFrame);
            simulation.message=captureNativeRaceMessage(ram);
        }
        if(stage==scr::state::replay::ReplayPresentationStage::wait){
            const auto inputs=scr::state::wheelAnimation::readInputs(ram.data());
            simulation.wheels.framesToWait=ram[0x616d8];simulation.wheels.framesToWaitWhenFading=ram[0x616da];
            for(unsigned n=0;n<ram[0x616d8];++n){
                (void)updateWheelVblank(wheelAssets_.animation,inputs,simulation.wheels);
                ++simulation.wheelVblanks;
                if(simulation.wheelVblanks<=6)simulation.wheelTimeline[simulation.wheelVblanks]=simulation.wheels;
            }
        }
    };
    presentationTrack_=simulation_->native.progressData;trig_=simulation_->native.physicsData.trig;
    projection_.angleTable=presentationTrack_.angleTable;projection_.magnitudeTable=presentationTrack_.magnitudeTable;
    frames_.clear();cachedFrame_=~0u;
}
ReplaySceneFrame ReplayScene::captureFrame(Simulation& simulation,unsigned tick,unsigned controls){
    namespace a=scr::state::audio;auto& state=simulation.native;auto& geometry=simulation.geometry;
    ReplaySceneFrame frame;frame.tick=tick;frame.controls=controls;
    if(state.ram[0x1ca33]==5){
        auto track=std::make_shared<ReplayTrackSnapshot>();track->progress=state.progressData;
        track->farTrack=far_;track->farTrack.mesh=decodeTrackMesh(state.ram);track->mesh=buildTrackMesh(track->farTrack.mesh);
        track->sides=buildTrackSides(track->mesh,{state.ram[0x691b3],state.ram[0x691b2],false});
        track->bridgePhase=state.ram[0x1bbb0];track->bridgeActive=state.ram[0x1bb3f];
        frame.movingTrack=std::move(track);
    }
    const auto& presentation=frame.movingTrack?frame.movingTrack->progress:presentationTrack_;
    auto& camera=frame.pose;
    for(unsigned n=0;n<3;++n)camera.worldPosition[n]=a::longword(state.ram.data(),0x1bcd8+4*n);
    camera.pitch=a::word(state.ram.data(),0x1bce4);camera.yaw=a::word(state.ram.data(),0x1bce6);camera.roll=a::word(state.ram.data(),0x1bce8);
    camera.averageWheelVelocity=a::word(state.ram.data(),0x1bd38);
    frame.cameraYaw=camera.yaw;frame.camera=makeRenderProjectionCamera(camera,trig_);
    frame.camera.viewportCenterY=a::word(state.ram.data(),0x1bc42);
    frame.camera.sinRoll=a::word(state.ram.data(),0x1c250);frame.camera.cosRoll=a::word(state.ram.data(),0x1c252);
    auto& nearInputs=frame.nearInputs;
    nearInputs.progress=scr::state::trackProgress::readState(state.ram);nearInputs.camera=frame.camera;
    // Original off-grid progress retains the near-render index which the
    // previous frame already cleared at0x65002. The native gameplay adapter
    // keeps renderer writes separate, so restore that presentation value here.
    if(!state.trackFound)nearInputs.progress.renderIndex=0;
    nearInputs.displayUpdate=state.ram[0x5d724];nearInputs.opponentSegment=state.ram[0x1bba6];nearInputs.opponentSubSegment=state.ram[0x1bc24];
    nearInputs.minimumBoundaryDistance=a::word(state.ram.data(),0x63ede);
    nearInputs.drawing.minimumQueueOffset=a::word(state.ram.data(),0x1bc1e);
    nearInputs.drawing.renderingOrderMode=state.ram[0x1bb9a];nearInputs.drawing.raceStartTimer=state.ram[0x1bbdf];
    nearInputs.drawing.offRoadSideFlags=nearInputs.progress.offRoadSide;nearInputs.drawing.offsetFromRoadCenter=nearInputs.progress.lateralOffset;
    nearInputs.drawing.sideEdgeColor=simulation.initial[0x691b3];nearInputs.drawing.alternateSideColor=simulation.initial[0x691b2];
    frame.nearSeed=geometry;frame.progressInputs=scr::state::trackProgress::readInputs(state.ram);
    auto near=buildNearTrackFrame(presentation,segmentRenderFlags_,nearInputs,geometry);
    frame.opponentEnabled=(state.ram[0x1ca22]&0x80)!=0;
    std::optional<OpponentCarInputs> opponent;
    if(frame.opponentEnabled){
        frame.opponent=scr::state::opponentCar::readInputs(state.ram.data());frame.opponent.camera=frame.camera;
        opponent=frame.opponent;
    }
    auto scene=buildReplaySceneCommands(near,nearInputs.drawing,projection_,frame.movingTrack?frame.movingTrack->farTrack:far_,
        geometry,mountains_,frame.cameraYaw,frame.camera,opponent);
    geometry=std::move(scene.geometry);frame.nearTrack=std::move(scene.presentation);
    return frame;
}
void ReplayScene::finishFrame(Simulation& simulation,ReplaySceneFrame& frame){
    namespace a=scr::state::audio;const auto& state=simulation.native;
    frame.restarts=state.restarts;frame.action=unsigned(state.outerAction);
    frame.cockpit=captureCockpitFrame(state.ram.data(),state.ram.size());
    frame.cockpit.flameFrame=simulation.flameFrame;
    frame.chains=simulation.chains;frame.chainDraws=simulation.chainDraws;
    frame.cockpit.wheelRotation=simulation.wheelTimeline[0].phase;
    frame.instrumentPixels=simulation.instruments.capture();
    frame.hundredthsInstrumentPixels=simulation.instruments.capture(true);
    frame.wheelSprites=captureNativeWheelSpriteFrame(wheelAssets_,state.ram.data(),state.ram.size());
    frame.wheelTimeline=simulation.wheelTimeline;frame.wheelVblanks=simulation.wheelVblanks;
    frame.forwardSpeed=a::word(state.ram.data(),0x1bd30);frame.lapCounter=state.ram[0x1bb20];frame.raceComplete=state.ram[0x1bbaf];
    frame.boostFuel=state.ram[0x1ca20];frame.damage=state.ram[0x1bb55];
    frame.gasOutput=a::word(state.ram.data(),0x1bd2a);frame.accelerationState=state.ram[0x1bba8];frame.boostActive=state.ram[0x1bb62];
    frame.particleDraws=state.particleDraws;frame.particleFrameCounter=state.particleFrameCounter;frame.message=simulation.message;
    frame.minutes=state.ram[0x1c908];frame.seconds=state.ram[0x1c920];frame.subseconds=state.ram[0x1c938];
    frame.audio.writes=state.audioWrites;frame.audio.restarts=state.restarts;frame.audio.action=frame.action;
    frame.audio.dmaControl=a::word(state.ram.data(),0x69ede);frame.audio.noiseFlag=state.ram[0x5b646];
    frame.audio.gameExit=state.ram[0x1bb63];
    frame.audio.forwardSpeed=a::word(state.ram.data(),0x1bd30);
    frame.audio.wheelMovement=state.ram[0x1bb7e];frame.audio.inputFlags=state.ram[0x1bb47];
}
ReplaySceneFrame ReplayScene::advanceSimulation(Simulation& simulation,unsigned controls,bool quit,DriveControlPreset preset){
    if(controls>31)throw std::invalid_argument("Live controls exceed original digital mask");
    auto& state=simulation.native;
    simulation.wheelVblanks=0;
    simulation.wheels.framesToWait=state.ram[0x616d8];simulation.wheels.framesToWaitWhenFading=state.ram[0x616da];
    simulation.wheelTimeline.fill(simulation.wheels);
    state.begin(controls);state.ram[0xead6+0x45]=quit?0xb3:0;
    // Modern's optional input policy releases the recovered acceleration
    // latch before control processing. Neutral then produces zero engine
    // force rather than braking. Original executes the untouched path.
    if(preset==DriveControlPreset::Modern&&!(controls&1))state.ram[0x1bba8]=0;
    state.controlsStage();state.physicsStage();state.engineStage();state.progressStage();
    // Original rendering boundary: before effects, timing and outer transitions.
    auto frame=captureFrame(simulation,simulation.ticks,controls);
    state.groundStage();state.wheelsStage();state.bridgeStage();state.speedometerStage();state.damageStage();state.timingStage();state.distanceStage();state.outerStage();finishFrame(simulation,frame);
    std::ostringstream row;scr::state::replay::traceRow(row,state.ram,simulation.ticks,controls,0,unsigned(state.outerAction),state.restarts,quit);
    simulation.lastRow=row.str();++simulation.ticks;return frame;
}
void ReplayScene::load(const std::filesystem::path& chip,const std::filesystem::path& payload,
    const std::filesystem::path& inputs,const std::filesystem::path& referenceTrace,const std::filesystem::path& nativeTrace){
    namespace replay=scr::state::replay;initialise(chip,payload);
    const auto input=replay::loadInputs(inputs.string().c_str());
    std::ifstream reference(referenceTrace);if(!reference)throw std::runtime_error("Verified original trace is required");
    std::string line;std::getline(reference,line);const auto expectedHeader=columns(line);
    std::ostringstream header;replay::traceHeader(header);const auto nativeHeader=columns(header.str().substr(0,header.str().size()-1));
    if(expectedHeader!=nativeHeader)throw std::runtime_error("Original trace schema does not match native replay");
    std::ofstream trace(nativeTrace);if(!trace)throw std::runtime_error("Cannot write native viewer trace");trace<<header.str();
    frames_.reserve(input.size());
    for(unsigned tick=0;tick<input.size();++tick){
        auto frame=advanceSimulation(*simulation_,input[tick].controls,input[tick].quit);
        const auto values=columns(simulation_->lastRow.substr(0,simulation_->lastRow.size()-1));
        if(!std::getline(reference,line))throw std::runtime_error("Original trace ends before native replay");const auto expected=columns(line);
        if(expected.size()!=values.size())throw std::runtime_error("Malformed original trace row");
        for(unsigned n=0;n<values.size();++n)if(nativeHeader[n]!="original_instructions"&&values[n]!=expected[n])
            throw std::runtime_error("Viewer replay differs from original at tick "+std::to_string(tick)+", field "+nativeHeader[n]);
        trace<<simulation_->lastRow;frames_.push_back(std::move(frame));
        if(liveTerminal()&&tick+1<input.size())throw std::runtime_error("Replay controls continue after the race exit");
    }
    if(std::getline(reference,line))throw std::runtime_error("Original trace contains extra rows");
    if(frames_.empty())throw std::runtime_error("No native replay frames");
}
void ReplayScene::loadLive(const std::filesystem::path& chip,const std::filesystem::path& payload,const std::filesystem::path& directory){
    initialise(chip,payload);beginLiveRecording(directory);
}
void ReplayScene::loadNative(const std::filesystem::path& pack,const std::filesystem::path& directory,unsigned trackId){
    auto startup=startNativeRace(pack,trackId);initialiseMemory(*startup.memory,startup.payload);
    nativePackPath_=pack;nativeTrack_=trackId;chipPath_.clear();payloadPath_.clear();beginLiveRecording(directory);
}
void ReplayScene::loadNativeState(const std::array<std::uint8_t,0x80000>& memory,
        const std::vector<std::uint8_t>& payload,const std::filesystem::path& directory){
    initialiseMemory(memory,payload);nativePackPath_.clear();chipPath_.clear();payloadPath_.clear();
    beginLiveRecording(directory);
}
void ReplayScene::finishLiveExit(std::array<std::uint8_t,0x80000>& sessionMemory){
    if(!liveTerminal())throw std::logic_error("Race exit requires a terminal transition");
    namespace t=scr::state::transitions;
    auto& memory=simulation_->native.ram;
    if(memory[0x5eb76]&&(memory[0x1ca22]&0x80))finishNativeMultiplayerRaceExit(memory);
    auto state=t::readState(memory.data());
    (void)finishLocalRaceExit(t::readInputs(memory.data()),state);t::writeState(memory.data(),state);
    sessionMemory=memory;
}
void ReplayScene::copyLiveExit(std::array<std::uint8_t,0x80000>& memory)const{
    if(!liveTerminal())throw std::logic_error("Checkpoint exit requires a finished race");
    memory=simulation_->native.ram;
    if(memory[0x5eb76]&&(memory[0x1ca22]&0x80))finishNativeMultiplayerRaceExit(memory);
    namespace t=scr::state::transitions;
    auto state=t::readState(memory.data());(void)finishLocalRaceExit(t::readInputs(memory.data()),state);t::writeState(memory.data(),state);
}
void ReplayScene::copyLiveRecords(std::array<std::uint8_t,32>& entries)const{
    const auto& memory=simulation_->native.ram;
    for(unsigned kind=0;kind<2;++kind){
        const unsigned name=kind?0x5ec55:0x5ec48;std::copy_n(memory.begin()+name,12,entries.begin()+kind*16);
        for(unsigned field=0;field<3;++field)entries[kind*16+12+field]=memory[0x1c916+kind+24*field];
        entries[kind*16+15]=0;
    }
}
void ReplayScene::beginLiveRecording(const std::filesystem::path& directory){
    recordingsDirectory_=directory;
    if(!directory.empty()){
    std::filesystem::create_directories(directory);
    const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto base=directory/("drive-"+std::to_string(stamp));unsigned suffix=0;
    while(std::filesystem::exists(base.string()+"-controls.csv"))base=directory/("drive-"+std::to_string(stamp)+"-"+std::to_string(++suffix));
    auto& simulation=*simulation_;simulation.recording=base.string()+"-controls.csv";
    simulation.controls.open(simulation.recording);simulation.trace.open(base.string()+"-native.csv");
    if(!simulation.controls||!simulation.trace)throw std::runtime_error("Cannot create live input recording");
    simulation.controls<<"tick,controls,quit,hold_acceleration\n";scr::state::replay::traceHeader(simulation.trace);
    }
    auto& simulation=*simulation_;
    simulation.wheelTimeline.fill(simulation.wheels);
    const auto seed=simulation.geometry;auto frame=captureFrame(simulation,0,0);simulation.geometry=seed;finishFrame(simulation,frame);
    frames_.push_back(frame);frames_.push_back(std::move(frame));
}
void ReplayScene::resetLive(){
    const auto directory=recordingsDirectory_;
    if(!nativePackPath_.empty()){const auto pack=nativePackPath_;loadNative(pack,directory,nativeTrack_);}
    else{const auto chip=chipPath_,payload=payloadPath_;loadLive(chip,payload,directory);}
}
unsigned ReplayScene::liveTicks()const noexcept{return simulation_?simulation_->ticks:0;}
bool ReplayScene::liveTerminal()const noexcept{return simulation_&&(simulation_->native.outerAction==RaceTransitionAction::raceEnd||simulation_->native.outerAction==RaceTransitionAction::quit);}
const std::filesystem::path& ReplayScene::liveRecording()const noexcept{return simulation_->recording;}
GameAudioSeed ReplayScene::audioSeed()const{
    namespace a=scr::state::audio;const auto& ram=simulation_->initial;GameAudioSeed seed;
    seed.samples.assign(ram.begin(),ram.end());seed.parameters=a::readData(ram.data());seed.playback=a::readState(ram.data());
    seed.engine={a::word(ram.data(),0x1bc34),a::word(ram.data(),0xf472)};seed.pitchDelta=a::word(ram.data(),0x1bbf4);
    seed.frame.forwardSpeed=a::word(ram.data(),0x1bd30);seed.frame.wheelMovement=ram[0x1bb7e];seed.frame.inputFlags=ram[0x1bb47];
    seed.frame.dmaControl=a::word(ram.data(),0x69ede);seed.frame.noiseFlag=ram[0x5b646];seed.frame.gameExit=ram[0x1bb63];
    return seed;
}
bool ReplayScene::stepLive(unsigned controls,bool quit,DriveControlPreset preset){
    if(liveTerminal())return false;
    auto next=advanceSimulation(*simulation_,controls,quit,preset);auto& simulation=*simulation_;
    if(!recordingsDirectory_.empty()){
    simulation.controls<<next.tick<<','<<controls<<','<<(quit?1:0)<<','<<(preset==DriveControlPreset::Modern?1:0)<<'\n';simulation.trace<<simulation.lastRow;
    simulation.controls.flush();simulation.trace.flush();
    if(!simulation.controls||!simulation.trace)throw std::runtime_error("Live input recording write failed");
    }
    frames_[0]=std::move(frames_[1]);frames_[1]=std::move(next);
    if(!canInterpolateReplay(frames_[0].restarts,frames_[1].restarts,frames_[0].action,frames_[1].action))frames_[0]=frames_[1];
    cachedFrame_=~0u;return true;
}
const std::vector<std::uint32_t>& ReplayScene::renderLive(double fraction,bool wireframe,bool interpolate,NativeView view){
    fraction=std::clamp(fraction,0.0,1.0);lastLiveFraction_=fraction;lastLiveInterpolate_=interpolate;lastLiveView_=view;
    if(view!=NativeView::Original){ReplayViewSettings settings;settings.view=view;return renderView(interpolate?fraction:1,settings,wireframe);}
    if(interpolate)return renderInterpolated(fraction,wireframe);
    const auto& frame=frames_[1];draw(frame,frame.nearTrack,wireframe,frame.movingTrack.get(),&frame,fraction);
    cachedFrame_=~0u;return pixels_;
}
const std::vector<std::uint32_t>& ReplayScene::renderLiveMessage(unsigned index,unsigned mode){
    // Repaint the final snapshot before overlaying the original panel, so a
    // pause message never becomes part of a later interpolated race image.
    cachedFrame_=~0u;renderLive(lastLiveFraction_,false,lastLiveInterpolate_,lastLiveView_);
    const auto& memory=simulation_->native.ram;
    const auto cockpit=captureCockpitFrame(memory.data(),memory.size());
    const bool wide=lastLiveView_==NativeView::Widescreen;
    if(wide)foreground_.assign(320*200,replayTransparent);
    auto& panel=wide?foreground_:pixels_;
    compositeMaskedObject(assets_,51,cockpit.objects[51],panel);
    drawNativeGameMessage(memory,panel,assets_.palette,index,mode,memory[0x1bb88]!=0);
    if(wide)compositeReplayCentred(panel,pixels_);
    cachedFrame_=~0u;return pixels_;
}
std::vector<std::uint8_t> ReplayScene::captureLiveIndices(bool endMessage,NativeView view){
    const auto saved=assets_.palette;
    const auto savedView=lastLiveView_;lastLiveView_=view;
    // Original fades use the four bitmap planes. The independent hardware
    // sprites have their own OCS palette and Sprite DMA is off for race fades.
    bitmapOnly_=true;
    try{
        // Preserve palette identity even when two original entries have the
        // same RGB. Palette fades can move those entries independently.
        for(unsigned i=0;i<assets_.palette.size();++i)assets_.palette[i]=i;
        cachedFrame_=~0u;
        ReplayViewSettings settings;settings.view=view;
        const auto& frame=endMessage?renderLiveMessage(0x3c,4):view!=NativeView::Original?renderView(1,settings):render(1);
        std::vector<std::uint8_t> indices;indices.reserve(frame.size());
        for(auto pixel:frame){
            if(pixel>=16)throw std::logic_error("Native race renderer emitted a non-palette pixel");
            indices.push_back(std::uint8_t(pixel));
        }
        assets_.palette=saved;lastLiveView_=savedView;bitmapOnly_=false;cachedFrame_=~0u;return indices;
    }catch(...){assets_.palette=saved;lastLiveView_=savedView;bitmapOnly_=false;cachedFrame_=~0u;throw;}
}
unsigned ReplayScene::verifyLiveReplay(const std::filesystem::path& inputs,const std::filesystem::path& referencePath,const std::filesystem::path& tracePath){
    namespace replay=scr::state::replay;const auto input=replay::loadInputs(inputs.string().c_str());
    std::ifstream reference(referencePath);std::ofstream trace(tracePath);
    if(!reference||!trace)throw std::runtime_error("Cannot open live verification trace");
    std::string line;std::getline(reference,line);const auto fields=columns(line);std::ostringstream header;replay::traceHeader(header);
    if(fields!=columns(header.str().substr(0,header.str().size()-1)))throw std::runtime_error("Live reference schema differs");trace<<header.str();
    FixedDriveClock clock;unsigned presentations=0;
    while(liveTicks()<input.size()){
        const auto steps=clock.advance(1.0/replayPresentationHz);
        for(unsigned n=0;n<steps&&liveTicks()<input.size();++n){
            const auto tick=liveTicks();if(!stepLive(input[tick].controls,input[tick].quit))throw std::runtime_error("Live verification stopped before its last input");
            const auto values=columns(simulation_->lastRow.substr(0,simulation_->lastRow.size()-1));
            if(!std::getline(reference,line))throw std::runtime_error("Live reference ended early");const auto expected=columns(line);
            if(values.size()!=expected.size())throw std::runtime_error("Malformed live reference row");
            for(unsigned k=0;k<fields.size();++k)if(fields[k]!="original_instructions"&&values[k]!=expected[k])throw std::runtime_error("Live simulation differs at tick "+std::to_string(tick)+", field "+fields[k]);
            trace<<simulation_->lastRow;
        }
        renderLive(clock.fraction());++presentations;
    }
    if(std::getline(reference,line))throw std::runtime_error("Live reference contains extra rows");return presentations;
}
const std::vector<std::uint32_t>& ReplayScene::render(unsigned index,bool wireframe) {
    if(index==cachedFrame_&&cachedFraction_==0&&wireframe==cachedWireframe_)return pixels_;
    const auto& frame=frames_.at(index);draw(frame,frame.nearTrack,wireframe,frame.movingTrack.get(),&frame);
    cachedFrame_=index;cachedFraction_=0;cachedWireframe_=wireframe;
    return pixels_;
}
const std::vector<std::uint32_t>& ReplayScene::renderInterpolated(double position,bool wireframe){
    if(!std::isfinite(position)||frames_.empty())throw std::invalid_argument("Invalid replay presentation position");
    position=std::clamp(position,0.0,double(frames_.size()-1));
    const auto index=unsigned(position);const double fraction=position-index;
    const auto& from=frames_[index];
    if(fraction<1e-9||index+1==frames_.size())return render(index,wireframe);
    const auto& to=frames_[index+1];
    if(!canInterpolateReplay(from.restarts,to.restarts,from.action,to.action))return render(index,wireframe);
    if(index==cachedFrame_&&fraction==cachedFraction_&&wireframe==cachedWireframe_)return pixels_;
    const auto visual=interpolateReplayVisuals(from,to,fraction,trig_);
    std::optional<OpponentCarInputs> opponent;
    if(from.opponentEnabled&&to.opponentEnabled)opponent=interpolateOpponent(from.opponent,to.opponent,visual.camera,fraction);
    const auto commands=buildViewCommands(visual,from,opponent);
    draw(visual,commands,wireframe,from.movingTrack.get(),&from,fraction,true,NativeView::Original,&to);
    cachedFrame_=index;cachedFraction_=fraction;cachedWireframe_=wireframe;
    return pixels_;
}
NearTrackPresentation ReplayScene::buildViewCommands(const ReplayVisualState& visual,const ReplaySceneFrame& from,
    const std::optional<OpponentCarInputs>& opponent,int padding,int topPadding,int bottomPadding)const{
    // All view queries rebuild from private presentation history. Track lookup
    // updates this local copy, independently of the driver's physics state.
    auto inputs=from.nearInputs;auto progress=from.progressInputs;auto seed=from.nearSeed;seed.horizontalPadding=padding;seed.topPadding=topPadding;seed.bottomPadding=bottomPadding;
    const auto& presentation=from.movingTrack?from.movingTrack->progress:presentationTrack_;
    inputs.progress.worldPosition=visual.pose.worldPosition;
    progress.pitch=visual.pose.pitch;progress.yaw=visual.pose.yaw;progress.averageWheelVelocity=visual.pose.averageWheelVelocity;
    if(!updateTrackProgress(presentation,progress,inputs.progress))inputs.progress.renderIndex=0;
    inputs.progress.cameraHeight=visual.camera.heightBaseline;inputs.camera=visual.camera;
    inputs.drawing.offRoadSideFlags=inputs.progress.offRoadSide;inputs.drawing.offsetFromRoadCenter=inputs.progress.lateralOffset;
    auto nearFrame=buildNearTrackFrame(presentation,segmentRenderFlags_,inputs,seed);
    return buildReplaySceneCommands(nearFrame,inputs.drawing,projection_,from.movingTrack?from.movingTrack->farTrack:far_,
        seed,mountains_,visual.pose.yaw,visual.camera,opponent).presentation;
}
const std::vector<std::uint32_t>& ReplayScene::renderView(double position,const ReplayViewSettings& settings,bool wireframe){
    if(settings.view==NativeView::Original&&!settings.hasCameraOffset()&&settings.showCockpit)return renderInterpolated(position,wireframe);
    if(!std::isfinite(position)||frames_.empty())throw std::invalid_argument("Invalid presentation view position");
    position=std::clamp(position,0.0,double(frames_.size()-1));const auto index=unsigned(position);
    const auto& from=frames_[index];double fraction=position-index;
    if(index+1>=frames_.size()||!canInterpolateReplay(from.restarts,frames_[index+1].restarts,from.action,frames_[index+1].action))fraction=0;
    auto visual=fraction>1e-9?interpolateReplayVisuals(from,frames_[index+1],fraction,trig_):static_cast<const ReplayVisualState&>(from);
    visual=makeReplayView(visual,settings,trig_);cachedFrame_=~0u;
    if(settings.view==NativeView::Original&&!settings.hasCameraOffset()&&fraction<1e-9){
        draw(visual,from.nearTrack,wireframe,from.movingTrack.get(),&from,0,settings.showCockpit);return pixels_;
    }
    std::optional<OpponentCarInputs> opponent;
    if(from.opponentEnabled){
        const auto& to=fraction>1e-9?frames_[index+1]:from;
        if(to.opponentEnabled)opponent=interpolateOpponent(from.opponent,to.opponent,visual.camera,fraction);
    }
    const auto commands=buildViewCommands(visual,from,opponent,viewPadding(settings.view),settings.view!=NativeView::Original?16:0,settings.view==NativeView::Widescreen?56:0);
    draw(visual,commands,wireframe,from.movingTrack.get(),&from,fraction,settings.showCockpit,settings.view,
        fraction>1e-9?&frames_[index+1]:nullptr);
    return pixels_;
}
void ReplayScene::draw(const ReplayVisualState& frame,const NearTrackPresentation& nearTrack,bool wireframe,const ReplayTrackSnapshot* movingTrack,const ReplaySceneFrame* discrete,double fraction,bool showCockpit,NativeView view,const ReplaySceneFrame* next){
    const bool wide=view==NativeView::Widescreen,extended=view!=NativeView::Original;
    if(showCockpit&&!wide)makeReplayBackground(assets_,pixels_);else pixels_.assign(viewWidth(view)*200,0);
    const auto mountains=projectMountainHorizon(mountains_,frame.pose.yaw,frame.camera,viewPadding(view));
    statistics_=renderer_.drawFrame(movingTrack?movingTrack->mesh:mesh_,movingTrack?movingTrack->sides:sides_,mountains,frame.camera,projection_,assets_.palette,pixels_,wireframe,&nearTrack,viewPadding(view),extended,wide?56:0);
    if(extended&&showCockpit){if(wide)composeReplayWidescreenFrame(assets_,pixels_);else composeReplayFrame(assets_,pixels_);}
    if(wide)foreground_.assign(320*200,replayTransparent);
    auto& foreground=wide?foreground_:pixels_;
    if(discrete){
        const auto particles=next?interpolateNativeDrivingParticles(discrete->particleDraws,next->particleDraws,fraction):discrete->particleDraws;
        composeNativeDrivingParticles(assets_,particleData_,particles,discrete->particleFrameCounter,foreground);
    }
    if(discrete&&showCockpit){
        if(next)composeInterpolatedNativeChains(assets_,discrete->chainDraws,next->chainDraws,fraction,foreground);
        else composeNativeChains(assets_,discrete->chainDraws,foreground);
    }
    if(showCockpit)composeReplayCockpit(assets_,frame.cockpit,foreground,extended);
    if(discrete&&showCockpit){
        const auto& instruments=runningLapHundredths_?discrete->hundredthsInstrumentPixels:discrete->instrumentPixels;
        for(unsigned n=0;n<instruments.size();++n)foreground[320*cockpitInstrumentTop+n]=assets_.palette[instruments[n]];
        const unsigned vblank=std::min(6u,unsigned(std::floor(fraction*6+1e-9)));
        const auto placements=next?interpolateNativeWheelSprites(discrete->wheelSprites,next->wheelSprites,discrete->cockpit,next->cockpit,fraction):discrete->wheelSprites;
        if(!bitmapOnly_)composeNativeWheelSprites(wheelAssets_,placements,discrete->wheelTimeline[vblank],foreground);
        drawNativeRaceMessage(simulation_->initial,assets_,discrete->message,foreground);
    }
    if(wide)compositeReplayCentred(foreground,pixels_);
}
void ReplayScene::saveBitmap(const std::filesystem::path& path) const {
    if(pixels_.size()!=320*200&&pixels_.size()!=replayWidescreenWidth*200)throw std::runtime_error("Render a frame before exporting");
    const auto width=std::uint32_t(pixels_.size()/200);
    std::ofstream out(path,std::ios::binary);if(!out)throw std::runtime_error("Cannot create rendered bitmap");
    const std::uint32_t size=54+width*200*4;out.put('B');out.put('M');littleLong(out,size);littleLong(out,0);littleLong(out,54);
    littleLong(out,40);littleLong(out,width);littleLong(out,std::uint32_t(-200));littleWord(out,1);littleWord(out,32);littleLong(out,0);
    littleLong(out,width*200*4);littleLong(out,2835);littleLong(out,2835);littleLong(out,0);littleLong(out,0);
    for(auto pixel:pixels_)littleLong(out,pixel);if(!out)throw std::runtime_error("Rendered bitmap write failed");
}
} // namespace scr::viewer
