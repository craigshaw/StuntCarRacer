#pragma once
#include "render/render_projection.h"
#include "track/track_progress.h"
#include <array>
#include <cstdint>

namespace scr {
struct NearTrackVertex {std::uint16_t height{0x8000};int x{},y{};};
struct NearTrackCutEdge {bool valid{};std::uint8_t fromIndex{},toIndex{};};
struct NearTrackGeometryState {
    TrackProgressState progress;
    RenderProjectionCamera camera;
    // Original byte indices0,2,...0x76 become indices0,1,...59. Flags
    // belong to cross-sections at byte indices0,4,...0x74.
    std::array<NearTrackVertex,60> upper{},lower{};
    std::array<std::uint8_t,30> flags{};
    std::array<std::uint8_t,2> processedIndices{{0x80,0x80}};
    std::array<std::uint16_t,2> savedHeights{},transformedCoordinates{};
    std::uint8_t startIndex{},renderingLoopIndex{},viewReverse{},continuation{},edgeVisibilityMode{};
    std::uint8_t displayUpdate{},gridSweepDirection{},lapApproachSegment{},preview{},segmentHalfFlags{};
    std::uint8_t heightDifference{},trackOffsetBase{},processingIndex{},lastProcessed{},reverseDecision{};
    std::uint16_t perspectiveDepth{};
    int topPadding{};
    int bottomPadding{};
    int horizontalPadding{}; // Private presentation bounds; never stored in simulation RAM.
};
inline bool nearTrackInsideY(int y,int padding,int bottomPadding=0){return std::uint16_t(y+padding)<128+padding+bottomPadding;}
inline bool nearTrackInsideX(int x,int padding){return std::uint16_t(x+padding)<256+2*padding;}
// Original0x6521c: configure one segment, decode its dense heights, project
// upper vertices and admit lower edges. State carries the original adjacent
// segment coordinates and processed-side history. No runtime CPU dependency.
void projectNearTrackSegment(const TrackProgressData&,NearTrackGeometryState&);
// Queue visibility may request a ground-level side point which the initial
// silhouette test rejected. byteIndex is the upper vertex's original index.
void materializeNearTrackLower(const TrackProgressData&,NearTrackGeometryState&,std::uint8_t byteIndex);
// Original0x65564 endpoint carry; returns the unsigned comparison X<256.
bool shiftNearTrackCoordinates(NearTrackGeometryState&);
// Original processRenderData: extend the interpolated first cut points beyond
// the viewport. Entries map to command offsets32,36,40,44 (left/right upper,
// left/right lower); valid false means the original invalid-line sentinel.
std::array<NearTrackCutEdge,4> prepareNearTrackCutEdges(const TrackProgressData&,NearTrackGeometryState&);
} // namespace scr
