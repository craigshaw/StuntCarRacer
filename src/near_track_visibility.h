#pragma once
#include "near_track_geometry.h"
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
    std::array<NearTrackQueueEntry,48> entries{};
    std::array<bool,100> coveredSegments{};
    std::uint16_t offset{32},opponentOffset{0xffff};
    std::uint16_t lineBytes{};
    std::uint8_t opponentSegment{},opponentSubSegment{},previousOffset{};
    std::uint16_t minimumBoundaryDistance{};
};
// Exact clipping metadata needed by queue admission and viewport closure.
// Edge pixels and scanline coverage remain the native renderer's concern.
NearTrackEdge makeNearTrackEdge(NearTrackPoint from,NearTrackPoint to);
void seedNearTrackCutEdges(const NearTrackGeometryState&,const std::array<NearTrackCutEdge,4>&,NearTrackQueueState&);
// segmentRenderFlags is the generated cache header's high byte, not steering
// flags. The callback materializes an upper-indexed ground vertex at raw512.
void processNearTrackVisibility(NearTrackGeometryState&,std::uint8_t segmentRenderFlags,
    NearTrackQueueState&,const std::function<void(std::uint8_t)>& materializeLower);
// Original processTrackSegments for first=true; generateTrackEdgeLines after
// each coordinate carry otherwise. Mutates original flags and depth counters.
void appendNearTrackSegment(NearTrackGeometryState&,NearTrackQueueState&,bool first);

enum class NearTrackCommandKind : std::uint8_t {fill,line};
enum class NearTrackSurface : std::uint8_t {rightSide,leftSide,road};
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
// Ordered original near painter operations. Fill boundaries may merge many
// strips; viewport boundary edges close the initial cut. No runtime CPU state.
std::vector<NearTrackDrawCommand> drawNearTrackQueue(const NearTrackQueueState&,const NearTrackDrawInputs&);
} // namespace scr
