#pragma once
#include "state/steering_response_adapter.h"
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace scr::test::steering {
using namespace scr::state::steering;
constexpr unsigned entry=0x61012,end=0x6125a;
inline constexpr std::array<unsigned,5> byteOutputs{0x1bb85,0x1bbc6,0x1bb5d,0x1bb1a,0x1bb1b};
inline constexpr std::array<unsigned,5> wordOutputs{0x1bce6,0x1bc2a,0x1bc3c,0x1bbf6,0x1bcfe};
inline std::vector<unsigned> outputBytes(){
    std::vector<unsigned> v;for(auto [a,m]:configBytes){(void)m;v.push_back(a);}for(auto [a,m]:configWords){(void)m;v.push_back(a);v.push_back(a+1);}
    for(auto a:byteOutputs)v.push_back(a);for(auto a:wordOutputs){v.push_back(a);v.push_back(a+1);}return v;
}
} // namespace scr::test::steering
