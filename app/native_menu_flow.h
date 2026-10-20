#pragma once
#include "native_menu_render.h"
#include <cstdint>

namespace scr::viewer {
enum class NativeMenuPage { Main,Division,Track,Preview,Race,Matchup,Results,RoundResults,Standings,SeasonEnd,SeasonAward,Roster,Records,Opening,HallOfFame };
enum class NativeMenuAction { None,Changed,EnterPreview,StartPractice,RotatePreview,UnsupportedOption,StartRace,ShowCompetitionPage,ShowHallOfFame,LeaveHallOfFame };
struct NativeMenuInput {
    // Original readControllerInput bits: up/down/left/right/fire. Number keys
    // select a row; they deliberately do not activate it without fire/Return.
    std::uint8_t controls{},numberedKeys{};
};

// Original startup's default names/record templates, followed by the local
// main-menu context. Reads all character and record bytes from the asset image.
void initializeNativeMenuSession(NativeMemory&);

// One original processLocalMenuInput pass. The host supplies a normalized
// controller mask; numeric keys are resolved through the original raw-key table.
// Returns true if the original would repaint its options (even at a boundary).
bool applyNativeMenuInput(NativeMemory&,const NativeMenuInput&);

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

class NativeMenuFlow {
public:
    explicit NativeMenuFlow(NativeMemory&);
    NativeMenuAction update(const NativeMenuInput&,double elapsedSeconds);
    void returnFromRace();
    void cancelPreview();
    [[nodiscard]] NativeMenuPage page()const noexcept{return page_;}
    [[nodiscard]] NativeMenuRenderState renderState()const;
    [[nodiscard]] unsigned trackId()const noexcept{return memory_[0x1ca33];}
    [[nodiscard]] unsigned unsupportedOption()const noexcept{return unsupported_;}
    [[nodiscard]] bool competitive()const noexcept{return competitive_;}
    [[nodiscard]] unsigned competitionDivision()const noexcept{return competitionDivision_;}
    [[nodiscard]] unsigned competitionContext()const noexcept{return competitionContext_;}
private:
    enum class Phase { PaintDelay,Release,ReleaseDelay,Input,ConfirmDelay,TrackDelay };
    NativeMemory& memory_;
    NativeMenuPage page_{NativeMenuPage::Main};
    Phase phase_{Phase::PaintDelay};
    double remaining_{};
    std::uint8_t previewReleaseMask_{0x1f};
    unsigned unsupported_{};
    bool competitive_{};
    unsigned competitionDivision_{};
    unsigned competitionContext_{};
    NativeFireWait competitionFire_;
    void startDelay(unsigned units,bool repaint=false);
    void openMain(bool beginLoop=true);
    void openPractice();
    NativeMenuAction openNextSeasonRace();
    void openCompetitionPage(NativeMenuPage,unsigned gameState);
    void openPage(NativeMenuPage,unsigned offset,unsigned count,unsigned selected,unsigned textMode);
    NativeMenuAction confirm();
};
} // namespace scr::viewer
