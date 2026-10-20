#pragma once
#include "native_startup.h"
#include "native_menu_flow.h"
#include "native_track_preview.h"
#include "replay_scene.h"
#include "live_drive.h"
#include "native_palette_fade.h"
#include "native_opening_flow.h"
#include "native_opening_render.h"
#include "native_player_name.h"
#include "native_keyboard_input.h"

namespace scr::viewer {
struct NativeGameInput {
    NativeMenuInput menu;
    unsigned driveControls{};
    bool quit{};
    std::vector<NativeRawKeyEvent> rawKeys;
};
enum class NativeGameUnavailable { none,menuRoute,computerLink,localMultiplayerSeason };
enum class NativeGameStart { OriginalOpening,EstablishedMenu };
struct NativeGameUpdate {
    bool raceStarted{},raceExited{};
    NativeGameUnavailable unavailable{};
    std::vector<GameAudioFrame> audio;
};
// Original opening -> names -> roster -> menu -> selection -> race -> menu.
// The memory belongs to the session; leaving a race retains the recovered RNG,
// player and track-selection state instead of constructing another saved game.
class NativeGameSession {
public:
    NativeGameSession(const std::filesystem::path& pack,const std::filesystem::path& recordings,
        NativeGameStart start=NativeGameStart::OriginalOpening);
    NativeGameUpdate update(double elapsed,const NativeGameInput&);
    const std::vector<std::uint32_t>& render(bool interpolate=false,bool paused=false);
    NativeMenuPage page()const noexcept{return openingActive()?NativeMenuPage::Opening:menu_->page();}
    NativeMenuRenderState menuState()const{return openingActive()?opening_->renderState():menu_->renderState();}
    bool openingActive()const noexcept{return opening_&&!opening_->complete();}
    NativeOpeningPage openingPage()const noexcept{return opening_?opening_->page():NativeOpeningPage::Complete;}
    bool enteringName()const noexcept{return openingActive()&&opening_->page()==NativeOpeningPage::Name;}
    const NativeMemory& memory()const noexcept{return *data_.memory;}
    const ReplayScene& race()const noexcept{return race_;}
    bool raceEnded()const noexcept{return page()==NativeMenuPage::Race&&race_.liveTerminal();}
    bool transitioning()const noexcept{return transition_!=Transition::None;}
    void releaseInput()noexcept;
private:
    NativeStartupData data_;
    std::unique_ptr<NativeMenuFlow> menu_;
    std::unique_ptr<NativeOpeningFlow> opening_;
    NativePlayerNameEntry nameEntry_;
    std::unique_ptr<NativeOpeningNameScreen> nameScreen_;
    NativeTrackPreview preview_;
    ReplayScene race_;
    FixedDriveClock driveClock_;
    DriveInputLatch driveLatch_;
    std::filesystem::path recordings_;
    std::vector<std::uint32_t> pixels_;
    NativeFireWait endFireWait_;
    bool quitPending_{};
    enum class Transition {None,InitialMenu,OutPreview,InPreview,OutRace,RaceDelay,InRace,OutMenu,InMenu,OutCompetition,InCompetition,OutOpening,InOpening};
    Transition transition_{Transition::InitialMenu};
    NativePaletteFade fade_;
    std::vector<std::uint8_t> transitionIndices_;
    double raceDelay_{};
    void advanceTransition(double,NativeGameUpdate&);
    void preparePreview();
    void startRace();
    void leaveRace();
    NativeMenuImage menuImage()const;
    void updateOpening(const NativeGameInput&,double,NativeGameUpdate&);
    void handleOpeningAction(NativeOpeningAction,const NativeMenuImage&,NativeGameUpdate&);
};
} // namespace scr::viewer
