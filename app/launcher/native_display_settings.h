#pragma once
#include "input/drive_input.h"
#include "menus/native_menu_navigation.h"
#include "presentation/camera_view.h"
#include <filesystem>
#include <string>

namespace scr::runtime {
enum class NativeScaling { Integer, Fit, Stretch };
enum class NativeFiltering { NearestNeighbour, Bilinear };
constexpr unsigned maximumIntegerScale=32;
// 320x200 bitmap pixels are displayed with the original 4:3 proportions.
constexpr int originalDisplayHeight=240;
struct NativeDisplayPreferences {
    bool fullscreen{},interpolate{};
    NativeScaling scaling{NativeScaling::Integer};
    DriveControlPreset controls{DriveControlPreset::Original};
    NativeMenuNavigation navigation{NativeMenuNavigation::Responsive};
    NativeFiltering filtering{NativeFiltering::NearestNeighbour};
    unsigned integerScale{}; // Zero selects the largest fitting integer scale.
    NativeView view{NativeView::Original};
    bool practiceGhost{true};
    bool operator==(const NativeDisplayPreferences& other)const noexcept {
        return fullscreen==other.fullscreen&&interpolate==other.interpolate&&scaling==other.scaling&&controls==other.controls&&navigation==other.navigation&&filtering==other.filtering&&integerScale==other.integerScale&&view==other.view&&practiceGhost==other.practiceGhost;
    }
};
NativeDisplayPreferences loadDisplayPreferences(const std::filesystem::path&);
bool saveDisplayPreferences(const std::filesystem::path&,const NativeDisplayPreferences&);
const wchar_t* scalingName(NativeScaling)noexcept;
const wchar_t* filteringName(NativeFiltering)noexcept;
std::wstring integerScaleName(unsigned);
const wchar_t* viewName(NativeView)noexcept;
int displayWidthAtScale(NativeView,unsigned scale)noexcept;
PresentationRectangle displayRectangle(int width,int height,const NativeDisplayPreferences&,int sourceWidth=0);
// Existing preference row identities stay stable; aspect ratio follows navigation.
constexpr unsigned displayRowOrder[]{0,1,5,2,3,4,6,7};
void changeDisplayPreference(NativeDisplayPreferences&,unsigned row,int direction=1);
// Bits shared by keyboard and controller navigation: up/down/left/right/A/B.
unsigned displayPadNavigation(const DrivePad&)noexcept;
bool gameMenuShortcut(unsigned virtualKey,bool textEntry,bool raceScreen,bool modeChooser)noexcept;
enum class NativeDisplayPage { Closed, Pause, Settings, QuitConfirmation, ClearGhostConfirmation };
enum class NativeDisplayAction { None, LeaveRace, Quit, ClearGhost };
enum class NativeQuitContext { General, Career, CareerRace, Multiplayer };
class NativeDisplaySettings {
public:
    explicit NativeDisplaySettings(NativeDisplayPreferences preferences={}):preferences_(preferences){}
    const NativeDisplayPreferences& preferences()const noexcept{return preferences_;}
    NativeDisplayPage page()const noexcept{return page_;}
    bool blocksGame()const noexcept{return page_!=NativeDisplayPage::Closed;}
    unsigned selected()const noexcept{return selected_;}
    unsigned optionCount()const noexcept;
    bool canLeaveRace()const noexcept{return canLeaveRace_;}
    NativeQuitContext quitContext()const noexcept{return quitContext_;}
    void setPracticeGhostContext(bool active,std::wstring time={},bool canClear=false){practice_=active;bestTime_=std::move(time);clearGhost_=active&&canClear;if(page_==NativeDisplayPage::Pause&&selected_>=optionCount())selected_=0;}
    bool practiceGhostContext()const noexcept{return practice_;}
    bool canClearGhost()const noexcept{return clearGhost_;}
    const std::wstring& bestTime()const noexcept{return bestTime_;}
    NativeDisplayAction takeAction()noexcept{const auto action=action_;action_=NativeDisplayAction::None;return action;}
    void openSettings(unsigned held=0);
    void openGameMenu(bool allowLeaveRace,unsigned held=0,NativeQuitContext context=NativeQuitContext::General);
    void pause(unsigned held=0);
    void suspendRace(unsigned held=0,bool allowLeaveRace=true,NativeQuitContext context=NativeQuitContext::General);
    void back();
    // True requests an atomic preference save. Call applied only on success.
    bool navigate(unsigned controls);
    void applied();
    void releaseNavigation()noexcept{previous_=0;waitForRelease_=true;}
    void toggleFullscreen()noexcept{preferences_.fullscreen=!preferences_.fullscreen;}
    void toggleInterpolation()noexcept{preferences_.interpolate=!preferences_.interpolate;}
private:
    NativeDisplayPreferences preferences_,before_;
    NativeDisplayPage page_{NativeDisplayPage::Closed},returnPage_{NativeDisplayPage::Closed};
    unsigned selected_{},previous_{};
    bool waitForRelease_{},canLeaveRace_{};
    bool practice_{},clearGhost_{};
    std::wstring bestTime_;
    NativeDisplayAction action_{NativeDisplayAction::None};
    NativeQuitContext quitContext_{NativeQuitContext::General};
};
} // namespace scr::runtime
