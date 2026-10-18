#include "road_suspension_step.h"

namespace scr {

void updateRoadSuspensionStep(const RoadSuspensionInputs& input,
                              const TrackGeometryData& geometry,
                              const TrigTable& trig,
                              const SuspensionAttenuationTable& attenuation,
                              RoadSuspensionState& state) {
    updateTransformMatrices(input.angles,trig,state.matrices);
    state.wheelPositions=calculateWheelCoordinates(state.matrices.wheelInputs());
    auto trackInput=input.track;
    trackInput.wheels=state.wheelPositions;
    trackInput.pitchHighByte=static_cast<std::uint8_t>(input.angles.pitch>>8);
    state.samples=sampleTrackWheelHeights(geometry,trackInput,state.track);
    state.expectedHeights=calculateExpectedWheelHeights(
        input.angles.pitch,input.angles.roll,input.worldHeight,trig);
    auto responseInput=input.response;
    responseInput.wheelHeight=state.track.heights;
    responseInput.expectedHeight={std::uint32_t(state.expectedHeights.frontLeft),
                                  std::uint32_t(state.expectedHeights.frontRight),
                                  std::uint32_t(state.expectedHeights.rearCenter)};
    responseInput.pitchAngle=input.angles.pitch;
    updateSuspensionResponse(responseInput,attenuation,state.response);
}

} // namespace scr
