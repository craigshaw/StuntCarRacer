#include "race/lap_recording.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace scr::runtime {
namespace {
constexpr std::int64_t fadeMicroseconds=240000;
unsigned decimal(std::uint8_t value){
    if((value&15)>9||(value>>4)>9)throw std::invalid_argument("Invalid ghost lap BCD time");
    return (value>>4)*10+(value&15);
}
struct Sampled {RenderCameraInputs pose;const GhostLapSample* discrete;double opacity;};
Sampled sample(const GhostLap& lap,std::int64_t elapsed,bool smooth,const TrigTable& trig){
    const auto at=std::min(elapsed,lap.durationMicroseconds);
    const auto next=std::upper_bound(lap.samples.begin(),lap.samples.end(),at,
        [](auto t,const GhostLapSample& s){return t<s.microseconds;});
    const auto& a=next==lap.samples.begin()?lap.samples.front():*(next-1);
    auto pose=a.pose;
    if(smooth&&next!=lap.samples.end()&&!next->cut&&at>a.microseconds){
        PresentationPose from,to;from.pose=a.pose;to.pose=next->pose;
        from.camera=makeRenderProjectionCamera(from.pose,trig);to.camera=makeRenderProjectionCamera(to.pose,trig);
        pose=interpolatePresentation(from,to,double(at-a.microseconds)/(next->microseconds-a.microseconds),trig).pose;
    }
    const auto opacity=elapsed<=lap.durationMicroseconds?1.0:
        std::clamp(1-double(elapsed-lap.durationMicroseconds)/fadeMicroseconds,0.0,1.0);
    return {pose,&a,opacity};
}
}
bool GhostLapKey::operator<(const GhostLapKey& other)const noexcept{
    return std::tie(career,track,superLeague)<std::tie(other.career,other.track,other.superLeague);
}
unsigned ghostLapHundredths(PackedLapTime time){
    const auto minutes=decimal(time.minutes),seconds=decimal(time.seconds),sub=decimal(time.subseconds);
    if(minutes>9||seconds>=60)throw std::invalid_argument("Ghost lap time outside original clock");
    return (minutes*60+seconds)*100+sub;
}
std::shared_ptr<const GhostLap> PersonalBestLaps::best(const GhostLapKey& key)const{
    const auto found=bests_.find(key);return found==bests_.end()?nullptr:found->second;
}
void PersonalBestLaps::forgetCareer(const std::string& career){
    for(auto it=bests_.begin();it!=bests_.end();)if(it->first.career==career)it=bests_.erase(it);else ++it;
}
bool PersonalBestLaps::accept(GhostLap lap){
    const auto old=best(lap.context.key);
    if(old&&old->hundredths<=lap.hundredths)return false;
    const auto key=lap.context.key;bests_[key]=std::make_shared<const GhostLap>(std::move(lap));return true;
}
LapRecorder::LapRecorder(PersonalBestLaps& bests,GhostLapContext context,const TrigTable& trig):
    bests_(bests),context_(std::move(context)),trig_(trig){
    if(context_.key.career.empty()||context_.key.track>=8)throw std::invalid_argument("Invalid lap recording context");
}
void LapRecorder::append(const LapMovementObservation& movement,bool cut){
    if(!candidate_||overflowed_)return;
    auto& lap=*candidate_;
    if(lap.samples.size()>=maxGhostLapSamples){candidate_.reset();overflowed_=true;++statistics_.overflowCandidates;return;}
    std::uint16_t bridge=noGhostBridge;
    if(movement.bridge){
        const auto found=std::find(lap.bridges.begin(),lap.bridges.end(),*movement.bridge);
        if(found==lap.bridges.end()){
            if(lap.bridges.size()>=maxGhostLapBridgeShapes){candidate_.reset();overflowed_=true;++statistics_.overflowCandidates;return;}
            bridge=std::uint16_t(lap.bridges.size());lap.bridges.push_back(*movement.bridge);
        }else bridge=std::uint16_t(found-lap.bridges.begin());
    }
    const auto region=context_.key.track==5?region_.observe(movement.segment,movement.grounded,cut):GhostBridgeRegion::outside;
    lap.samples.push_back({movement.microseconds-lap.start.alignedMicroseconds,movement.pose,bridge,region,std::uint8_t(movement.segment),cut});
    if(movement.microseconds>=lap.start.alignedMicroseconds)lap.controlPresets|=movement.preset==DriveControlPreset::Modern?2u:1u;
}
void LapRecorder::observeMovement(LapMovementObservation movement){
    if(stopped_)return;
    if(movement.microseconds<0||(current_&&movement.microseconds<current_->microseconds)||movement.segment>=movement.segmentCount||movement.segmentCount>100)
        throw std::invalid_argument("Invalid lap movement observation");
    previous_=std::move(current_);current_=std::move(movement);++statistics_.movements;
    const bool cut=previous_&&previous_->restarts!=current_->restarts;
    append(*current_,cut);compatibility();
}
GhostLapBoundary LapRecorder::boundary(bool first)const{
    const auto& now=*current_;GhostLapBoundary result;
    result.observedMicroseconds=result.alignedMicroseconds=now.microseconds;
    result.progress=std::uint16_t(now.progress);result.recovery=now.recovery;
    if(!first&&now.progress<256)result.correctionHundredths=std::uint8_t(((now.progress^255)*13)>>8);
    // Interpolate the physical segment boundary only when adjacent, forward
    // observations bracket it. The game's decimal correction remains separate.
    if(previous_&&!now.recovery&&previous_->restarts==now.restarts&&now.segment==now.lapSegment&&
       previous_->segment==(now.lapSegment+now.segmentCount-1)%now.segmentCount&&
       previous_->progress<=previous_->segmentLength&&now.progress<now.segmentLength&&now.microseconds>previous_->microseconds){
        const auto before=previous_->segmentLength-previous_->progress,total=before+now.progress;
        if(total){result.alignedMicroseconds=previous_->microseconds+std::llround(double(now.microseconds-previous_->microseconds)*before/total);result.interpolated=true;}
    }
    return result;
}
void LapRecorder::observeTiming(const RaceTimingEffects& effects){
    if(stopped_||!current_)return;
    ++statistics_.timingUpdates;statistics_.recoveryTimingUpdates+=current_->recovery;
    const RaceTimingEvent* crossing=nullptr;const RaceTimingEvent* completed=nullptr;
    for(unsigned n=0;n<effects.count;++n){const auto& event=effects.events[n];
        if(event.kind==RaceTimingEventKind::lapCounterHud)crossing=&event;
        if(event.kind==RaceTimingEventKind::lapTimerHud&&event.visibility==0x80&&event.timeSlot==0)completed=&event;
    }
    if(!crossing)return;
    const bool first=crossing->value==1;const auto line=boundary(first);
    if(!first){
        ++statistics_.completions;
        if(candidate_&&completed&&line.alignedMicroseconds>candidate_->start.alignedMicroseconds){
            auto lap=std::move(*candidate_);lap.finish=line;lap.durationMicroseconds=line.alignedMicroseconds-lap.start.alignedMicroseconds;
            lap.completedTime=completed->time;lap.hundredths=ghostLapHundredths(completed->time);
            ++statistics_.recordedLaps;
            if(bests_.accept(std::move(lap)))++statistics_.improvements;
        }
    }
    candidate_.emplace();candidate_->context=context_;candidate_->start=line;overflowed_=false;region_=GhostBridgeRegionTracker{};
    if(previous_)append(*previous_,false);
    append(*current_,previous_&&previous_->restarts!=current_->restarts);++statistics_.starts;
    Epoch epoch;epoch.start=line.alignedMicroseconds;
    if(context_.source==GhostLapSource::Practice)epoch.target=bests_.best(context_.key);
    epochs_.push_back(std::move(epoch));while(epochs_.size()>4)epochs_.pop_front();compatibility();
}
void LapRecorder::compatibility(){
    if(epochs_.empty()||!epochs_.back().target||!current_||context_.key.track!=5)return;
    auto& epoch=epochs_.back();const auto elapsed=current_->microseconds-epoch.start;
    if(elapsed<0||elapsed>epoch.target->durationMicroseconds)return;
    const auto source=sample(*epoch.target,elapsed,false,trig_);
    const bool matches=source.discrete->bridge!=noGhostBridge&&current_->bridge&&
        epoch.target->bridges.at(source.discrete->bridge)==*current_->bridge;
    const bool cut=source.discrete->cut&&epoch.lastCut!=source.discrete->microseconds;
    if(cut)epoch.lastCut=source.discrete->microseconds;
    epoch.visibility.observe(current_->microseconds/1000000.0,source.discrete->region,matches,cut);
}
void LapRecorder::stop(){candidate_.reset();stopped_=true;}
std::optional<GhostLapPlayback> LapRecorder::playback(std::int64_t absolute,bool interpolate)const{
    const auto next=std::upper_bound(epochs_.begin(),epochs_.end(),absolute,[](auto t,const Epoch& e){return t<e.start;});
    if(next==epochs_.begin())return std::nullopt;
    const auto& epoch=*(next-1);if(!epoch.target)return std::nullopt;
    const auto elapsed=absolute-epoch.start;const auto source=sample(*epoch.target,elapsed,interpolate,trig_);
    return GhostLapPlayback{makeGhostCarPose(source.pose),source.opacity*epoch.visibility.alphaAt(absolute/1000000.0),
        epoch.visibility.conditionsDifferAt(absolute/1000000.0),elapsed>epoch.target->durationMicroseconds,elapsed,epoch.target};
}
} // namespace scr::runtime
