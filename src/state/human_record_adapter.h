#pragma once
#include "race/race_timing.h"
#include <array>
#include <optional>

namespace scr::state {
struct HumanRaceRecord {
    std::array<char,12> name{};
    PackedLapTime total{};
    unsigned player{};
};
// Read at the timing observer, after the native routine has accumulated the
// completed lap. The running clock has already been cleared at this boundary.
inline std::optional<HumanRaceRecord> readHumanRaceRecord(const std::uint8_t* ram){
    const unsigned player=ram[0x5eb79];if(player>=12)return {};
    HumanRaceRecord result;result.player=player;
    for(unsigned n=0;n<12;++n)result.name[n]=char(ram[0x1ecab+16*player+n]);
    result.total={ram[0x1c908+12+player],ram[0x1c920+12+player],ram[0x1c938+12+player]};return result;
}
} // namespace scr::state
