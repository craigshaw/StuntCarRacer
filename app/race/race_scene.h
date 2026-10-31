#pragma once
#include "presentation/race_render_assets.h"
#include "presentation/presentation_interpolation.h"
#include "input/drive_input.h"
#include "presentation/camera_view.h"
#include "race/lap_recording.h"
#include "render/software_renderer.h"
#include "render/ghost_car.h"
#include "audio/game_audio.h"
#include "render/opponent_car.h"
#include "render/opponent_far_track.h"
#include "presentation/native_cockpit_presentation.h"
#include "presentation/native_wheel_sprites.h"
#include "presentation/native_driving_particles.h"
#include "presentation/native_cockpit_chains.h"
#include "presentation/native_race_messages.h"
#include <filesystem>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace scr::runtime {
struct TrackPresentationSnapshot {
    TrackProgressData progress;
    TrackMesh mesh;
    std::vector<TrackSideFace> sides;
    OpponentFarTrackData farTrack;
    std::uint8_t bridgePhase{},bridgeActive{};
};
struct RaceFrame : PresentationPose {
    GameAudioFrame audio;
    NearTrackPresentation nearTrack;
    NearTrackFrameInputs nearInputs;
    NearTrackGeometryState nearSeed;
    TrackProgressInputs progressInputs;
    OpponentCarInputs opponent;
    bool opponentEnabled{};
    CockpitInstrumentPixels instrumentPixels{};
    CockpitInstrumentPixels hundredthsInstrumentPixels{};
    NativeWheelSpriteFrame wheelSprites;
    std::array<WheelAnimationState,7> wheelTimeline{};
    unsigned wheelVblanks{};
    // Moving track data belongs to the rendered tick. Retained replays and
    // interpolation must never read the simulation's later bridge position.
    std::shared_ptr<const TrackPresentationSnapshot> movingTrack;
    unsigned tick{},controls{},restarts{},action{};
    std::int64_t presentationMicroseconds{};
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
class RaceScene {
public:
    RaceScene();
    ~RaceScene();
    void load(const std::filesystem::path& chip,const std::filesystem::path& payload,
        const std::filesystem::path& inputs,const std::filesystem::path& referenceTrace,
        const std::filesystem::path& nativeTrace);
    void loadLive(const std::filesystem::path& chip,const std::filesystem::path& payload,
        const std::filesystem::path& recordingsDirectory);
    void loadNative(const std::filesystem::path& pack,const std::filesystem::path& recordingsDirectory,unsigned trackId=0);
    // A native menu session supplies its own freshly initialized race memory.
    void loadNativeState(const std::array<std::uint8_t,0x80000>& memory,
        const std::vector<std::uint8_t>& payload,const std::filesystem::path& recordingsDirectory);
    // Host Leave Race: select the normal quit outcome without another tick.
    // Recorded keyboard quit input continues to use the original gated path.
    void quitLive();
    // Complete the recovered local race exit after quit or the end-message wait.
    void finishLiveExit(std::array<std::uint8_t,0x80000>& sessionMemory);
    // Observation/copy boundaries for native persistence; no live mutation.
    void copyLiveExit(std::array<std::uint8_t,0x80000>& sessionMemory)const;
    void copyLiveRecords(std::array<std::uint8_t,32>& entries)const;
    void resetLive();
    bool stepLive(unsigned controls,bool quit=false,DriveControlPreset preset=DriveControlPreset::Original);
    // In-memory career targets only. No file, settings or automatic draw path.
    // Multiplayer and a mismatched mode/track/league cannot bind a recorder.
    bool recordPersonalBestLaps(PersonalBestLaps&,GhostLapContext);
    const LapRecordingStatistics* lapRecordingStatistics()const noexcept;
    std::optional<GhostLapPlayback> lapGhostPlayback(double position,bool interpolate=true)const;
    void clearPersonalBestLap();
    const std::vector<std::uint32_t>& renderPracticeGhost(double fraction,bool interpolate,NativeView);
    unsigned verifyLiveReplay(const std::filesystem::path& inputs,const std::filesystem::path& reference,
        const std::filesystem::path& nativeTrace);
    const std::vector<std::uint32_t>& renderLive(double fraction,bool wireframe=false,bool interpolate=true,NativeView view=NativeView::Original);
    const std::vector<std::uint32_t>& renderLiveMessage(unsigned index,unsigned mode,bool includeGhost=false);
    // Original 0..15 indices; wide upper-cage samples additionally encode
    // palette pairs for presentationColor. Persistence uses Original.
    std::vector<std::uint16_t> captureLiveIndices(bool endMessage=false,NativeView view=NativeView::Original);
    unsigned liveTicks() const noexcept;
    bool liveTerminal() const noexcept;
    const std::filesystem::path& liveRecording() const noexcept;
    GameAudioSeed audioSeed()const;
    const std::vector<std::uint32_t>& render(unsigned frame,bool wireframe=false);
    // Player HUD enhancement. Exact/original-code tools leave this disabled.
    void setRunningLapHundredths(bool enabled){runningLapHundredths_=enabled;cachedFrame_=~0u;}
    const std::vector<std::uint32_t>& renderInterpolated(double position,bool wireframe=false);
    // Independent presentation query. Default settings retain the original
    // view; alternate angles/cockpit visibility leave saved frames unchanged.
    const std::vector<std::uint32_t>& renderView(double position,const CameraViewSettings&,bool wireframe=false);
    // Explicit renderer query. lapGhostPlayback observes career targets
    // separately; this entry point does not enable player ghost controls.
    const std::vector<std::uint32_t>& renderGhostView(double position,const CameraViewSettings&,
        const GhostCarPose&,const GhostCarStyle& style={});
    const GhostCarStatistics& ghostStatistics()const noexcept{return ghostStatistics_;}
    void saveBitmap(const std::filesystem::path&) const;
    const std::vector<RaceFrame>& frames() const noexcept {return frames_;}
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
    std::uint64_t trackContentIdentity_{};
    void initialise(const std::filesystem::path& chip,const std::filesystem::path& payload);
    void initialiseMemory(const std::array<std::uint8_t,0x80000>&,const std::vector<std::uint8_t>&);
    void beginLiveRecording(const std::filesystem::path&);
    RaceFrame captureFrame(Simulation&,unsigned tick,unsigned controls);
    void finishFrame(Simulation&,RaceFrame&);
    RaceFrame advanceSimulation(Simulation&,unsigned controls,bool quit,DriveControlPreset preset=DriveControlPreset::Original);
    RaceRenderAssets assets_;
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
    std::vector<RaceFrame> frames_;
    SoftwareRenderer renderer_;
    GhostCarRenderer ghostRenderer_;
    GhostCarStatistics ghostStatistics_{};
    RenderStatistics statistics_{};
    std::vector<std::uint32_t> pixels_;
    std::vector<std::uint32_t> foreground_;
    std::vector<std::uint32_t> cage_;
    unsigned cachedFrame_=~0u;
    double cachedFraction_{};
    bool cachedWireframe_{};
    double lastLiveFraction_{};
    bool lastLiveInterpolate_{true};
    NativeView lastLiveView_{NativeView::Original};
    bool bitmapOnly_{};
    bool runningLapHundredths_{};
    const std::vector<std::uint32_t>& renderViewImpl(double,const CameraViewSettings&,bool,
        const GhostCarPose*,const GhostCarStyle*);
    NearTrackPresentation buildViewCommands(const PresentationPose&,const RaceFrame&,
        const std::optional<OpponentCarInputs>&,int padding=0,int topPadding=0,int bottomPadding=0)const;
    void draw(const PresentationPose&,const NearTrackPresentation&,bool wireframe,
        const TrackPresentationSnapshot* movingTrack=nullptr,const RaceFrame* discrete=nullptr,double fraction=0,
        bool showCockpit=true,NativeView view=NativeView::Original,const RaceFrame* next=nullptr,
        const GhostCarPose* ghost=nullptr,const GhostCarStyle* ghostStyle=nullptr);
};
} // namespace scr::runtime
