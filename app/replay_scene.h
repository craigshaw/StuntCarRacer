#pragma once
#include "replay_render_assets.h"
#include "replay_interpolation.h"
#include "software_renderer.h"
#include "game_audio.h"
#include <filesystem>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace scr::viewer {
struct ReplaySceneFrame : ReplayVisualState {
    GameAudioFrame audio;
    NearTrackPresentation nearTrack;
    NearTrackFrameInputs nearInputs;
    NearTrackGeometryState nearSeed;
    TrackProgressInputs progressInputs;
    unsigned tick{},controls{},restarts{},action{};
    std::uint16_t cameraYaw{};
    std::uint8_t lapCounter{},raceComplete{},boostFuel{},damage{},minutes{},seconds{},subseconds{};
};
class ReplayScene {
public:
    ReplayScene();
    ~ReplayScene();
    void load(const std::filesystem::path& chip,const std::filesystem::path& payload,
        const std::filesystem::path& inputs,const std::filesystem::path& referenceTrace,
        const std::filesystem::path& nativeTrace);
    void loadLive(const std::filesystem::path& chip,const std::filesystem::path& payload,
        const std::filesystem::path& recordingsDirectory);
    void loadNative(const std::filesystem::path& pack,const std::filesystem::path& recordingsDirectory,unsigned trackId=0);
    void resetLive();
    bool stepLive(unsigned controls,bool quit=false);
    unsigned verifyLiveReplay(const std::filesystem::path& inputs,const std::filesystem::path& reference,
        const std::filesystem::path& nativeTrace);
    const std::vector<std::uint32_t>& renderLive(double fraction,bool wireframe=false,bool interpolate=true);
    unsigned liveTicks() const noexcept;
    bool liveTerminal() const noexcept;
    const std::filesystem::path& liveRecording() const noexcept;
    GameAudioSeed audioSeed()const;
    const std::vector<std::uint32_t>& render(unsigned frame,bool wireframe=false);
    const std::vector<std::uint32_t>& renderInterpolated(double position,bool wireframe=false);
    void saveBitmap(const std::filesystem::path&) const;
    const std::vector<ReplaySceneFrame>& frames() const noexcept {return frames_;}
    const TrackMesh& mesh() const noexcept {return mesh_;}
    std::size_t sideFaceCount() const noexcept {return sides_.size();}
    std::size_t mountainShapeCount() const noexcept {return mountains_.shapes.size();}
    std::size_t mountainPlacementCount() const noexcept {return mountains_.placements.size();}
    const RenderStatistics& statistics() const noexcept {return statistics_;}
    std::size_t backgroundCompressedBytes() const noexcept {return assets_.backgroundCompressedBytes;}
private:
    struct Simulation;
    std::unique_ptr<Simulation> simulation_;
    std::filesystem::path chipPath_,payloadPath_,recordingsDirectory_;
    std::filesystem::path nativePackPath_;
    unsigned nativeTrack_{};
    void initialise(const std::filesystem::path& chip,const std::filesystem::path& payload);
    void initialiseMemory(const std::array<std::uint8_t,0x80000>&,const std::vector<std::uint8_t>&);
    void beginLiveRecording(const std::filesystem::path&);
    ReplaySceneFrame captureFrame(Simulation&,unsigned tick,unsigned controls);
    void finishFrame(Simulation&,ReplaySceneFrame&);
    ReplaySceneFrame advanceSimulation(Simulation&,unsigned controls,bool quit);
    ReplayRenderAssets assets_;
    TrackMesh mesh_;
    std::vector<TrackSideFace> sides_;
    MountainHorizonData mountains_;
    RenderProjectionTables projection_{};
    TrackProgressData presentationTrack_;
    TrigTable trig_{};
    std::array<std::uint8_t,100> segmentRenderFlags_{};
    std::vector<ReplaySceneFrame> frames_;
    SoftwareReplayRenderer renderer_;
    RenderStatistics statistics_{};
    std::vector<std::uint32_t> pixels_;
    unsigned cachedFrame_=~0u;
    double cachedFraction_{};
    bool cachedWireframe_{};
    void draw(const ReplayVisualState&,const NearTrackPresentation&,bool wireframe);
};
} // namespace scr::viewer
