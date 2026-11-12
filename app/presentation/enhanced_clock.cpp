#include "presentation/enhanced_clock.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace scr::runtime {
namespace {
EnhancedClockValue value(const EnhancedTime& time){return {time.scoredMicroseconds,time.hasEstimate,!time.rankable()};}
EnhancedClockValue running(const EnhancedClockState& state,std::int64_t at){
    if(state.frozen)return state.frozenValue;
    if(state.holding&&state.held)return value(*state.held);
    if(!state.start||at<state.start->crossingMicroseconds)return {};
    auto finish=state.movement;finish.observedMicroseconds=finish.crossingMicroseconds=at;
    if(at==state.start->crossingMicroseconds&&finish.recoveries==state.start->recoveries&&finish.recoveryEpoch==state.start->recoveryEpoch)
        return {0,true,state.start->confidence!=timing::Reason::reliable};
    return value(measureEnhancedTime(*state.start,finish));
}
void best(std::optional<EnhancedTime>& held,const EnhancedTime& candidate){
    if(candidate.rankable()&&(!held||candidate.milliseconds()<held->milliseconds()))held=candidate;
}
}
void EnhancedClockPresentation::beginFrame(){before_=state_;changes_.clear();}
void EnhancedClockPresentation::key(std::int64_t at){
    if(changes_.size()>=16)throw std::logic_error("Enhanced clock frame event bound exceeded");changes_.push_back({at,state_});
}
void EnhancedClockPresentation::observeMovement(LapTimingBoundary movement){
    const bool changed=state_.movement.recoveries!=movement.recoveries||state_.movement.recoveryEpoch!=movement.recoveryEpoch||state_.movement.player!=movement.player;
    state_.movement=movement;if(changed)key(movement.observedMicroseconds);
}
void EnhancedClockPresentation::observeTiming(LapTimingBoundary movement,const LapTimingBoundary* crossing,const LapTimingCompletion* completion,bool holding,bool finished){
    if(crossing){state_.start=*crossing;
        if(completion){state_.held=completion->enhanced.lap;best(state_.bestLap,completion->enhanced.lap);
            if(completion->enhanced.race){state_.raceResult=completion->enhanced.race->enhanced;best(state_.bestRace,*state_.raceResult);}}
        state_.holding=holding&&state_.held.has_value();
        if(state_.frozen||finished){state_.frozen=true;state_.frozenValue=state_.held?value(*state_.held):EnhancedClockValue{};}
        key(crossing->crossingMicroseconds);
    }
    if(state_.holding!=(holding&&state_.held.has_value())){state_.holding=holding&&state_.held.has_value();key(movement.observedMicroseconds);}
    if(finished&&!state_.frozen){state_.frozenValue=running(state_,movement.observedMicroseconds);state_.frozen=true;key(movement.observedMicroseconds);}
}
void EnhancedClockPresentation::records(std::optional<EnhancedTime> lap,std::optional<EnhancedTime> race){
    if(lap)validateEnhancedTime(*lap);if(race)validateEnhancedTime(*race);state_.bestLap=std::move(lap);state_.bestRace=std::move(race);
}
EnhancedClockDisplay queryEnhancedClock(const EnhancedClockSnapshot& from,const EnhancedClockSnapshot* next,std::int64_t at){
    auto state=from.before;
    for(const auto& key:from.changes)if(key.microseconds<=at)state=key.state;
    if(next)for(const auto& key:next->changes)if(key.microseconds<=at)state=key.state;
    const auto record=state.frozen&&state.raceResult?state.raceResult:state.bestLap;
    return {running(state,at),record?value(*record):EnhancedClockValue{}};
}
std::string formatEnhancedClock(EnhancedClockValue time){
    if(!time.available||time.microseconds<0)return "--:--.---";
    const auto ms=time.microseconds/1000+(time.microseconds%1000>=500?1:0);std::ostringstream out;
    if(time.approximate)out<<'~';out<<ms/60000<<':'<<std::setw(2)<<std::setfill('0')<<(ms/1000)%60<<'.'<<std::setw(3)<<ms%1000;return out.str();
}
void composeEnhancedClock(CockpitInstrumentPixels& pixels,const std::array<std::uint8_t,96*8>& font,const EnhancedClockDisplay& display){
    const auto draw=[&](EnhancedClockValue value,unsigned y){const auto text=formatEnhancedClock(value);constexpr unsigned x=230,width=58;
        for(unsigned row=y;row<y+8;++row)std::fill_n(pixels.begin()+(row-cockpitInstrumentTop)*320+x,width,14);
        const auto pitch=std::min(7u,width/unsigned(text.size()));const auto left=x+width-pitch*unsigned(text.size());
        for(unsigned n=0;n<text.size();++n)for(unsigned row=0;row<8;++row){const auto ch=static_cast<unsigned char>(text[n]);
            const auto bits=ch=='~'?(row==2?0x32u:row==3?0x4cu:0u):unsigned(font[(ch-32)*8+row]);
            for(unsigned col=0;col<pitch;++col)if(bits&(128u>>(col*7/pitch)))pixels[(y+row-cockpitInstrumentTop)*320+left+n*pitch+col]=0;}
    };
    draw(display.lap,178);draw(display.record,188);
}
void composeEnhancedFinish(std::vector<std::uint32_t>& pixels,const std::array<std::uint8_t,96*8>& font,const std::array<std::uint32_t,16>& palette,const EnhancedClockDisplay& display){
    if(pixels.size()!=64000)throw std::invalid_argument("Enhanced finish requires the original saved canvas");
    CockpitInstrumentPixels instruments{};composeEnhancedClock(instruments,font,display);
    for(unsigned top:{178u,188u})for(unsigned y=top;y<top+8;++y)for(unsigned x=230;x<288;++x)pixels[y*320+x]=palette[instruments[(y-cockpitInstrumentTop)*320+x]];
}
} // namespace scr::runtime
