#pragma once
#include "assets/native_asset_initialization.h"
#include "state/native_checkpoint_io.h"

namespace scr::runtime {
inline std::uint64_t nativeCheckpointBaselineIdentity(const NativeMemory& baseline){
    const auto identity=state::checkpoint::hash(baseline.data(),baseline.size());
    // These verified mode-chooser baselines differ only in unused loader code
    // and preview help text. Retain the existing QTX career identity.
    return identity==0x5bddbb6119b27c96ULL?0x885427bc82f0893cULL:identity;
}
} // namespace scr::runtime
