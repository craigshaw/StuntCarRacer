#pragma once
// Development reference for comparing the legacy ghost interpolation.
#include "race/crossing_timing.h"

namespace scr::tools::timing {
inline std::optional<std::int64_t> progress(const scr::timing::Gate& g,const std::optional<scr::timing::Observation>& previous,const scr::timing::Observation& now){
    if(!previous||!g.valid)return {};
    const auto& before=*previous;
    // Existing ghost boundary conditions, with integer rounding for comparison.
    if(now.recovery||before.restarts!=now.restarts||before.segment!=g.previous||now.segment!=g.lap||
       before.progress>2048||now.progress>=2048||now.us<=before.us)return {};
    const auto a=2048-before.progress,total=a+now.progress;
    if(!total)return {};
    return scr::timing::divideNearest((now.us-before.us)*a,total);
}
} // namespace scr::tools::timing
