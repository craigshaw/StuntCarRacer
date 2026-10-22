#pragma once
#include "state/race_restart_adapter.h"
#include "track_progress_fixture.h"
#include "physics_update_fixture.h"
#include "driving_effects_fixture.h"
#include <set>

namespace scr::test::restart {
using namespace scr::state::restart;
constexpr unsigned entry=0x605b6,end=0x6076c;
inline std::vector<unsigned> outputs() {
    std::set<unsigned> v;const auto mark=[&](unsigned a,unsigned n){for(unsigned i=0;i<n;++i)v.insert(a+i);};
    mark(0x1bccc,0x92);mark(0x1bca4,12);mark(0x1bc94,12);mark(0x1bc60,2);
    for(auto a:trackProgress::outputBytes())v.insert(a);
    for(unsigned a:{0x1bbdf,0x1bbbb,0x1bb1a,0x1bbea,0x1bbe9})v.insert(a);
    return {v.begin(),v.end()};
}
} // namespace scr::test::restart
