#pragma once
#include "state/track_sampling_adapter.h"
#include <array>
#include <cstdint>
#include <vector>

namespace scr_test {
using namespace scr::state::memory;
inline const std::vector<unsigned>& trackSamplingOutputBytes() {
    static const std::vector<unsigned> result=[] {
        std::vector<unsigned> v;
        for(unsigned a:{0x1bb4d,0x1bb59,0x1bb5a,0x1bb65,0x1bb6a,0x1bb79,0x1bb7b,
                        0x1bb85,0x1bb86,0x1bb97,0x1bb98,0x1bb9a,0x1bba1,0x1bba3,
                        0x1bbd4,0x1bbd9,0x1bbda,0x1bbdc,0x1bbf9,0x1bc32,0x1bc44,0x1bc4a,0x1bc4d}) v.push_back(a);
        for(unsigned a:{0x1bc02,0x1bc04,0x1bc06,0x1bc08,0x1bc0e,0x1bc10,0x1bc22,0x1bc40,0x1bc8c,0x1bc90,0x1bcbc})
            for(unsigned j=0;j<2;++j) v.push_back(a+j);
        for(unsigned a:{0x1bb18,0x1bca4,0x1bca8,0x1bcac}) for(unsigned j=0;j<4;++j) v.push_back(a+j);
        return v;
    }(); return result;
}
} // namespace scr_test
