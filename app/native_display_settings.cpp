#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_display_settings.h"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace scr::viewer {
namespace {
void validatePath(const std::filesystem::path& path){
    if(path.extension()!=L".ini")throw std::invalid_argument("Display preferences require an .ini file");
}
}
NativeDisplayPreferences loadDisplayPreferences(const std::filesystem::path& path){
    validatePath(path);NativeDisplayPreferences preferences;
    std::error_code error;const auto size=std::filesystem::file_size(path,error);
    if(error||size>1024)return preferences;
    std::ifstream input(path);std::string header,full,scale,motion,extra;
    if(!std::getline(input,header)||!std::getline(input,full)||!std::getline(input,scale)||!std::getline(input,motion))return {};
    const bool version5=header=="SCR display 5",version6=header=="SCR display 6",version7=header=="SCR display 7",version8=header=="SCR display 8";
    const bool current=header=="SCR display 4"||version5||version6||version7||version8;
    if((header!="SCR display 1"&&header!="SCR display 2"&&header!="SCR display 3"&&!current)||(full!="fullscreen=0"&&full!="fullscreen=1")||
       (motion!="interpolate=0"&&motion!="interpolate=1"))return {};
    if(scale=="scaling=integer")preferences.scaling=NativeScaling::Integer;
    else if(current&&scale=="scaling=fit")preferences.scaling=NativeScaling::Fit;
    else if(current&&scale=="scaling=stretch")preferences.scaling=NativeScaling::Stretch;
    else if(!current&&scale=="scaling=sharp")preferences.scaling=NativeScaling::Fit;
    else if(!current&&scale=="scaling=smooth"){preferences.scaling=NativeScaling::Fit;preferences.filtering=NativeFiltering::Bilinear;}
    else return {};
    // Version 1 predates control presets. Keep its display choices and use
    // the faithful controls until the player explicitly selects Modern.
    if(header!="SCR display 1"){
        std::string controls;if(!std::getline(input,controls))return {};
        if(controls=="controls=modern")preferences.controls=DriveControlPreset::Modern;
        else if(controls!="controls=original")return {};
    }
    if(header=="SCR display 3"||current){
        std::string navigation;if(!std::getline(input,navigation))return {};
        if(navigation=="navigation=original")preferences.navigation=NativeMenuNavigation::Original;
        else if(navigation!="navigation=responsive")return {};
    }
    if(current){
        std::string filtering,integer;
        if(!std::getline(input,filtering)||!std::getline(input,integer))return {};
        if(filtering=="filtering=bilinear")preferences.filtering=NativeFiltering::Bilinear;
        else if(filtering!="filtering=nearest")return {};
        const std::string prefix="integer-scale=";
        if(integer.compare(0,prefix.size(),prefix)!=0)return {};
        const auto first=integer.data()+prefix.size(),last=integer.data()+integer.size();
        const auto parsed=std::from_chars(first,last,preferences.integerScale);
        if(parsed.ec!=std::errc{}||parsed.ptr!=last||preferences.integerScale>maximumIntegerScale)return {};
    }
    if(version5){
        std::string view;if(!std::getline(input,view))return {};
        if(view=="view=widescreen")preferences.view=NativeView::Extended;
        else if(view!="view=original")return {};
    }
    if(version6||version7||version8){
        std::string aspect;if(!std::getline(input,aspect))return {};
        if(version8&&aspect=="aspect=ultrawide")preferences.view=NativeView::Ultrawide;
        else if((version7||version8)&&aspect=="aspect=widescreen")preferences.view=NativeView::Widescreen;
        else if(aspect=="aspect=extended")preferences.view=NativeView::Extended;
        else if(aspect!="aspect=original")return {};
    }
    while(std::getline(input,extra))if(!extra.empty())return {};
    preferences.fullscreen=full=="fullscreen=1";preferences.interpolate=motion=="interpolate=1";return preferences;
}
bool saveDisplayPreferences(const std::filesystem::path& path,const NativeDisplayPreferences& preferences){
    validatePath(path);
    std::error_code error;if(path.has_parent_path())std::filesystem::create_directories(path.parent_path(),error);
    if(error)return false;
    if(preferences.integerScale>maximumIntegerScale)return false;
    std::ostringstream output;output<<"SCR display 8\nfullscreen="<<preferences.fullscreen<<"\nscaling=";
    switch(preferences.scaling){case NativeScaling::Integer:output<<"integer";break;
        case NativeScaling::Fit:output<<"fit";break;case NativeScaling::Stretch:output<<"stretch";break;default:return false;}
    output<<"\ninterpolate="<<preferences.interpolate<<"\ncontrols=";
    switch(preferences.controls){case DriveControlPreset::Original:output<<"original";break;
        case DriveControlPreset::Modern:output<<"modern";break;default:return false;}
    output<<"\nnavigation=";
    switch(preferences.navigation){case NativeMenuNavigation::Original:output<<"original";break;
        case NativeMenuNavigation::Responsive:output<<"responsive";break;default:return false;}
    output<<"\nfiltering=";
    switch(preferences.filtering){case NativeFiltering::NearestNeighbour:output<<"nearest";break;
        case NativeFiltering::Bilinear:output<<"bilinear";break;default:return false;}
    output<<"\ninteger-scale="<<preferences.integerScale<<"\naspect=";
    switch(preferences.view){case NativeView::Original:output<<"original";break;case NativeView::Extended:output<<"extended";break;case NativeView::Widescreen:output<<"widescreen";break;case NativeView::Ultrawide:output<<"ultrawide";break;default:return false;}
    output<<'\n';const auto bytes=output.str();
    const std::filesystem::path pending=path.wstring()+L".tmp";
    const auto handle=CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return false;
    DWORD written{};const bool complete=WriteFile(handle,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(handle);
    CloseHandle(handle);
    if(!complete||!MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(pending.c_str());return false;
    }
    return true;
}
const wchar_t* scalingName(NativeScaling scaling)noexcept{
    switch(scaling){case NativeScaling::Fit:return L"Fit";case NativeScaling::Stretch:return L"Stretch";default:return L"Integer";}
}
const wchar_t* filteringName(NativeFiltering filtering)noexcept{return filtering==NativeFiltering::Bilinear?L"Bilinear":L"Nearest neighbour";}
std::wstring integerScaleName(unsigned scale){return scale?std::to_wstring(scale)+L"×":L"Auto";}
const wchar_t* viewName(NativeView view)noexcept{
    return view==NativeView::Ultrawide?L"32:9":view==NativeView::Widescreen?L"16:9":view==NativeView::Extended?L"4:3 (Extended)":L"4:3 (Original)";
}
int displayWidthAtScale(NativeView view,unsigned scale)noexcept{
    return isWideView(view)?int(((view==NativeView::Ultrawide?2560:1280)*scale+1)/3):320*int(scale);
}
PresentationRectangle displayRectangle(int width,int height,const NativeDisplayPreferences& preferences,int sourceWidth){
    if(!sourceWidth)sourceWidth=viewWidth(preferences.view);
    if(width<=0||height<=0)return {};
    if(preferences.scaling==NativeScaling::Stretch)return {0,0,width,height};
    const auto view=sourceWidth==replayUltrawideWidth?NativeView::Ultrawide:sourceWidth==replayWidescreenWidth?NativeView::Widescreen:NativeView::Original;
    const bool wide=isWideView(view);const int wideUnits=view==NativeView::Ultrawide?2560:1280;
    const auto fitted=fitPresentationImage(wide?(view==NativeView::Ultrawide?32:16):4,wide?9:3,width,height,false);
    if(preferences.scaling==NativeScaling::Fit)return fitted;
    const auto maximum=std::min(wide?(width*3+1)/wideUnits:width/320,height/originalDisplayHeight);
    if(maximum<1)return fitted;
    const auto scale=preferences.integerScale?std::min(unsigned(maximum),preferences.integerScale):unsigned(maximum);
    const int w=displayWidthAtScale(view,scale),h=originalDisplayHeight*int(scale);return {(width-w)/2,(height-h)/2,w,h};
}
void changeDisplayPreference(NativeDisplayPreferences& preferences,unsigned row,int direction){
    if(row==0)preferences.fullscreen=!preferences.fullscreen;
    else if(row==1)preferences.scaling=NativeScaling((unsigned(preferences.scaling)+(direction<0?2:1))%3);
    else if(row==2)preferences.interpolate=!preferences.interpolate;
    else if(row==3)preferences.controls=preferences.controls==DriveControlPreset::Original?DriveControlPreset::Modern:DriveControlPreset::Original;
    else if(row==4)preferences.navigation=preferences.navigation==NativeMenuNavigation::Responsive?NativeMenuNavigation::Original:NativeMenuNavigation::Responsive;
    else if(row==6)preferences.view=NativeView((unsigned(preferences.view)+(direction<0?3:1))%4);
    else if(row==5){
        if(preferences.scaling==NativeScaling::Integer)preferences.integerScale=(preferences.integerScale+(direction<0?maximumIntegerScale:1))%(maximumIntegerScale+1);
        else preferences.filtering=preferences.filtering==NativeFiltering::NearestNeighbour?NativeFiltering::Bilinear:NativeFiltering::NearestNeighbour;
    }
}
unsigned displayPadNavigation(const DrivePad& pad)noexcept{
    if(!pad.connected)return 0;
    return (((pad.buttons&0x0001)||pad.y>16000)?1u:0u)|(((pad.buttons&0x0002)||pad.y<-16000)?2u:0u)|
        (((pad.buttons&0x0004)||pad.x<-16000)?4u:0u)|(((pad.buttons&0x0008)||pad.x>16000)?8u:0u)|
        ((pad.buttons&0x1000)?16u:0u)|((pad.buttons&0x2000)?32u:0u);
}
void NativeDisplaySettings::openSettings(unsigned held){
    if(page_==NativeDisplayPage::Settings)return;
    returnPage_=page_;before_=preferences_;page_=NativeDisplayPage::Settings;selected_=0;previous_=held;
}
bool gameMenuShortcut(unsigned key,bool textEntry,bool raceScreen,bool modeChooser)noexcept{
    return key==VK_ESCAPE?(raceScreen||modeChooser):key=='P'&&!textEntry;
}
unsigned NativeDisplaySettings::optionCount()const noexcept{
    return page_==NativeDisplayPage::Settings?9:page_==NativeDisplayPage::Pause?(canLeaveRace_?4:3):page_==NativeDisplayPage::QuitConfirmation?2:0;
}
void NativeDisplaySettings::openGameMenu(bool allowLeaveRace,unsigned held,NativeQuitContext context){
    canLeaveRace_=allowLeaveRace;quitContext_=context;page_=NativeDisplayPage::Pause;selected_=0;previous_=held;action_=NativeDisplayAction::None;
}
void NativeDisplaySettings::pause(unsigned held){openGameMenu(true,held);}
void NativeDisplaySettings::suspendRace(unsigned held,bool allowLeaveRace,NativeQuitContext context){
    canLeaveRace_=allowLeaveRace;quitContext_=context;
    if(page_==NativeDisplayPage::Settings){returnPage_=NativeDisplayPage::Pause;previous_=held;}
    else if(page_!=NativeDisplayPage::QuitConfirmation)openGameMenu(allowLeaveRace,held,context);
}
void NativeDisplaySettings::back(){
    if(page_==NativeDisplayPage::Settings){preferences_=before_;page_=returnPage_;selected_=page_==NativeDisplayPage::Pause?1:0;}
    else if(page_==NativeDisplayPage::QuitConfirmation){page_=NativeDisplayPage::Pause;selected_=canLeaveRace_?3:2;}
    else if(page_==NativeDisplayPage::Pause)page_=NativeDisplayPage::Closed;
}
void NativeDisplaySettings::applied(){
    if(page_==NativeDisplayPage::Settings){page_=returnPage_;selected_=page_==NativeDisplayPage::Pause?1:0;}
}
bool NativeDisplaySettings::navigate(unsigned controls){
    if(waitForRelease_){previous_=controls;if(controls==0)waitForRelease_=false;return false;}
    const auto pressed=controls&~previous_;previous_=controls;if(!blocksGame())return false;
    if(pressed&32){back();return false;}
    const unsigned count=optionCount();
    if(pressed&1)selected_=(selected_+count-1)%count;
    else if(pressed&2)selected_=(selected_+1)%count;
    if(page_==NativeDisplayPage::Pause){
        if(pressed&16){
            if(selected_==0)back();
            else if(selected_==1)openSettings(controls);
            else if(canLeaveRace_&&selected_==2){action_=NativeDisplayAction::LeaveRace;page_=NativeDisplayPage::Closed;}
            else{page_=NativeDisplayPage::QuitConfirmation;selected_=0;}
        }
        return false;
    }
    if(page_==NativeDisplayPage::QuitConfirmation){
        if(pressed&16){if(selected_==0)back();else{action_=NativeDisplayAction::Quit;page_=NativeDisplayPage::Closed;}}
        return false;
    }
    if(selected_<7&&(pressed&(4|8|16)))changeDisplayPreference(preferences_,displayRowOrder[selected_],(pressed&4)?-1:1);
    else if(pressed&16){if(selected_==7)return true;if(selected_==8)back();}
    return false;
}
} // namespace scr::viewer
