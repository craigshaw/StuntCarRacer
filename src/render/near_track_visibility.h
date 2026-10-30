#pragma once
#include "render/near_track_geometry.h"
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace scr {
struct NearTrackPoint {int x{},y{};};
struct NearTrackEdge {
    NearTrackPoint from{},to{},clippedFrom{},clippedTo{};
    bool present{};
    std::uint8_t clipFlags{}; // original packed pointer high byte
    std::uint16_t originQueueOffset{};
    std::uint8_t originEdge{};
    std::uint16_t storageBytes{}; // original line buffer use, excluding raster work
};
struct NearTrackQueueEntry {
    // Original offsets0/4/8/12/16/20/24: upperR/L,lowerR/L,
    // current verticalR/L,current road cross-edge.
    std::array<NearTrackEdge,7> edges{};
    std::uint8_t segment{},edgeColor{},processedFlags{},steeringFlags{};
};
struct NearTrackQueueState {
    std::vector<NearTrackQueueEntry> entries=std::vector<NearTrackQueueEntry>(48);
    std::array<bool,100> coveredSegments{};
    std::uint16_t offset{32},opponentOffset{0xffff};
    std::uint16_t lineBytes{};
    std::uint8_t opponentSegment{},opponentSubSegment{},previousOffset{};
    std::uint16_t minimumBoundaryDistance{};
    int horizontalPadding{},topPadding{},bottomPadding{};
};
// Expanded views own native storage rather than the original byte-indexed
// buffers. Keep the original limits exactly when no viewport padding is used.
constexpr bool expandedTrackView(int padding,int top,int bottom){return padding||top||bottom;}
constexpr unsigned trackPresentationLineLimit(int padding,int top,int bottom){
    // Scanline storage grows with the exposed area. Leave headroom below the
    // word counter's maximum for the final admitted line and horizon.
    const auto scaled=std::uint64_t(0x2328)*unsigned(256+2*padding)*unsigned(128+top+bottom)/(256*128);
    return scaled>60000?60000:unsigned(scaled);
}
inline unsigned trackPresentationQueueLimit(const NearTrackQueueState& q){
    return expandedTrackView(q.horizontalPadding,q.topPadding,q.bottomPadding)?unsigned((q.entries.size()-1)*32):0x5e0;
}
// Exact clipping metadata needed by queue admission and viewport closure.
// Edge pixels and scanline coverage remain the native renderer's concern.
NearTrackEdge makeNearTrackEdge(NearTrackPoint from,NearTrackPoint to,int horizontalPadding=0,int topPadding=0,int bottomPadding=0);
void seedNearTrackCutEdges(const NearTrackGeometryState&,const std::array<NearTrackCutEdge,4>&,NearTrackQueueState&);
// segmentRenderFlags is the generated cache header's high byte, not steering
// flags. The callback materializes an upper-indexed ground vertex at raw512.
void processNearTrackVisibility(NearTrackGeometryState&,std::uint8_t segmentRenderFlags,
    NearTrackQueueState&,const std::function<void(std::uint8_t)>& materializeLower);
// Original processTrackSegments for first=true; generateTrackEdgeLines after
// each coordinate carry otherwise. Mutates original flags and depth counters.
void appendNearTrackSegment(NearTrackGeometryState&,NearTrackQueueState&,bool first);

enum class NearTrackCommandKind : std::uint8_t {fill,line};
enum class NearTrackSurface : std::uint8_t {rightSide,leftSide,road,opponent};
struct NearTrackDrawCommand {
    NearTrackCommandKind kind{};
    NearTrackSurface surface{};
    std::uint8_t color{};
    std::uint16_t originQueueOffset{},firstQueueOffset{};
    std::vector<NearTrackEdge> edges;
};
struct NearTrackDrawInputs {
    std::uint16_t minimumQueueOffset{};
    std::uint8_t renderingOrderMode{},raceStartTimer{},offRoadSideFlags{};
    std::uint16_t offsetFromRoadCenter{};
    std::uint8_t sideEdgeColor{},alternateSideColor{};
    bool previewColorOverride{};
};
enum class NearTrackPainterSlot : std::uint8_t {ahead,behind,occluded};
using NearTrackPainter=std::function<void(NearTrackPainterSlot,std::uint16_t,
    std::vector<NearTrackDrawCommand>&)>;
// Ordered original near painter operations. Fill boundaries may merge many
// strips; viewport boundary edges close the initial cut. No runtime CPU state.
std::vector<NearTrackDrawCommand> drawNearTrackQueue(const NearTrackQueueState&,const NearTrackDrawInputs&,
    const NearTrackPainter& painter={},std::uint16_t* lineBytes=nullptr);
} // namespace scr
