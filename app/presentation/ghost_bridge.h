#pragma once
#include "render/track_mesh.h"
#include "track/track_sampling.h"
#include <array>
#include <cstdint>
#include <vector>

namespace scr::runtime {
// Recorded presentation geometry, not bridge phase or AI guidance state.
struct GhostBridgeShape {
    std::array<std::uint8_t,0x48> nearGeometry{};
    std::vector<std::uint16_t> cachedSections;
    bool operator==(const GhostBridgeShape& other)const noexcept{
        return nearGeometry==other.nearGeometry&&cachedSections==other.cachedSections;
    }
};
GhostBridgeShape captureGhostBridgeShape(const TrackGeometryData&,const TrackMeshData&,unsigned rawOffset);

enum class GhostBridgeRegion {outside,approach,crossing,landing};
// Source-timeline classification. Landing ends only after two grounded source
// updates beyond the moving sections. A recovery cut starts a new local region.
class GhostBridgeRegionTracker {
public:
    GhostBridgeRegion observe(unsigned segment,bool grounded,bool cut=false);
private:
    bool engaged_{},crossed_{};
    unsigned groundedTicks_{};
};
// observe() runs once per live gameplay observation. alphaAt() is a pure query:
// repeated, paused or reverse presentation queries cannot advance the fade.
class GhostBridgeVisibility {
public:
    void observe(double seconds,GhostBridgeRegion,bool geometryMatches,bool cut=false);
    double alphaAt(double seconds)const;
    bool conditionsDifferAt(double seconds)const;
private:
    struct Transition {double time{},from{1},to{1};bool differs{};};
    std::vector<Transition> transitions_;
    double lastTime_{-1};
    bool blocked_{};
};
} // namespace scr::runtime
