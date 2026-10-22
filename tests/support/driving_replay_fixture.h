#pragma once
#include "state/native_race.h"
#include "physics_update_fixture.h"
#include "player_controls_fixture.h"
#include "track_progress_fixture.h"
#include "race_timing_fixture.h"
#include "driving_effects_fixture.h"
#include "race_transitions_fixture.h"
#include "race_restart_fixture.h"
#include "draw_bridge_fixture.h"
#include "opponent_driving_fixture.h"
#include "opponent_collision_fixture.h"
#include <set>
#include <functional>

namespace scr::test::replay {
using namespace scr::state::replay;
inline std::vector<unsigned> transitionOutputs() {
    std::set<unsigned> v;auto mark=[&](unsigned a,unsigned n){while(n--)v.insert(a++);};
    for(unsigned a:{0x1bb6f,0x1bbc8,0x1bb41,0x1bb6c,0x1bb75,0x1bbb4,0x1bb63,0x57c5b,0x1bba4,0x1bbb9,0x1bb8e,0x5e65a,0x5e65b,0x1bb8c,0x5dabe,0x616d8,0x1bcc8,0x1b816})mark(a,1);
    mark(0x1bbf4,2);mark(0x1bc62,2);mark(0x6a584,16);mark(0x1b812,4);
    return {v.begin(),v.end()};
}
inline std::vector<unsigned> physicsOutputs() {
    std::set<unsigned> values;auto mark=[&](unsigned a,unsigned n){for(unsigned i=0;i<n;++i)values.insert(a+i);};
    mark(0x1c230,72);mark(0x1bd02,12);mark(0x1bc94,12);mark(0x1bbf6,2);
    for(auto a:motion::outputWords)mark(a,2);
    for(auto a:wheelMotion::speedOutputs)mark(a,2);for(auto a:wheelMotion::dampingOutputs)mark(a,2);
    for(auto a:scr_test::trackSamplingOutputBytes())mark(a,1);
    for(auto r:suspension::observedRanges)mark(r.begin,r.end-r.begin);
    for(auto r:integration::stateRanges)mark(r.begin,r.end-r.begin);
    for(auto a:effects::stateWords)mark(a,2);for(auto a:effects::stateBytes)mark(a,1);mark(effects::seedAddress,4);
    for(auto a:steering::outputBytes())mark(a,1);
    for(auto a:carDynamics::wordOutputs)mark(a,2);mark(carDynamics::byteOutput,1);
    for(unsigned i=0;i<4;++i)mark(audio::channels+4*i,2);mark(audio::guard,2);
    // The suspension adapter's enclosing observed range also contains two
    // retained renderer bytes. Physics neither reads nor writes them. Original
    // rendering updates them between ticks, while this gameplay mirror keeps
    // its own presentation state. 1bb7c: writers66ff0/6704a/670c0, readers670c6/
    // 670f0. 1bb7f: writers64e2e/65036, readers6587a/65960. Captured races often
    // already held their eventual values; fresh startup exposes the difference.
    values.erase(0x1bb7c);values.erase(0x1bb7f);
    return {values.begin(),values.end()};
}
inline std::vector<unsigned> timingOutputs() {
    std::set<unsigned> v(timing::byteOutputs.begin(),timing::byteOutputs.end());
    for(auto r:timing::arrayOutputs)for(unsigned a=r.begin;a<r.end;++a)v.insert(a);
    // Renderer-owned scratch is retained on timing paths that do not use it.
    // Callers initialize the multiplier before use; its helper overwrites the
    // product. Icon and visibility bytes are HUD output only.
    for(unsigned a:{0x1bb1a,0x1bb1b,0x1bbe4,0x1bbe8,0x1bb8c,0x1bbde,0x1bb89})v.erase(a);
    return {v.begin(),v.end()};
}
} // namespace scr::test::replay
