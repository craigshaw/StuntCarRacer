#pragma once
#include "native_menu_render.h"
#include "native_menu_navigation.h"
#include <array>
#include <cstdint>

namespace scr::viewer {
enum class NativeMenuPage { Main,Division,Track,Preview,Race,Matchup,Results,RoundResults,Standings,SeasonEnd,SeasonAward,Roster,Records,Opening,HallOfFame,LoadSave,SaveSlots,RaceResults,TrackResults };
enum class NativeMenuAction { None,Changed,EnterPreview,StartPractice,RotatePreview,UnsupportedOption,StartRace,ShowCompetitionPage,ShowHallOfFame,LeaveHallOfFame,LoadGame,SaveGame,ReplayGame };

// Original startup's default names/record templates, followed by the local
// main-menu context. Reads all character and record bytes from the asset image.
void initializeNativeMenuSession(NativeMemory&);

// One original processLocalMenuInput pass. The host supplies a normalized
// controller mask; numeric keys are resolved through the original raw-key table.
// Returns true if the original would repaint its options (even at a boundary).
// Optional native-only row mask leaves disabled entries visible but inert.
bool applyNativeMenuInput(NativeMemory&,const NativeMenuInput&,unsigned disabledRows=0);

// PAL 68000 execution time of the original busy-loop delay, excluding caller
// rendering and Amiga bus contention. This preserves its release/debounce order.
[[nodiscard]] double nativeMenuDelaySeconds(unsigned units,unsigned initialCounter=0);

// Original waitForFireButtonPress0x6488a: only fire must be released; gas and
// direction bits do not block it. A five-unit debounce precedes the next press.
class NativeFireWait {
public:
    void reset()noexcept{phase_=Phase::Release;remaining_=0;}
    bool update(std::uint8_t controls,double elapsedSeconds);
private:
    enum class Phase { Release,Delay,Press,Done };
    Phase phase_{Phase::Release};
    double remaining_{};
};

struct NativeMenuContinuation {
    NativeMenuPage page{NativeMenuPage::Main};
    bool competitive{},multiplayerTrackEnd{};
    unsigned division{},context{};
    int mainSelection{-1},divisionSelection{-1},saveSelection{-1};
    std::array<int,4> trackSelections{{-1,-1,-1,-1}};
};

class NativeMenuFlow {
public:
    explicit NativeMenuFlow(NativeMemory&);
    NativeMenuFlow(NativeMemory&,const NativeMenuContinuation&);
    NativeMenuContinuation continuation()const noexcept;
    NativeMenuAction update(const NativeMenuInput&,double elapsedSeconds);
    void setNavigation(NativeMenuNavigation mode)noexcept{navigation_=mode;}
    void setMainRowCount(unsigned count);
    NativeMenuAction back();
    void returnFromRace();
    void cancelPreview();
    void returnFromSave(bool cancelled=false);
    [[nodiscard]] NativeMenuPage page()const noexcept{return page_;}
    [[nodiscard]] NativeMenuRenderState renderState()const;
    [[nodiscard]] unsigned trackId()const noexcept{return memory_[0x1ca33];}
    [[nodiscard]] unsigned unsupportedOption()const noexcept{return unsupported_;}
    [[nodiscard]] bool competitive()const noexcept{return competitive_;}
    [[nodiscard]] bool multiplayer()const noexcept{return competitive_&&memory_[0x5eb76]!=0;}
    [[nodiscard]] unsigned competitionDivision()const noexcept{return competitionDivision_;}
    [[nodiscard]] unsigned competitionContext()const noexcept{return competitionContext_;}
private:
    enum class Phase { PaintDelay,Release,ReleaseDelay,Input,ConfirmDelay,TrackDelay,LoadSaveDelay,MainDelay };
    NativeMemory& memory_;
    NativeMenuPage page_{NativeMenuPage::Main};
    Phase phase_{Phase::PaintDelay};
    double remaining_{};
    std::uint8_t previewReleaseMask_{0x1f};
    unsigned unsupported_{};
    unsigned mainRowCount_{4};
    bool competitive_{};
    bool multiplayerTrackEnd_{};
    unsigned competitionDivision_{};
    unsigned competitionContext_{};
    NativeFireWait competitionFire_;
    NativeMenuNavigation navigation_{NativeMenuNavigation::Original};
    int mainSelection_{-1},divisionSelection_{-1},saveSelection_{-1};
    std::array<int,4> trackSelections_{{-1,-1,-1,-1}};
    void rememberSelection();
    void startDelay(unsigned units,bool repaint=false);
    void openMain(bool beginLoop=true);
    void openPractice();
    NativeMenuAction openNextSeasonRace();
    void openCompetitionPage(NativeMenuPage,unsigned gameState);
    void openPage(NativeMenuPage,unsigned offset,unsigned count,unsigned selected,unsigned textMode);
    NativeMenuAction confirm();
};
} // namespace scr::viewer
