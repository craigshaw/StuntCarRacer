#pragma once
#include "replay_render_assets.h"
#include "software_renderer.h"
#include <filesystem>
#include <string>
#include <vector>

namespace scr::viewer {
struct ReplaySceneFrame {
    RenderProjectionCamera camera{};
    CockpitFrame cockpit{};
    unsigned tick{},controls{},restarts{},action{};
    std::uint16_t forwardSpeed{},cameraYaw{};
    std::uint8_t lapCounter{},raceComplete{},boostFuel{},damage{},minutes{},seconds{},subseconds{};
};
class ReplayScene {
public:
    void load(const std::filesystem::path& chip,const std::filesystem::path& payload,
        const std::filesystem::path& inputs,const std::filesystem::path& referenceTrace,
        const std::filesystem::path& nativeTrace);
    const std::vector<std::uint32_t>& render(unsigned frame,bool wireframe=false);
    void saveBitmap(const std::filesystem::path&) const;
    const std::vector<ReplaySceneFrame>& frames() const noexcept {return frames_;}
    const TrackMesh& mesh() const noexcept {return mesh_;}
    std::size_t sideFaceCount() const noexcept {return sides_.size();}
    std::size_t mountainShapeCount() const noexcept {return mountains_.shapes.size();}
    std::size_t mountainPlacementCount() const noexcept {return mountains_.placements.size();}
    const RenderStatistics& statistics() const noexcept {return statistics_;}
    std::size_t backgroundCompressedBytes() const noexcept {return assets_.backgroundCompressedBytes;}
private:
    ReplayRenderAssets assets_;
    TrackMesh mesh_;
    std::vector<TrackSideFace> sides_;
    MountainHorizonData mountains_;
    RenderProjectionTables projection_{};
    std::vector<ReplaySceneFrame> frames_;
    SoftwareReplayRenderer renderer_;
    RenderStatistics statistics_{};
    std::vector<std::uint32_t> pixels_;
    unsigned cachedFrame_=~0u;
    bool cachedWireframe_{};
};
} // namespace scr::viewer
