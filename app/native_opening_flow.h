#pragma once
#include "native_menu_flow.h"
#include <memory>

namespace scr::viewer {
enum class NativeOpeningPage { Mode,Name,AddPlayer,Roster,LocalStandings,Complete };
enum class NativeOpeningAction { None,Changed,BeginName,ShowRoster,Complete };

// Original startup templates and pre-mode local context. Asset decoding and
// initializeGameData are already performed by startNativeGame; do not repeat
// that initialization here because it advances the original random sequence.
void initializeNativeOpeningSession(NativeMemory&);
// Original setupGameConfiguration/sortRaceResults for the 2..8 local drivers.
// This is startup standings preparation, independent of racing a season.
void prepareNativeOpeningLocalStandings(NativeMemory&);

// Mode/add-driver menus use the original release, busy-delay and confirm order.
// The caller freezes this machine during palette fades, handles BeginName with
// NativePlayerNameEntry, and calls nameCompleted only after a nonblank name.
class NativeOpeningFlow {
public:
    explicit NativeOpeningFlow(NativeMemory&);
    NativeOpeningAction update(const NativeMenuInput&,double elapsedSeconds);
    NativeOpeningAction nameCompleted();
    NativeOpeningAction back();
    NativeOpeningAction beginSingleName(){multiplayer_=false;completedPlayers_=0;memory_[0x5eb76]=0;page_=NativeOpeningPage::Name;return NativeOpeningAction::BeginName;}
    NativeOpeningAction beginMultiplayerName(){multiplayer_=true;completedPlayers_=0;memory_[0x5eb76]=0;page_=NativeOpeningPage::Name;return NativeOpeningAction::BeginName;}
    bool multiplayer()const noexcept{return multiplayer_;}
    void setNavigation(NativeMenuNavigation mode)noexcept{navigation_=mode;}
    // Return to the mode chooser; Computer Link is a disabled native row.
    void returnToMode();
    [[nodiscard]] NativeOpeningPage page()const noexcept{return page_;}
    [[nodiscard]] bool complete()const noexcept{return page_==NativeOpeningPage::Complete;}
    [[nodiscard]] NativeMenuRenderState renderState()const;
    [[nodiscard]] unsigned playerCount()const noexcept{return unsigned(memory_[0x5eb76])+1;}
private:
    enum class Phase { PaintDelay,Release,ReleaseDelay,Input,ConfirmDelay };
    NativeMemory& memory_;
    NativeOpeningPage page_{NativeOpeningPage::Mode};
    Phase phase_{Phase::PaintDelay};
    double remaining_{};
    NativeFireWait fire_;
    bool multiplayer_{};
    NativeMenuNavigation navigation_{NativeMenuNavigation::Original};
    unsigned completedPlayers_{};
    int modeSelection_{-1},addSelection_{-1};
    std::unique_ptr<NativeMemory> setupCheckpoint_;
    void openMenu(NativeOpeningPage,unsigned offset,unsigned finalRow);
    void delay(unsigned units);
    NativeOpeningAction confirm();
    NativeOpeningAction showRoster();
};
} // namespace scr::viewer
