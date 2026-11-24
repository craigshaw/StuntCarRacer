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
    const NativeLaunchPaths defaults{output/"old.pack",output/"old.ini",output/"old.save",{},output/"old-root"};
    const auto normal=parseNativeLaunchOptions(defaults,{});
    require(!normal.dataLayout&&normal.paths.pack==defaults.pack&&normal.paths.settings==defaults.settings&&normal.paths.save==defaults.save&&normal.paths.dataRoot==defaults.dataRoot,"Stage 1 changed normal launch defaults");
    const auto isolated=parseNativeLaunchOptions(defaults,{L"--data-dir",L"relative-data",L"--fullscreen",L"--windowed",L"--interpolate"});
    require(isolated.dataLayout.has_value()&&isolated.paths.dataRoot==output/"cwd/relative-data","CLI root was not resolved once");
    const auto& paths=isolated.paths;
    require(paths.pack==isolated.dataLayout->pack&&paths.settings==isolated.dataLayout->settings&&paths.save==isolated.dataLayout->save,"Whole-root override leaked legacy paths");
    require(paths.recordings.empty()&&isolated.fullscreen==false&&isolated.interpolate==true,"CLI enabled recording or lost transient presentation overrides");
    const auto before=parseNativeLaunchOptions(defaults,{L"--asset-pack",L"external.pack",L"--settings-file",L"external.ini",L"--save-file",L"external.save",L"--recordings-dir",L"recordings",L"--data-dir",L"data"});
    const auto after=parseNativeLaunchOptions(defaults,{L"--data-dir",L"data",L"--asset-pack",L"external.pack",L"--settings-file",L"external.ini",L"--save-file",L"external.save",L"--recordings-dir",L"recordings"});
    require(before.paths.pack==after.paths.pack&&before.paths.settings==after.paths.settings&&before.paths.save==after.paths.save&&before.paths.recordings==after.paths.recordings,"Explicit file override depends on option order");
    require(before.paths.pack==output/"cwd/external.pack"&&before.paths.settings==output/"cwd/external.ini"&&before.paths.save==output/"cwd/external.save","Explicit compatibility overrides were lost");
    require(!fs::exists(paths.dataRoot),"Parsing arguments created the data root");
    rejected([&]{parseNativeLaunchOptions(defaults,{L"--unknown"});});
    rejected([&]{parseNativeLaunchOptions(defaults,{L"--data-dir"});});
    rejected([&]{parseNativeLaunchOptions(defaults,{L"--data-dir",L""});});
#endif
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"resolution_without_writes\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
