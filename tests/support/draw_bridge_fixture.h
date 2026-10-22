#pragma once
#include "state/draw_bridge_adapter.h"
#include <algorithm>
#include <vector>

namespace scr::test::drawBridge {
using namespace scr::state::drawBridge;
constexpr unsigned entry=0x5a794,end=0x5a996;
inline std::vector<unsigned> outputs(const Ram& r){
    std::vector<unsigned> result;const auto range=[&](unsigned first,unsigned count){for(unsigned i=0;i<count;++i)result.push_back(first+i);};
    for(unsigned a:{0x1bbb0U,0x1bb3fU,0x1bb71U,0x1bb1bU})range(a,1);
    for(unsigned a:{0x1bc2aU,0x1bbf6U,0x1bcc0U})range(a,2);
    range(0x1c982,5);range(geometryAddress(readWord(r,0x1f060)),0x48);
    const auto cache=readLong(r,0x7aae6);if(cache&&cache+0x54<=r.size())for(auto offset:cacheOffsets)range(cache+offset,2);
    return result;
}
} // namespace scr::test::drawBridge
