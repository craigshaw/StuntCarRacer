#include "launcher/native_launcher.h"
#include <stdexcept>

namespace scr::runtime {
NativeLaunchOptions parseNativeLaunchOptions(const NativeLaunchPaths& defaults,const std::vector<std::wstring>& args){
    NativeLaunchOptions options;options.paths=defaults;
    std::optional<std::filesystem::path> root,recordings;
    for(std::size_t n=0;n<args.size();++n){
        const auto& key=args[n];
        if(key==L"--interpolate"){options.interpolate=true;continue;}
        if(key==L"--fullscreen"){options.fullscreen=true;continue;}
        if(key==L"--windowed"){options.fullscreen=false;continue;}
        if(key!=L"--data-dir"&&key!=L"--recordings-dir")
            throw std::invalid_argument("Unknown game argument. Use --data-dir for the complete player-data folder.");
        if(++n==args.size()||args[n].empty())throw std::invalid_argument("Missing native game argument value");
        const auto value=std::filesystem::absolute(args[n]).lexically_normal();
        if(key==L"--data-dir")root=value;
        else recordings=value;
    }
    options.dataLayout=playerDataLayout(root?*root:defaults.dataRoot);const auto& layout=*options.dataLayout;
    options.paths={layout.pack,layout.settings,{},layout.root};
    if(recordings)options.paths.recordings=*recordings;
    return options;
}
} // namespace scr::runtime
