#include "persistence/player_data_layout.h"
#ifdef _WIN32
#include "launcher/native_launcher.h"
#endif
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace scr::runtime;
namespace fs=std::filesystem;
unsigned checks{};
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F f){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}require(caught,"Invalid layout/arguments accepted");}
struct WorkingDirectory {fs::path original=fs::current_path();~WorkingDirectory(){fs::current_path(original);}};
}
int main(int argc,char** argv)try{
    require(argc==2,"Usage: player_data_layout_tests isolated-output");
    const auto output=fs::absolute(argv[1]);
    const auto executable=output/"Install A"/"Game.exe";
    const auto layout=portablePlayerDataLayout(executable);
    require(!fs::exists(executable.parent_path())&&!fs::exists(layout.root),"Path resolution created data directories");
    require(layout.root==output/"Install A"/"data","Portable root did not follow executable directory");
    require(layout.pack==layout.root/"assets/stunt-car-racer.scr-assets"&&layout.settings==layout.root/"settings.ini","Portable assets/settings differ");
    require(layout.careers==layout.root/"careers"&&layout.multiplayer==layout.root/"multiplayer","Saved-game roots differ");
    require(layout.originalHall.root==layout.root/"hall-of-fame/original"&&layout.enhancedHall.root==layout.root/"hall-of-fame/enhanced","Category roots are not explicit");
    require(layout.originalHall.access==HallDirectoryAccess::Inherited&&layout.enhancedHall.access==HallDirectoryAccess::Inherited,"Portable Hall requests shared ACLs");
    require(portablePlayerDataLayout(executable.parent_path()/"Renamed.exe").root==layout.root,"EXE rename changed data root");
    require(portablePlayerDataLayout(output/"Install B"/"Game.exe").root!=layout.root,"Independent game folders share a root");
    rejected([]{playerDataLayout({});});rejected([&]{playerDataLayout(output.root_path());});
    rejected([]{portablePlayerDataLayout("relative.exe");});
    fs::create_directories(output/"cwd");WorkingDirectory cwd;fs::current_path(output/"cwd");
    require(portablePlayerDataLayout(executable).root==layout.root,"Working directory changed portable storage");
    require(playerDataLayout("relative-data").root==output/"cwd/relative-data","Explicit relative root used the wrong working directory");
    require(playerDataLayout(layout.root/".."/"data").root==layout.root,"Lexical root alias changed the layout");
#ifdef _WIN32
    const auto defaultLayout=playerDataLayout(output/"default-root");
    const NativeLaunchPaths defaults{defaultLayout.pack,defaultLayout.settings,{},defaultLayout.root};
    const auto normal=parseNativeLaunchOptions(defaults,{});
    require(normal.dataLayout&&normal.paths.dataRoot==defaults.dataRoot,"Normal launches are not portable");
    const auto isolated=parseNativeLaunchOptions(defaults,{L"--data-dir",L"relative-data",L"--fullscreen",L"--windowed",L"--interpolate"});
    require(isolated.dataLayout&&isolated.paths.dataRoot==output/"cwd/relative-data","CLI root was not resolved once");
    const auto& paths=isolated.paths;
    require(paths.pack==isolated.dataLayout->pack&&paths.settings==isolated.dataLayout->settings,"Whole-root override leaked legacy paths");
    require(paths.recordings.empty()&&isolated.fullscreen==false&&isolated.interpolate==true,"CLI enabled recording or lost transient presentation overrides");
    const auto recorded=parseNativeLaunchOptions(defaults,{L"--recordings-dir",L"recordings"});
    require(recorded.paths.recordings==output/"cwd/recordings","Recording option differs");
    for(const auto* retired:{L"--asset-pack",L"--settings-file",L"--save-file",L"--import-from",L"--import-hall-from"})rejected([&]{parseNativeLaunchOptions(defaults,{retired,L"external"});});
    require(!fs::exists(paths.dataRoot),"Parsing arguments created the data root");
    rejected([&]{parseNativeLaunchOptions(defaults,{L"--unknown"});});
    rejected([&]{parseNativeLaunchOptions(defaults,{L"--data-dir"});});
    rejected([&]{parseNativeLaunchOptions(defaults,{L"--data-dir",L""});});
#endif
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"resolution_without_writes\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
