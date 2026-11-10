#pragma once
#include "track_sampling_adapter.h"
#include <algorithm>
#include <stdexcept>

namespace scr::state::raceSelection {
inline constexpr unsigned leagueContext=0x1c9d0;
inline constexpr unsigned currentDivision=0x1c9ce,practiceDivision=0x5eb7c;
inline constexpr unsigned selectedTrackId=0x1ca33,trackInDivision=0x1ca22;
inline constexpr unsigned divisionTrackTable=0x648c2,leagueTrackParameterTable=0x648ca;
inline constexpr unsigned tracksPerDivision=2,trackCount=8;

inline unsigned trackId(const memory::ChipRam& ram) noexcept { return ram[selectedTrackId]; }
inline bool superLeague(const memory::ChipRam& ram) noexcept { return ram[leagueContext]!=0; }
inline bool competitive(const memory::ChipRam& ram) noexcept { return (ram[trackInDivision]&0x80)!=0; }

inline void selectPracticeTrack(memory::ChipRam& ram,unsigned slot) {
    const unsigned ordinal=ram[practiceDivision]*tracksPerDivision+slot;
    if(slot>=tracksPerDivision||ordinal>=trackCount)throw std::runtime_error("Invalid original Practice track selection");
    ram[trackInDivision]=std::uint8_t(slot);
    ram[0x5eb7d]=1; // Remember the Practice selection for the next visit.
    ram[selectedTrackId]=ram[divisionTrackTable+ordinal];
    // Super League takes the parameter from the other slot in the same division.
    ram[0x1ca21]=ram[leagueTrackParameterTable+(ordinal^(superLeague(ram)?1u:0u))];
}

inline void preparePracticePreview(memory::ChipRam& ram,unsigned track,bool super) {
    const auto first=ram.begin()+divisionTrackTable;
    const auto end=first+trackCount;
    const auto selected=std::find(first,end,track);
    if(selected==end)throw std::logic_error("Hall track is absent from the Practice division map");
    const auto ordinal=unsigned(selected-first);

    ram[leagueContext]=super?11:0; // Native roster contexts for Super and Normal.
    ram[currentDivision]=ram[practiceDivision]=std::uint8_t(ordinal/tracksPerDivision);
    selectPracticeTrack(ram,ordinal%tracksPerDivision);
    ram[0x5eb75]=0; // Select the Practice menu text bank.
    ram[0x57c62]=0x12; // Original preview stage.
}
} // namespace scr::state::raceSelection
