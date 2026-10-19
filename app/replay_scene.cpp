#include "replay_scene.h"
#include "track_mesh_capture.h"
#include "../tests/driving_replay_io.h"
#include "../tests/near_track_geometry_fixture.h"
#include <algorithm>
#include <cmath>
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
} // namespace
void ReplayScene::load(const std::filesystem::path& chip,const std::filesystem::path& payload,
    const std::filesystem::path& inputs,const std::filesystem::path& referenceTrace,
    const std::filesystem::path& nativeTrace) {
    namespace replay=scr::test::replay;namespace a=scr::test::audio;
    auto capture=replay::loadCapture(chip.string().c_str());const auto original=readFile(payload);
    assets_=decodeReplayRenderAssets(original.data(),original.size(),capture->data(),capture->size());
    const auto meshData=decodeTrackMesh(*capture);mesh_=buildTrackMesh(meshData);
    segmentRenderFlags_.fill(0);std::array<bool,100> knownFlags{};
    for(const auto& section:meshData.sections)if(!knownFlags[section.segment]){
        segmentRenderFlags_[section.segment]=section.flags;knownFlags[section.segment]=true;
    }
    sides_=buildTrackSides(mesh_,{(*capture)[0x691b3],(*capture)[0x691b2],false});
    mountains_=decodeMountainHorizonData(original.data(),original.size());
    auto state=std::make_unique<replay::NativeReplay>(*capture);
    auto nearGeometry=scr::test::nearGeometry::readState(*capture);
    presentationTrack_=state->progressData;trig_=state->physicsData.trig;
    projection_.angleTable=state->progressData.angleTable;projection_.magnitudeTable=state->progressData.magnitudeTable;
    const auto input=replay::loadInputs(inputs.string().c_str());
    std::ifstream reference(referenceTrace);if(!reference)throw std::runtime_error("Verified original trace is required");
    std::string line;std::getline(reference,line);const auto expectedHeader=columns(line);
    std::ostringstream header;replay::traceHeader(header);
    const auto nativeHeader=columns(header.str().substr(0,header.str().size()-1));
    if(expectedHeader!=nativeHeader)throw std::runtime_error("Original trace schema does not match native replay");
    std::ofstream trace(nativeTrace);if(!trace)throw std::runtime_error("Cannot write native viewer trace");replay::traceHeader(trace);
    frames_.clear();frames_.reserve(input.size());
    cachedFrame_=~0u;
    for(unsigned tick=0;tick<input.size();++tick){
        state->begin(input[tick].controls);state->ram[0xead6+0x45]=input[tick].quit?0xb3:0;
        state->controlsStage();state->physicsStage();state->engineStage();state->progressStage();
        // Rendering starts here in the original frame, before collision visual
        // effects, suspension sprites, timing and outer race transitions.
        RenderCameraInputs camera{};
        for(unsigned n=0;n<3;++n)camera.worldPosition[n]=a::longword(state->ram.data(),0x1bcd8+4*n);
        camera.pitch=a::word(state->ram.data(),0x1bce4);camera.yaw=a::word(state->ram.data(),0x1bce6);camera.roll=a::word(state->ram.data(),0x1bce8);
        camera.averageWheelVelocity=a::word(state->ram.data(),0x1bd38);
        auto renderCamera=makeRenderProjectionCamera(camera,state->physicsData.trig);
        renderCamera.viewportCenterY=a::word(state->ram.data(),0x1bc42);
        renderCamera.sinRoll=a::word(state->ram.data(),0x1c250);renderCamera.cosRoll=a::word(state->ram.data(),0x1c252);
        NearTrackFrameInputs nearInputs;
        nearInputs.progress=scr::test::trackProgress::readState(state->ram);nearInputs.camera=renderCamera;
        nearInputs.displayUpdate=state->ram[0x5d724];nearInputs.opponentSegment=state->ram[0x1bba6];nearInputs.opponentSubSegment=state->ram[0x1bc24];
        nearInputs.minimumBoundaryDistance=a::word(state->ram.data(),0x63ede);
        nearInputs.drawing.minimumQueueOffset=a::word(state->ram.data(),0x1bc1e);
        nearInputs.drawing.renderingOrderMode=state->ram[0x1bb9a];nearInputs.drawing.raceStartTimer=state->ram[0x1bbdf];
        nearInputs.drawing.offRoadSideFlags=nearInputs.progress.offRoadSide;nearInputs.drawing.offsetFromRoadCenter=nearInputs.progress.lateralOffset;
        nearInputs.drawing.sideEdgeColor=(*capture)[0x691b3];nearInputs.drawing.alternateSideColor=(*capture)[0x691b2];
        const auto nearSeed=nearGeometry;
        const auto progressInputs=scr::test::trackProgress::readInputs(state->ram);
        auto nearFrame=buildNearTrackFrame(state->progressData,segmentRenderFlags_,nearInputs,nearGeometry);
        state->groundStage();state->wheelsStage();state->damageStage();state->timingStage();state->outerStage();
        std::ostringstream row;replay::traceRow(row,state->ram,tick,input[tick].controls,0,unsigned(state->outerAction),state->restarts,input[tick].quit);
        const auto values=columns(row.str().substr(0,row.str().size()-1));
        if(!std::getline(reference,line))throw std::runtime_error("Original trace ends before native replay");
        const auto expected=columns(line);
        if(expected.size()!=values.size())throw std::runtime_error("Malformed original trace row");
        for(unsigned n=0;n<values.size();++n)if(nativeHeader[n]!="original_instructions"&&values[n]!=expected[n])
            throw std::runtime_error("Viewer replay differs from original at tick "+std::to_string(tick)+", field "+nativeHeader[n]);
        trace<<row.str();
        ReplaySceneFrame frame;frame.tick=tick;frame.controls=input[tick].controls;frame.restarts=state->restarts;frame.action=unsigned(state->outerAction);
        frame.cameraYaw=camera.yaw;frame.camera=renderCamera;frame.nearTrack=std::move(nearFrame.presentation);
        frame.pose=camera;frame.nearInputs=nearInputs;frame.nearSeed=nearSeed;frame.progressInputs=progressInputs;
        frame.cockpit=captureCockpitFrame(state->ram.data(),state->ram.size());
        frame.forwardSpeed=a::word(state->ram.data(),0x1bd30);frame.lapCounter=state->ram[0x1bb20];frame.raceComplete=state->ram[0x1bbaf];
        frame.boostFuel=state->ram[0x1ca20];frame.damage=state->ram[0x1bb55];
        frame.minutes=state->ram[0x1c908];frame.seconds=state->ram[0x1c920];frame.subseconds=state->ram[0x1c938];frames_.push_back(std::move(frame));
        if((state->outerAction==RaceTransitionAction::raceEnd||state->outerAction==RaceTransitionAction::quit)&&tick+1<input.size())
            throw std::runtime_error("Replay controls continue after the race exit");
    }
    if(std::getline(reference,line))throw std::runtime_error("Original trace contains extra rows");
    if(frames_.empty())throw std::runtime_error("No native replay frames");
}
const std::vector<std::uint32_t>& ReplayScene::render(unsigned index,bool wireframe) {
    if(index==cachedFrame_&&cachedFraction_==0&&wireframe==cachedWireframe_)return pixels_;
    const auto& frame=frames_.at(index);draw(frame,frame.nearTrack,wireframe);
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
    // Rebuild near geometry from a private copy of the saved renderer history.
    // This query never advances gameplay or writes back to verified snapshots.
    auto inputs=from.nearInputs;auto progress=from.progressInputs;auto seed=from.nearSeed;
    inputs.progress.worldPosition=visual.pose.worldPosition;
    progress.pitch=visual.pose.pitch;progress.yaw=visual.pose.yaw;progress.averageWheelVelocity=visual.pose.averageWheelVelocity;
    (void)updateTrackProgress(presentationTrack_,progress,inputs.progress);
    inputs.progress.cameraHeight=visual.camera.heightBaseline;inputs.camera=visual.camera;
    inputs.drawing.offRoadSideFlags=inputs.progress.offRoadSide;inputs.drawing.offsetFromRoadCenter=inputs.progress.lateralOffset;
    const auto nearFrame=buildNearTrackFrame(presentationTrack_,segmentRenderFlags_,inputs,seed);
    draw(visual,nearFrame.presentation,wireframe);
    cachedFrame_=index;cachedFraction_=fraction;cachedWireframe_=wireframe;
    return pixels_;
}
void ReplayScene::draw(const ReplayVisualState& frame,const NearTrackPresentation& nearTrack,bool wireframe){
    makeReplayBackground(assets_,pixels_);
    const auto mountains=projectMountainHorizon(mountains_,frame.pose.yaw,frame.camera);
    statistics_=renderer_.drawFrame(mesh_,sides_,mountains,frame.camera,projection_,assets_.palette,pixels_,wireframe,&nearTrack);
    composeReplayCockpit(assets_,frame.cockpit,pixels_);
    // The original speedometer uses this word multiply/shift before drawing
    // its two-row span. This changes presentation pixels only.
    int speed=int(frame.forwardSpeed)-0x1100;if(speed<0)speed=0;
    unsigned bar=unsigned((std::uint32_t(speed)*0xb700u)>>16)>>7;if(bar>=128)bar-=128;bar=std::min(127u,bar);
    for(unsigned y=174;y<176;++y)for(unsigned x=0;x<128;++x)pixels_[y*320+96+x]=assets_.palette[x<=bar?3:0];
}
void ReplayScene::saveBitmap(const std::filesystem::path& path) const {
    if(pixels_.size()!=320*200)throw std::runtime_error("Render a frame before exporting");
    std::ofstream out(path,std::ios::binary);if(!out)throw std::runtime_error("Cannot create rendered bitmap");
    constexpr std::uint32_t size=54+320*200*4;out.put('B');out.put('M');littleLong(out,size);littleLong(out,0);littleLong(out,54);
    littleLong(out,40);littleLong(out,320);littleLong(out,std::uint32_t(-200));littleWord(out,1);littleWord(out,32);littleLong(out,0);
    littleLong(out,320*200*4);littleLong(out,2835);littleLong(out,2835);littleLong(out,0);littleLong(out,0);
    for(auto pixel:pixels_)littleLong(out,pixel);if(!out)throw std::runtime_error("Rendered bitmap write failed");
}
} // namespace scr::viewer
