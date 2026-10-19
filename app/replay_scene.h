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
    std::uint16_t forwardSpeed{};
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
    const RenderStatistics& statistics() const noexcept {return statistics_;}
    std::size_t backgroundCompressedBytes() const noexcept {return assets_.backgroundCompressedBytes;}
private:
    ReplayRenderAssets assets_;
    TrackMesh mesh_;
    RenderProjectionTables projection_{};
    std::vector<ReplaySceneFrame> frames_;
    SoftwareReplayRenderer renderer_;
    RenderStatistics statistics_{};
    std::vector<std::uint32_t> pixels_;
};
} // namespace scr::viewer
