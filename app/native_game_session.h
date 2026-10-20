#pragma once
#include "native_startup.h"
#include "native_menu_flow.h"
#include "native_track_preview.h"
#include "replay_scene.h"
#include "live_drive.h"
#include "native_palette_fade.h"

namespace scr::viewer {
struct NativeGameInput {
    NativeMenuInput menu;
    unsigned driveControls{};
    bool quit{};
};
enum class NativeGameUnavailable { none,menuRoute };
struct NativeGameUpdate {
    bool raceStarted{},raceExited{};
    NativeGameUnavailable unavailable{};
    std::vector<GameAudioFrame> audio;
};
// Original local main menu -> Practice selection -> overview -> race -> menu.
// The memory belongs to the session; leaving a race retains the recovered RNG,
// player and track-selection state instead of constructing another saved game.
class NativeGameSession {
public:
    NativeGameSession(const std::filesystem::path& pack,const std::filesystem::path& recordings);
    NativeGameUpdate update(double elapsed,const NativeGameInput&);
    const std::vector<std::uint32_t>& render(bool interpolate=false,bool paused=false);
    NativeMenuPage page()const noexcept{return menu_.page();}
    NativeMenuRenderState menuState()const{return menu_.renderState();}
    const NativeMemory& memory()const noexcept{return *data_.memory;}
    const ReplayScene& race()const noexcept{return race_;}
    bool raceEnded()const noexcept{return page()==NativeMenuPage::Race&&race_.liveTerminal();}
    bool transitioning()const noexcept{return transition_!=Transition::None;}
    void releaseInput()noexcept{driveLatch_.clear();quitPending_=false;}
private:
    NativeStartupData data_;
    NativeMenuFlow menu_;
    NativeTrackPreview preview_;
    ReplayScene race_;
    FixedDriveClock driveClock_;
    DriveInputLatch driveLatch_;
    std::filesystem::path recordings_;
    std::vector<std::uint32_t> pixels_;
    NativeFireWait endFireWait_;
    bool quitPending_{};
    enum class Transition {None,InitialMenu,OutPreview,InPreview,OutRace,RaceDelay,InRace,OutMenu,InMenu,OutCompetition,InCompetition};
    Transition transition_{Transition::InitialMenu};
    NativePaletteFade fade_;
    std::vector<std::uint8_t> transitionIndices_;
    double raceDelay_{};
    void advanceTransition(double,NativeGameUpdate&);
    void preparePreview();
    void startRace();
    void leaveRace();
    NativeMenuImage menuImage()const;
};
} // namespace scr::viewer
