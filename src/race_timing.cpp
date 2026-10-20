#include "race_timing.h"
#include <stdexcept>

namespace scr {
namespace {
struct DecimalResult { std::uint8_t value;bool carry; };
DecimalResult addDecimal(std::uint8_t a,std::uint8_t b,bool carry=false) noexcept {
    unsigned low=(a&15u)+(b&15u)+(carry?1u:0u);
    if(low>9)low+=6;
    unsigned sum=(a&0xf0u)+(b&0xf0u)+low;
    const bool overflow=sum>0x99;
    if(overflow)sum+=0x60;
    return {static_cast<std::uint8_t>(sum),overflow};
}
void event(RaceTimingEffects& e,RaceTimingEventKind kind,std::uint8_t value) {
    if(e.count>=e.events.size())throw std::logic_error("Race timing event bound exceeded");
    e.events[e.count++]={kind,value};
}
void timerHud(RaceTimingState& s,RaceTimingEffects& e) {
    s.renderingIndex=0;s.subsecondVisibility=s.lapDisplayDuration?0x80:0;
    event(e,RaceTimingEventKind::lapTimerHud,s.subsecondVisibility);
    auto& draw=e.events[e.count-1];draw.time=s.times[0];draw.visibility=s.subsecondVisibility;
    draw.displayPosition=2;draw.timeSlot=0;
}
void resultsHud(RaceTimingState& s,RaceTimingEffects& e) {
    s.resultsIcon=s.winnerBits?11:7;s.renderingIndex=2;s.subsecondVisibility=0x80;
    event(e,RaceTimingEventKind::resultsHud,s.resultsIcon);
    auto& draw=e.events[e.count-1];draw.time=s.times[2];draw.visibility=0x80;
    draw.displayPosition=3;draw.timeSlot=2;
}
void increment(std::uint8_t delta,unsigned player,RaceTimingState& s,RaceTimingEffects& e) {
    auto& time=s.times[player];const auto sub=addDecimal(time.subseconds,delta);time.subseconds=sub.value;
    if(!sub.carry)return;
    time.seconds=addDecimal(time.seconds,0,true).value;
    if(time.seconds>=0x60){
        time.seconds=0;const auto minute=addDecimal(time.minutes,1).value;
        // Original CMP.B/BGE is signed, including invalid BCD input bytes.
        if(minute<10 || (minute&0x80))time.minutes=minute;
    }
    if(!player&&!s.lapDisplayDuration&&s.lapCounter[0])timerHud(s,e);
}
bool compareCopy(unsigned source,unsigned destination,RaceTimingState& s) noexcept {
    const auto& a=s.times[source];const auto& b=s.times[destination];
    const bool better=a.minutes<b.minutes || (a.minutes==b.minutes&&(a.seconds<b.seconds || (a.seconds==b.seconds&&a.subseconds<b.subseconds)));
    if(better)s.times[destination]=a;
    return better;
}
void addTimes(unsigned source,unsigned destination,RaceTimingState& s) noexcept {
    // Read each component after the preceding write, preserving possible alias.
    auto value=addDecimal(s.times[source].subseconds,s.times[destination].subseconds);
    s.times[destination].subseconds=value.value;
    value=addDecimal(s.times[source].seconds,s.times[destination].seconds,value.carry);
    const bool minuteCarry=value.carry||value.value>=0x60;
    if(minuteCarry){
        // SBCD #$60: decimal correction also applies to invalid low nibbles.
        unsigned low=value.value&15u;if(low>9)low-=6;
        unsigned corrected=low+(value.value&0xf0u)-0x60u;
        if(corrected>0x99)corrected+=0xa0;
        value.value=static_cast<std::uint8_t>(corrected);
    }
    s.times[destination].seconds=value.value;
    s.times[destination].minutes=addDecimal(s.times[source].minutes,s.times[destination].minutes,minuteCarry).value;
}
void copyName(const RaceTimingInputs& in,RaceTimingState& s,bool race) noexcept {
    auto& name=race?s.raceRecordName:s.lapRecordName;
    const unsigned start=(unsigned(in.currentPlayerId)*16)&255;
    for(unsigned n=0;n<name.size();++n)name[n]=in.playerNames[(start+n)&255];
    s.productLowScratch=0;
}
void records(const RaceTimingInputs& in,RaceTimingState& s) noexcept {
    if(compareCopy(0,12,s))s.bestLapHolder=in.currentPlayerId;
    if(compareCopy(0,14,s)){copyName(in,s,false);s.displayFlags|=0x41;}
    compareCopy(0,in.currentPlayerId,s);
    const unsigned total=unsigned(in.currentPlayerId)+12;addTimes(0,total,s);
    if(s.lapCounter[0]!=4)return;
    if(compareCopy(total,13,s))s.bestRaceHolder=in.currentPlayerId;
    if(compareCopy(total,15,s)){copyName(in,s,true);s.displayFlags|=0x81;}
}
void crossing(unsigned player,const RaceTimingInputs& in,RaceTimingState& s,RaceTimingEffects& e) {
    if(!player&&(in.offTrackStateFlags&0x40))return;
    if(s.crossingArmed[player]&0x80){
        if(in.segmentIndex[player]==in.finishLineSegment)s.crossingArmed[player]=0;
        return;
    }
    if(in.segmentIndex[player]!=in.lapLineSegment)return;
    s.crossingArmed[player]=0x80;++s.lapCounter[player];
    if(s.lapCounter[player]!=1 && !(in.trackProgress[player]&0xff00)){
        s.multiplierScratch=static_cast<std::uint8_t>(in.trackProgress[player]^0xff);
        const unsigned product=unsigned(s.multiplierScratch)*13;
        s.productLowScratch=static_cast<std::uint8_t>(product);
        auto correction=static_cast<std::uint8_t>(product>>8);if(correction>=10)correction+=6;
        increment(correction,player,s,e);
    }
    // togglePlayerContext returns immediately in the supported local mode.
    if(!player){
        if(s.lapCounter[0]!=1){records(in,s);s.lapDisplayDuration=0x1b;timerHud(s,e);s.networkTimeBuffer=s.times[0];}
        s.savedPlayerScratch=0;event(e,RaceTimingEventKind::lapCounterHud,s.lapCounter[0]);
    }
    if(s.lapCounter[player]!=1&&compareCopy(player,2,s)){
        s.winnerBits=player?0x40:0;
        if(s.winnerBits){s.lapDisplayDuration=0;resultsHud(s,e);}
    }
    s.times[player]={};
}
bool opponentBehind(const RaceTimingInputs& in,const RaceTimingState& s) noexcept {
    auto difference=static_cast<std::uint8_t>(s.lapCounter[1]-s.lapCounter[0]);
    if(difference)return (difference&0x80)!=0;
    auto first=static_cast<std::uint8_t>(in.segmentIndex[0]-in.lapLineSegment);
    if(in.segmentIndex[0]<in.lapLineSegment)first=static_cast<std::uint8_t>(first+in.numberOfTrackSegments);
    auto second=static_cast<std::uint8_t>(in.segmentIndex[1]-in.lapLineSegment);
    if(in.segmentIndex[1]<in.lapLineSegment)second=static_cast<std::uint8_t>(second+in.numberOfTrackSegments);
    difference=static_cast<std::uint8_t>(second-first);
    if(difference)return (difference&0x80)!=0;
    return (in.playerDistanceDifference&0x8000)!=0;
}
void completion(const RaceTimingInputs& in,RaceTimingState& s,RaceTimingEffects& e) {
    for(unsigned n=0;n<2;++n){const unsigned player=1-n;
        if(!s.raceComplete&&s.lapCounter[player]==in.maximumLaps){
            s.raceComplete=s.lapCounter[player];if(!s.blinkCountdown)s.blinkCountdown=0x2c;
            s.leaderIcon=opponentBehind(in,s)?7:11;event(e,RaceTimingEventKind::leaderIcon,s.leaderIcon);
            auto message=static_cast<std::uint8_t>((in.selectedTrackInDivision&0x80)?8:0x54);
            if(s.leaderIcon==11){s.outcomeFlags=0x80;message=0x1c;}
            s.messageActive=0x80;s.messageMode=4;s.messageIndex=message;
        }
        s.outcomeFlags=static_cast<std::uint8_t>((s.outcomeFlags&0xbf)|s.winnerBits);
    }
}
} // namespace
void updateBlinkCountdown(const RaceTimingInputs& in,RaceTimingState& s) noexcept {
    if(!s.blinkCountdown)return;
    if(!(s.blinkCountdown&0x80)){
        s.blinkFlag=(s.blinkCountdown>>2)&1;
        if(!in.raceStartTimer&&!in.wheelMovementActive&&s.blinkCountdown<6)return;
        if(s.throttleFlag&0x80)return;
        if(--s.blinkCountdown)return;
    }
    s.frameBufferSyncMask=s.blinkCountdown=0x80;
}
RaceTimingEffects incrementLapClock(std::uint8_t delta,unsigned player,RaceTimingState& s) {
    if(player>1)throw std::out_of_range("Lap clock player must be zero or one");
    RaceTimingEffects effects{};increment(delta,player,s,effects);return effects;
}
RaceTimingEffects updateRaceTiming(const RaceTimingInputs& in,RaceTimingState& s) {
    if(in.currentPlayerId>11)throw std::out_of_range("Race record player must be 0..11");
    RaceTimingEffects e{};++s.globalFrameCounter;
    const unsigned throttle=unsigned(s.throttleAccumulator)+0xee;
    s.throttleAccumulator=static_cast<std::uint8_t>(throttle);s.throttleFlag=throttle>255?0:0xff;
    if(s.lapDisplayDuration&&!--s.lapDisplayDuration)resultsHud(s,e);
    if(in.selectedTrackInDivision&0x80){increment(0x13,1,s,e);crossing(1,in,s,e);}
    increment(0x13,0,s,e);s.networkTimeBuffer=s.times[0];crossing(0,in,s,e);completion(in,s,e);
    // Original negative flag executes this routine twice (JSR plus fallthrough).
    if(in.raceCompletionCheckFlag&0x80)updateBlinkCountdown(in,s);
    updateBlinkCountdown(in,s);return e;
}
} // namespace scr
