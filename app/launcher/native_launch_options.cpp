#include "launcher/native_launcher.h"
#include <stdexcept>

namespace scr::runtime {
NativeLaunchOptions parseNativeLaunchOptions(const NativeLaunchPaths& defaults,const std::vector<std::wstring>& args){
    NativeLaunchOptions options;options.paths=defaults;
    std::optional<std::filesystem::path> root,pack,settings,save,recordings;
    for(std::size_t n=0;n<args.size();++n){
        const auto& key=args[n];
        if(key==L"--interpolate"){options.interpolate=true;continue;}
        if(key==L"--fullscreen"){options.fullscreen=true;continue;}
        if(key==L"--windowed"){options.fullscreen=false;continue;}
        if(key!=L"--data-dir"&&key!=L"--asset-pack"&&key!=L"--settings-file"&&key!=L"--save-file"&&key!=L"--recordings-dir")
            throw std::invalid_argument("Unknown native game argument");
        if(++n==args.size()||args[n].empty())throw std::invalid_argument("Missing native game argument value");
        const auto value=std::filesystem::absolute(args[n]).lexically_normal();
        if(key==L"--data-dir")root=value;
        else if(key==L"--asset-pack")pack=value;
        else if(key==L"--settings-file")settings=value;
        else if(key==L"--save-file")save=value;
        else recordings=value;
    }
    if(root){
        options.dataLayout=playerDataLayout(*root);const auto& layout=*options.dataLayout;
        options.paths={layout.pack,layout.settings,layout.save,{},layout.root};
    }
    if(pack)options.paths.pack=*pack;
    if(settings)options.paths.settings=*settings;
    if(save)options.paths.save=*save;
    if(recordings)options.paths.recordings=*recordings;
    return options;
}
} // namespace scr::runtime
