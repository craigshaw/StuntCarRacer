#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native_display_settings.h"
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
    if(header!="SCR display 1"||(full!="fullscreen=0"&&full!="fullscreen=1")||
       (motion!="interpolate=0"&&motion!="interpolate=1"))return {};
    if(scale=="scaling=integer")preferences.scaling=NativeScaling::Integer;
    else if(scale=="scaling=sharp")preferences.scaling=NativeScaling::FitSharp;
    else if(scale=="scaling=smooth")preferences.scaling=NativeScaling::FitSmooth;
    else return {};
    while(std::getline(input,extra))if(!extra.empty())return {};
    preferences.fullscreen=full=="fullscreen=1";preferences.interpolate=motion=="interpolate=1";return preferences;
}
bool saveDisplayPreferences(const std::filesystem::path& path,const NativeDisplayPreferences& preferences){
    validatePath(path);
    std::error_code error;if(path.has_parent_path())std::filesystem::create_directories(path.parent_path(),error);
    if(error)return false;
    std::ostringstream output;output<<"SCR display 1\nfullscreen="<<preferences.fullscreen<<"\nscaling=";
    switch(preferences.scaling){case NativeScaling::Integer:output<<"integer";break;
        case NativeScaling::FitSharp:output<<"sharp";break;case NativeScaling::FitSmooth:output<<"smooth";break;default:return false;}
    output<<"\ninterpolate="<<preferences.interpolate<<'\n';const auto bytes=output.str();
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
    switch(scaling){case NativeScaling::FitSharp:return L"Fit sharp";case NativeScaling::FitSmooth:return L"Fit smooth";default:return L"Integer";}
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
void NativeDisplaySettings::pause(unsigned held){page_=NativeDisplayPage::Pause;selected_=0;previous_=held;}
void NativeDisplaySettings::suspendRace(unsigned held){
    if(page_==NativeDisplayPage::Settings){returnPage_=NativeDisplayPage::Pause;previous_=held;}
    else pause(held);
}
void NativeDisplaySettings::back(){
    if(page_==NativeDisplayPage::Settings){preferences_=before_;page_=returnPage_;selected_=page_==NativeDisplayPage::Pause?1:0;}
    else if(page_==NativeDisplayPage::Pause)page_=NativeDisplayPage::Closed;
}
void NativeDisplaySettings::applied(){
    if(page_==NativeDisplayPage::Settings){page_=returnPage_;selected_=page_==NativeDisplayPage::Pause?1:0;}
}
bool NativeDisplaySettings::navigate(unsigned controls){
    if(waitForRelease_){previous_=controls;if(controls==0)waitForRelease_=false;return false;}
    const auto pressed=controls&~previous_;previous_=controls;if(!blocksGame())return false;
    if(pressed&32){back();return false;}
    const unsigned count=page_==NativeDisplayPage::Pause?2:5;
    if(pressed&1)selected_=(selected_+count-1)%count;
    else if(pressed&2)selected_=(selected_+1)%count;
    if(page_==NativeDisplayPage::Pause){
        if(pressed&16){if(selected_==0)back();else openSettings(controls);}return false;
    }
    if(selected_<3&&(pressed&(4|8|16))){
        if(selected_==0)toggleFullscreen();
        else if(selected_==2)toggleInterpolation();
        else preferences_.scaling=NativeScaling((unsigned(preferences_.scaling)+((pressed&4)?2:1))%3);
    }else if(pressed&16){if(selected_==3)return true;if(selected_==4)back();}
    return false;
}
} // namespace scr::viewer
