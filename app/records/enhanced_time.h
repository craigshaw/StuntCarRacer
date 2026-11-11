#pragma once
#include "race/crossing_timing.h"

namespace scr::runtime {
enum class TimingCategory {Original,Enhanced};
constexpr std::uint32_t enhancedTimingMethodVersion=1;
constexpr std::int64_t recoveryPenaltyMicroseconds=260000;
struct LapTimingBoundary {
    std::int64_t observedMicroseconds{},crossingMicroseconds{};
    std::uint64_t recoveries{},recoveryEpoch{};
    unsigned player{};
    timing::Reason confidence{timing::Reason::missingPrevious};
};
struct EnhancedTime {
    std::uint32_t methodVersion{enhancedTimingMethodVersion};
    std::int64_t elapsedMicroseconds{},penaltyMicroseconds{},scoredMicroseconds{};
    std::uint64_t recoveries{};
    timing::Reason confidence{timing::Reason::missingPrevious};
    bool hasEstimate{};
    bool rankable()const noexcept{return hasEstimate&&confidence==timing::Reason::reliable;}
    std::int64_t milliseconds()const noexcept{return scoredMicroseconds/1000+(scoredMicroseconds%1000>=500?1:0);}
};
EnhancedTime measureEnhancedTime(const LapTimingBoundary& start,const LapTimingBoundary& finish)noexcept;
void validateEnhancedTime(const EnhancedTime&);
bool sameEnhancedTime(const EnhancedTime&,const EnhancedTime&)noexcept;
} // namespace scr::runtime
