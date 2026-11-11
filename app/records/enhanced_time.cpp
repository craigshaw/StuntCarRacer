#include "records/enhanced_time.h"
#include <limits>
#include <stdexcept>

namespace scr::runtime {
EnhancedTime measureEnhancedTime(const LapTimingBoundary& start,const LapTimingBoundary& finish)noexcept{
    using timing::Reason;
    EnhancedTime result;result.confidence=start.confidence==Reason::reliable?finish.confidence:start.confidence;
    if(start.player>11||start.player!=finish.player)result.confidence=Reason::player;
    if(start.crossingMicroseconds<0||finish.crossingMicroseconds<=start.crossingMicroseconds){result.confidence=Reason::interval;return result;}
    result.elapsedMicroseconds=finish.crossingMicroseconds-start.crossingMicroseconds;
    if(start.recoveryEpoch!=finish.recoveryEpoch||finish.recoveries<start.recoveries){result.confidence=Reason::penaltySequence;return result;}
    result.recoveries=finish.recoveries-start.recoveries;
    constexpr auto maximum=std::numeric_limits<std::int64_t>::max();
    if(result.recoveries>std::uint64_t(maximum/recoveryPenaltyMicroseconds)){result.confidence=Reason::arithmetic;return result;}
    result.penaltyMicroseconds=std::int64_t(result.recoveries)*recoveryPenaltyMicroseconds;
    if(result.elapsedMicroseconds>maximum-result.penaltyMicroseconds){result.confidence=Reason::arithmetic;return result;}
    result.scoredMicroseconds=result.elapsedMicroseconds+result.penaltyMicroseconds;result.hasEstimate=true;return result;
}
void validateEnhancedTime(const EnhancedTime& time){
    constexpr auto maximum=std::numeric_limits<std::int64_t>::max();
    if(time.methodVersion!=enhancedTimingMethodVersion||!time.rankable()||time.elapsedMicroseconds<=0||
       time.recoveries>std::uint64_t(maximum/recoveryPenaltyMicroseconds)||
       time.penaltyMicroseconds!=std::int64_t(time.recoveries)*recoveryPenaltyMicroseconds||
       time.elapsedMicroseconds>maximum-time.penaltyMicroseconds||
       time.scoredMicroseconds!=time.elapsedMicroseconds+time.penaltyMicroseconds)
        throw std::invalid_argument("Invalid ranked Enhanced time");
}
bool sameEnhancedTime(const EnhancedTime& a,const EnhancedTime& b)noexcept{
    return a.methodVersion==b.methodVersion&&a.elapsedMicroseconds==b.elapsedMicroseconds&&
        a.penaltyMicroseconds==b.penaltyMicroseconds&&a.scoredMicroseconds==b.scoredMicroseconds&&
        a.recoveries==b.recoveries&&a.confidence==b.confidence&&a.hasEstimate==b.hasEstimate;
}
} // namespace scr::runtime
