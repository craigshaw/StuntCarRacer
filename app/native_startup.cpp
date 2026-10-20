#include "native_startup.h"
#include "local_asset_pack.h"
#include "native_race_initialization.h"
#include "track_initialization.h"
#include <algorithm>
#include <stdexcept>

namespace scr::viewer {
NativeStartupData startNativeGame(const std::filesystem::path& pack){
    auto assets=loadLocalAssetPack(pack);auto memory=std::make_unique<NativeMemory>();memory->fill(0);
    std::copy(assets.payload.begin(),assets.payload.end(),memory->begin()+0xe700);
    initializeNativeAssets(*memory);
    initializeNativeLoadState(*memory);
    initializeNativeGameData(*memory);
    return {std::move(memory),std::move(assets.payload)};
}
NativeStartupData startNativeRace(const std::filesystem::path& pack,unsigned trackId){
    if(trackId>=8||trackId==5)throw std::invalid_argument("Native driving supports static tracks0..7 except moving drawbridge5");
    auto data=startNativeGame(pack);
    initializeNativeTrack(*data.memory,trackId);
    initializeNativeRace(*data.memory);
    return data;
}
} // namespace scr::viewer
