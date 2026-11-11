#include "race/lap_timing.h"
#include <algorithm>
#include <limits>
#include <utility>

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
LapTiming::LapTiming(timing::Gate gate,bool competitive,unsigned initialRestarts)
    :gate_(gate),competitive_(competitive),restarts_(initialRestarts){}
void LapTiming::observeMovement(timing::Observation observation){
    if(stopped_)return;
    // Native recovery increments its identity once, then emits two observations.
    if(observation.restarts<restarts_){++recoveryEpoch_;recoveries_=0;}
    else {const auto count=std::uint64_t(observation.restarts-restarts_);recoveries_+=count;statistics_.recoveries+=count;}
    restarts_=observation.restarts;
    previous_=std::move(current_);current_=std::move(observation);++movementSerial_;++statistics_.movements;
    statistics_.recoveryObservations+=current_->recovery;
}
LapTimingBoundary LapTiming::boundary()const{
    const auto& now=*current_;const auto estimate=timing::world(gate_,previous_,now,true);
    LapTimingBoundary result;result.observedMicroseconds=now.us;result.crossingMicroseconds=std::max(std::int64_t(0),now.us);
    result.confidence=estimate.reason;result.recoveries=recoveries_;result.recoveryEpoch=recoveryEpoch_;result.player=now.player;
    if(estimate.reason==timing::Reason::reliable)result.crossingMicroseconds=previous_->us+estimate.offsetUs;
    if(now.player>11)result.confidence=timing::Reason::player;
    return result;
}
std::optional<LapTimingCompletion> LapTiming::observeTiming(const std::optional<HumanLapCrossing>& crossing,
    std::optional<PackedLapTime> originalTotal){
    if(stopped_)return {};
    ++statistics_.timingUpdates;
    if(!crossing||!current_||timingSerial_==movementSerial_)return {};
    timingSerial_=movementSerial_;
    if(start_&&crossing->counter==lastCounter_)return {};
    ++statistics_.crossings;
    const auto line=boundary();std::optional<LapTimingCompletion> completion;
    const bool consecutive=start_&&crossing->counter==std::uint8_t(lastCounter_+1);
    if(crossing->completedTime&&consecutive){
        completion.emplace();completion->counter=crossing->counter;completion->original=*crossing->completedTime;
        auto& enhanced=completion->enhanced;enhanced.start=*start_;enhanced.finish=line;enhanced.lap=measureEnhancedTime(*start_,line);
        ++statistics_.completions;statistics_.rankableLaps+=enhanced.lap.rankable();
        if(raceStart_&&crossing->counter==raceLaps_+2){
            ++raceLaps_;
            if(raceConfidence_==timing::Reason::reliable)raceConfidence_=enhanced.lap.confidence;
            if(competitive_&&raceLaps_==3&&crossing->counter==4&&originalTotal){
                TimedRaceResult race;race.original=*originalTotal;race.enhanced=measureEnhancedTime(*raceStart_,line);
                if(raceConfidence_!=timing::Reason::reliable)race.enhanced.confidence=raceConfidence_;
                enhanced.race=race;++statistics_.races;
            }
        }else raceStart_.reset();
    }else raceStart_.reset();
    start_=line;lastCounter_=crossing->counter;
    if(!crossing->completedTime&&crossing->counter==1){raceStart_=line;raceLaps_=0;raceConfidence_=timing::Reason::reliable;}
    return completion;
}
void LapTiming::stop()noexcept{stopped_=true;start_.reset();raceStart_.reset();}
} // namespace scr::runtime
