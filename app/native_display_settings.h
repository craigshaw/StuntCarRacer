#pragma once
#include "live_drive.h"
#include "native_menu_navigation.h"
#include "replay_view.h"
#include <filesystem>
#include <string>

namespace scr::viewer {
enum class NativeScaling { Integer, Fit, Stretch };
enum class NativeFiltering { NearestNeighbour, Bilinear };
constexpr unsigned maximumIntegerScale=32;
struct NativeDisplayPreferences {
    bool fullscreen{},interpolate{};
    NativeScaling scaling{NativeScaling::Integer};
    DriveControlPreset controls{DriveControlPreset::Original};
    NativeMenuNavigation navigation{NativeMenuNavigation::Responsive};
    NativeFiltering filtering{NativeFiltering::NearestNeighbour};
    unsigned integerScale{}; // Zero selects the largest fitting integer scale.
    bool operator==(const NativeDisplayPreferences& other)const noexcept {
        return fullscreen==other.fullscreen&&interpolate==other.interpolate&&scaling==other.scaling&&controls==other.controls&&navigation==other.navigation&&filtering==other.filtering&&integerScale==other.integerScale;
    }
};
NativeDisplayPreferences loadDisplayPreferences(const std::filesystem::path&);
bool saveDisplayPreferences(const std::filesystem::path&,const NativeDisplayPreferences&);
const wchar_t* scalingName(NativeScaling)noexcept;
const wchar_t* filteringName(NativeFiltering)noexcept;
std::wstring integerScaleName(unsigned);
PresentationRectangle displayRectangle(int width,int height,const NativeDisplayPreferences&);
// Existing preference row identities stay stable; the dependent row is last.
constexpr unsigned displayRowOrder[]{0,1,5,2,3,4};
void changeDisplayPreference(NativeDisplayPreferences&,unsigned row,int direction=1);
// Bits shared by keyboard and controller navigation: up/down/left/right/A/B.
unsigned displayPadNavigation(const DrivePad&)noexcept;
enum class NativeDisplayPage { Closed, Pause, Settings };
class NativeDisplaySettings {
public:
    explicit NativeDisplaySettings(NativeDisplayPreferences preferences={}):preferences_(preferences){}
    const NativeDisplayPreferences& preferences()const noexcept{return preferences_;}
    NativeDisplayPage page()const noexcept{return page_;}
    bool blocksGame()const noexcept{return page_!=NativeDisplayPage::Closed;}
    unsigned selected()const noexcept{return selected_;}
    void openSettings(unsigned held=0);
    void pause(unsigned held=0);
    void suspendRace(unsigned held=0);
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
    bool waitForRelease_{};
};
} // namespace scr::viewer
