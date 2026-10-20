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
#include "native_save_menu.h"
#include "native_save_state.h"
#include "native_save_store.h"
#include <optional>

namespace scr::viewer {
struct NativeGameInput {
    NativeMenuInput menu;
    unsigned driveControls{};
    bool quit{};
    std::vector<NativeRawKeyEvent> rawKeys;
};
enum class NativeGameUnavailable { none,menuRoute,computerLink };
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
        NativeGameStart start=NativeGameStart::OriginalOpening,const std::filesystem::path& saveFile={});
    NativeGameUpdate update(double elapsed,const NativeGameInput&);
    const std::vector<std::uint32_t>& render(bool interpolate=false,bool paused=false);
    NativeMenuPage page()const noexcept{
        if(openingActive())return NativeMenuPage::Opening;
        if(saveReturn_==SaveReturn::Roster)return NativeMenuPage::Roster;
        if(saveReturn_==SaveReturn::Standings)return NativeMenuPage::Standings;
        return menu_->page();
    }
    NativeMenuRenderState menuState()const{return openingActive()?opening_->renderState():menu_->renderState();}
    bool openingActive()const noexcept{return opening_&&!opening_->complete();}
    NativeOpeningPage openingPage()const noexcept{return opening_?opening_->page():NativeOpeningPage::Complete;}
    bool enteringName()const noexcept{return openingActive()&&opening_->page()==NativeOpeningPage::Name;}
    bool usingTextInput()const noexcept{return enteringName()||bool(saveMenu_);}
    std::optional<NativeSaveMenuPhase> savePhase()const noexcept{return saveMenu_?std::optional{saveMenu_->phase()}:std::nullopt;}
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
    NativeSaveStore saveStore_;
    std::unique_ptr<NativeSaveMenu> saveMenu_;
    std::optional<NativeSavePayload> pendingSave_;
    enum class SaveReturn {None,Roster,Standings};
    SaveReturn saveReturn_{SaveReturn::None};
    NativeFireWait saveReturnFire_;
    bool saveLoaded_{};
    bool saveWriting_{};
    NativeTrackPreview preview_;
    ReplayScene race_;
    FixedDriveClock driveClock_;
    DriveInputLatch driveLatch_;
    std::filesystem::path recordings_;
    std::vector<std::uint32_t> pixels_;
    NativeFireWait endFireWait_;
    bool quitPending_{};
    enum class Transition {None,InitialMenu,OutPreview,InPreview,OutRace,RaceDelay,InRace,OutMenu,InMenu,OutCompetition,InCompetition,OutOpening,InOpening,OutHall,InHall,OutHallMenu,InHallMenu,OutSaveRoster,InSaveRoster,OutReplayFrame,InReplayFrame,OutRestartOpening,SaveDelay};
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
    void handleSaveAction(const NativeSaveMenuResult&);
    void finishSave();
    void showSaveReturn(const NativeMenuImage&);
    void replayGame(const NativeMenuImage&);
};
} // namespace scr::viewer
