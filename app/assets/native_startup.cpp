#include "assets/native_startup.h"
#include "assets/local_asset_pack.h"
#include "race/native_race_initialization.h"
#include "track/track_initialization.h"
#include "state/draw_bridge_adapter.h"
#include <algorithm>
#include <stdexcept>

namespace scr::runtime {
NativeStartupData startNativeGame(const std::filesystem::path& pack){
    auto assets=loadLocalAssetPack(pack);auto memory=std::make_unique<NativeMemory>();memory->fill(0);
    std::copy(assets.payload.begin(),assets.payload.end(),memory->begin()+0xe700);
    initializeNativeAssets(*memory);
    initializeNativeLoadState(*memory);
    initializeNativeGameData(*memory);
    return {std::move(memory),std::move(assets.payload)};
}
NativeStartupData startNativeRace(const std::filesystem::path& pack,unsigned trackId){
    if(trackId>=8)throw std::invalid_argument("Native track ID must be0..7");
    auto data=startNativeGame(pack);
    initializeNativeTrack(*data.memory,trackId);
    if(state::drawBridge::animate(*data.memory).geometryUpdated)rebuildNativeTrackGeometryCache(*data.memory);
    initializeNativeRace(*data.memory);
    return data;
}
} // namespace scr::runtime
