#include "native_cockpit_presentation.h"
#include <algorithm>

namespace scr::viewer {
namespace {
unsigned base(const NativeMemory& m){return (unsigned(m[0x6a584])<<24)|(unsigned(m[0x6a585])<<16)|(unsigned(m[0x6a586])<<8)|m[0x6a587];}
}
NativeCockpitPresentation::NativeCockpitPresentation(const NativeMemory& memory,bool fresh):memory_(std::make_unique<NativeMemory>(memory)){
    if(fresh)initializeNativeCockpitInstruments(*memory_);
}
void NativeCockpitPresentation::update(state::replay::ReplayPresentationStage stage,const NativeMemory& gameplay,const RaceTimingEffects& effects){
    auto& m=*memory_;syncNativeCockpitInstrumentInputs(m,gameplay);
    using Stage=state::replay::ReplayPresentationStage;
    switch(stage){
    case Stage::fuel:drawNativeCockpitBoostDigits(m);break;
    case Stage::speedometer:updateNativeCockpitSpeedometer(m);break;
    case Stage::distance:drawNativeCockpitDistance(m);break;
    case Stage::resultIcon:drawNativeCockpitResultIcon(m,11);break;
    case Stage::timing:
        for(unsigned n=0;n<effects.count;++n){const auto& e=effects.events[n];switch(e.kind){
        case RaceTimingEventKind::lapTimerHud:drawNativeCockpitLapTime(m,e.time,e.visibility,e.displayPosition,e.timeSlot);break;
        case RaceTimingEventKind::resultsHud:drawNativeCockpitResultIcon(m,e.value);drawNativeCockpitLapTime(m,e.time,e.visibility,e.displayPosition,e.timeSlot);break;
        case RaceTimingEventKind::lapCounterHud:{const auto saved=m[0x1bb20];m[0x1bb20]=e.value;drawNativeCockpitLapCounter(m);m[0x1bb20]=saved;break;}
        case RaceTimingEventKind::leaderIcon:drawNativeCockpitLeaderIcon(m,e.value);break;
        }}break;
    default:break;
    }
}
CockpitInstrumentPixels NativeCockpitPresentation::capture()const{
    CockpitInstrumentPixels result{};const auto& m=*memory_;const auto first=base(m);
    for(unsigned y=cockpitInstrumentTop;y<200;++y)for(unsigned x=0;x<320;++x){
        unsigned colour=0;for(unsigned p=0;p<4;++p)colour|=((m.at(first+8000*p+40*y+x/8)>>(7-x%8))&1)<<p;
        result[(y-cockpitInstrumentTop)*320+x]=std::uint8_t(colour);
    }return result;
}
void NativeCockpitPresentation::storeInitialState(NativeMemory& memory)const{
    const auto& m=*memory_;const auto first=base(m);
    for(unsigned screen=0;screen<2;++screen)for(unsigned p=0;p<4;++p){const auto offset=first+32000*screen+8000*p+40*cockpitInstrumentTop;
        std::copy_n(m.begin()+offset,40*(200-cockpitInstrumentTop),memory.begin()+offset);}
    for(unsigned a:{0x1bb8c,0x1bbde,0x1bc18,0x1bc19,0x1bc20,0x1bc21})memory[a]=m[a];
    for(unsigned a=0x5982e;a<0x59846;++a)memory[a]=m[a];
    for(unsigned a:{0x1bbbc,0x1bbd3,0x1bb6d,0x1bb6e,0x5924a})memory[a]=m[a];
}
}
