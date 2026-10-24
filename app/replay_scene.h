#pragma once
#include "replay_render_assets.h"
#include "replay_interpolation.h"
#include "live_drive.h"
#include "replay_view.h"
#include "software_renderer.h"
#include "game_audio.h"
#include "opponent_car.h"
#include "opponent_far_track.h"
#include "native_cockpit_presentation.h"
#include "native_wheel_sprites.h"
#include "native_driving_particles.h"
#include "native_cockpit_chains.h"
#include "native_race_messages.h"
#include <filesystem>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace scr::viewer {
struct ReplayTrackSnapshot {
    TrackProgressData progress;
    TrackMesh mesh;
    std::vector<TrackSideFace> sides;
    OpponentFarTrackData farTrack;
    std::uint8_t bridgePhase{},bridgeActive{};
};
struct ReplaySceneFrame : ReplayVisualState {
    GameAudioFrame audio;
    NearTrackPresentation nearTrack;
    NearTrackFrameInputs nearInputs;
    NearTrackGeometryState nearSeed;
    TrackProgressInputs progressInputs;
    OpponentCarInputs opponent;
    bool opponentEnabled{};
    CockpitInstrumentPixels instrumentPixels{};
    NativeWheelSpriteFrame wheelSprites;
    std::array<WheelAnimationState,7> wheelTimeline{};
    unsigned wheelVblanks{};
    // Moving track data belongs to the rendered tick. Retained replays and
    // interpolation must never read the simulation's later bridge position.
    std::shared_ptr<const ReplayTrackSnapshot> movingTrack;
    unsigned tick{},controls{},restarts{},action{};
    std::uint16_t cameraYaw{};
    std::uint8_t lapCounter{},raceComplete{},boostFuel{},damage{},minutes{},seconds{},subseconds{};
    std::uint16_t gasOutput{};
    std::uint8_t accelerationState{},boostActive{};
    DrivingParticleDraws particleDraws;
    std::uint8_t particleFrameCounter{};
    NativeRaceMessageFrame message;
    NativeChainState chains;
    NativeChainDraws chainDraws;
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
    // A native menu session supplies its own freshly initialized race memory.
    void loadNativeState(const std::array<std::uint8_t,0x80000>& memory,
        const std::vector<std::uint8_t>& payload,const std::filesystem::path& recordingsDirectory);
    // Complete the recovered local race exit after quit or the end-message wait.
    void finishLiveExit(std::array<std::uint8_t,0x80000>& sessionMemory);
    // Observation/copy boundaries for native persistence; no live mutation.
    void copyLiveExit(std::array<std::uint8_t,0x80000>& sessionMemory)const;
    void copyLiveRecords(std::array<std::uint8_t,32>& entries)const;
    void resetLive();
    bool stepLive(unsigned controls,bool quit=false,DriveControlPreset preset=DriveControlPreset::Original);
    unsigned verifyLiveReplay(const std::filesystem::path& inputs,const std::filesystem::path& reference,
        const std::filesystem::path& nativeTrace);
    const std::vector<std::uint32_t>& renderLive(double fraction,bool wireframe=false,bool interpolate=true);
    const std::vector<std::uint32_t>& renderLiveMessage(unsigned index,unsigned mode);
    std::vector<std::uint8_t> captureLiveIndices(bool endMessage=false);
    unsigned liveTicks() const noexcept;
    bool liveTerminal() const noexcept;
    const std::filesystem::path& liveRecording() const noexcept;
    GameAudioSeed audioSeed()const;
    const std::vector<std::uint32_t>& render(unsigned frame,bool wireframe=false);
    const std::vector<std::uint32_t>& renderInterpolated(double position,bool wireframe=false);
    // Independent presentation query. Default settings retain the original
    // view; alternate angles/cockpit visibility leave saved frames unchanged.
    const std::vector<std::uint32_t>& renderView(double position,const ReplayViewSettings&,bool wireframe=false);
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
    ReplaySceneFrame advanceSimulation(Simulation&,unsigned controls,bool quit,DriveControlPreset preset=DriveControlPreset::Original);
    ReplayRenderAssets assets_;
    NativeWheelSprites wheelAssets_;
    NativeDrivingParticleData particleData_;
    TrackMesh mesh_;
    OpponentFarTrackData far_;
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
    double lastLiveFraction_{};
    bool lastLiveInterpolate_{true};
    bool bitmapOnly_{};
    NearTrackPresentation buildViewCommands(const ReplayVisualState&,const ReplaySceneFrame&,
        const std::optional<OpponentCarInputs>&)const;
    void draw(const ReplayVisualState&,const NearTrackPresentation&,bool wireframe,
        const ReplayTrackSnapshot* movingTrack=nullptr,const ReplaySceneFrame* discrete=nullptr,double wheelFraction=0,
        bool showCockpit=true);
};
} // namespace scr::viewer
