#include "persistence/native_career_checkpoint.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace scr::runtime {
namespace {
using namespace state::checkpoint;
std::uint64_t checkpointBaselineHash(const NativeMemory& baseline){
    const auto identity=hash(baseline.data(),baseline.size());
    // These two verified mode-chooser baselines differ only in unused loader
    // code and preview help text. Keep the existing QTX save identity without
    // changing either disk's payload or accepting any other altered baseline.
    return identity==0x5bddbb6119b27c96ULL?0x885427bc82f0893cULL:identity;
}
bool allowed(NativeMenuPage page){
    switch(page){
    case NativeMenuPage::Main:case NativeMenuPage::Matchup:case NativeMenuPage::Results:
    case NativeMenuPage::Records:case NativeMenuPage::RoundResults:case NativeMenuPage::Standings:
    case NativeMenuPage::SeasonEnd:case NativeMenuPage::SeasonAward:case NativeMenuPage::Roster:return true;
    default:return false;
    }
}
void validate(const NativeCareerCheckpoint& c){
    if(!c.memory||!allowed(c.menu.page)||c.menu.division>3||(c.menu.context!=0&&c.menu.context!=11)||
       c.preparedTrack>7||(c.preparedLeague!=0&&c.preparedLeague!=11)||
       (c.finishPending&&(c.menu.page!=NativeMenuPage::Results||c.finishPixels.size()!=64000)))throw std::runtime_error("Invalid career continuation");
    const auto& m=*c.memory;
    if(m[0x5eb76]||m[0x57c3c]||m[0x1c9ce]>3||(m[0x1c9d0]!=0&&m[0x1c9d0]!=11)||m[0x1ca33]>7||
       m[0x5eb7c]>3||m[0x1bbd6]>3)throw std::runtime_error("Invalid career state");
    unsigned drivers=0;
    for(unsigned n=0;n<12;++n){const auto id=m[0x1c9c2+n];
        if(id>11||(drivers&(1u<<id))||m[0x1ecab+11*16+n]<32||m[0x1ecab+11*16+n]>126)throw std::runtime_error("Invalid career drivers");drivers|=1u<<id;}
    if(m[0x1c9cf]>10||m[0x1ca35]>5)throw std::runtime_error("Invalid career damage or round");
    for(int value:{c.menu.mainSelection,c.menu.divisionSelection,c.menu.saveSelection,c.menu.trackSelections[0],c.menu.trackSelections[1],c.menu.trackSelections[2],c.menu.trackSelections[3]})
        if(value< -1||value>3)throw std::runtime_error("Invalid career selection");
    for(int value:c.menu.trackSelections)if(value>2)throw std::runtime_error("Invalid career track selection");
    if(!c.suspendedSeason.empty()&&(c.menu.competitive||c.menu.page!=NativeMenuPage::Main||c.finishPending))throw std::runtime_error("Invalid suspended-season owner");
    if(c.enhancedRecordNotice){
        if(!c.menu.competitive||(c.menu.page!=NativeMenuPage::Results&&c.menu.page!=NativeMenuPage::Records))throw std::runtime_error("Invalid Enhanced record notice owner");
        for(const auto* line:{&c.enhancedRecordNotice->lap,&c.enhancedRecordNotice->race})if(*line){
            const auto& value=**line;
            if(normaliseHallName(value.name).display!=value.name||value.milliseconds<0||
               value.milliseconds>std::numeric_limits<std::int64_t>::max()/1000+1)throw std::runtime_error("Invalid Enhanced record notice");
        }
    }
}
NativeCareerCheckpoint decode(const NativeMemory&,const CareerBytes&,bool);
}
CareerBytes encodeNativeCheckpoint(const NativeMemory& baseline,const NativeCareerCheckpoint& c){
    validate(c);
    if(!c.suspendedSeason.empty()){
        const auto season=decode(baseline,c.suspendedSeason,false);
        if(!season.menu.competitive)throw std::runtime_error("Suspended checkpoint is not a season");
    }
    if(c.finishTiming&&!c.finishPending)throw std::runtime_error("Enhanced finish has no pending finish");
    Writer w;w.u32(c.enhancedRecordNotice?4:c.finishTiming?3:2);w.u64(checkpointBaselineHash(baseline));
    w.u32(unsigned(c.menu.page));w.u32(c.menu.competitive);w.u32(c.menu.division);w.u32(c.menu.context);
    for(int value:{c.menu.mainSelection,c.menu.divisionSelection,c.menu.saveSelection,c.menu.trackSelections[0],c.menu.trackSelections[1],c.menu.trackSelections[2],c.menu.trackSelections[3]})w.u32(unsigned(value+1));
    w.u32(c.previewPrepared);w.u32(c.preparedTrack);w.u32(c.preparedLeague);w.u32(c.finishPending);
    // Range encoding preserves every changed native byte, including both RNG
    // stores, without persisting unchanged assets or invoking a game routine.
    Writer ranges;unsigned count=0;
    for(unsigned n=0;n<baseline.size();){
        if((*c.memory)[n]==baseline[n]){++n;continue;}
        const auto first=n;while(n<baseline.size()&&(*c.memory)[n]!=baseline[n])++n;
        ranges.u32(first);ranges.u32(n-first);ranges.bytes(c.memory->data()+first,n-first);++count;
    }
    w.u32(count);w.bytes(ranges.data.data(),ranges.data.size());
    if(c.finishPending)for(auto colour:c.finishPixels)w.u32(colour);
    w.u32(std::uint32_t(c.suspendedSeason.size()));w.bytes(c.suspendedSeason.data(),c.suspendedSeason.size());
    if(c.enhancedRecordNotice)w.u32(c.finishTiming.has_value());
    if(c.finishTiming){w.u32(enhancedTimingMethodVersion);for(const auto value:{c.finishTiming->lap,c.finishTiming->record}){
        if(value.microseconds<0)throw std::runtime_error("Invalid Enhanced finish time");w.u64(std::uint64_t(value.microseconds));w.u32(value.available);w.u32(value.approximate);}}
    if(c.enhancedRecordNotice){w.u32(enhancedTimingMethodVersion);for(const auto* line:{&c.enhancedRecordNotice->lap,&c.enhancedRecordNotice->race}){
        w.u32(line->has_value());if(*line){w.text((*line)->name);w.u64(std::uint64_t((*line)->milliseconds));}}}
    return std::move(w.data);
}
namespace {
NativeCareerCheckpoint decode(const NativeMemory& baseline,const CareerBytes& bytes,bool allowSeason){
    Reader r{bytes};const auto version=r.u32();
    if((version<1||version>4)||r.u64()!=checkpointBaselineHash(baseline))throw UnsupportedNativeCheckpoint("Unsupported career checkpoint or asset pack");
    NativeCareerCheckpoint c;c.memory=std::make_unique<NativeMemory>(baseline);c.menu.page=NativeMenuPage(r.u32());
    const auto flag=[&](){const auto value=r.u32();if(value>1)throw std::runtime_error("Invalid checkpoint flag");return bool(value);};
    c.menu.competitive=flag();c.menu.division=r.u32();c.menu.context=r.u32();
    const auto selection=[&](){const auto value=r.u32();if(value>4)throw std::runtime_error("Invalid saved selection");return int(value)-1;};
    c.menu.mainSelection=selection();c.menu.divisionSelection=selection();c.menu.saveSelection=selection();
    for(auto& value:c.menu.trackSelections)value=selection();
    c.previewPrepared=flag();c.preparedTrack=r.u32();c.preparedLeague=r.u32();c.finishPending=flag();
    const auto count=r.u32();if(count>baseline.size()/2)throw std::runtime_error("Too many checkpoint ranges");
    unsigned end=0;for(unsigned n=0;n<count;++n){const auto first=r.u32(),size=r.u32();
        if(first<end||first>=baseline.size()||!size||size>baseline.size()-first)throw std::runtime_error("Invalid checkpoint range");
        r.bytes(c.memory->data()+first,size);end=first+size;
    }
    if(c.finishPending){c.finishPixels.resize(64000);for(auto& colour:c.finishPixels)colour=r.u32();}
    if(version>=2){const auto size=r.u32();if(size>2*1024*1024||(!allowSeason&&size))throw std::runtime_error("Invalid suspended-season size or nesting");
        r.require(size);c.suspendedSeason.assign(bytes.begin()+r.position,bytes.begin()+r.position+size);r.position+=size;
        if(size){const auto season=decode(baseline,c.suspendedSeason,false);if(!season.menu.competitive)throw std::runtime_error("Invalid suspended season");}
    }
    if(version>=3&&(version==3||flag())){if(!c.finishPending)throw std::runtime_error("Enhanced finish has no pending finish");
        if(r.u32()!=enhancedTimingMethodVersion)throw UnsupportedNativeCheckpoint("Unsupported Enhanced finish timing method");c.finishTiming.emplace();
        for(auto* value:{&c.finishTiming->lap,&c.finishTiming->record}){const auto time=r.u64();if(time>std::uint64_t(std::numeric_limits<std::int64_t>::max()))throw std::runtime_error("Invalid Enhanced finish time");value->microseconds=std::int64_t(time);value->available=flag();value->approximate=flag();}}
    if(version==4){
        if(r.u32()!=enhancedTimingMethodVersion)throw UnsupportedNativeCheckpoint("Unsupported Enhanced record timing method");
        c.enhancedRecordNotice.emplace();for(auto* line:{&c.enhancedRecordNotice->lap,&c.enhancedRecordNotice->race})if(flag()){
            const auto name=r.text(12);const auto time=r.u64();
            if(time>std::uint64_t(std::numeric_limits<std::int64_t>::max()))throw std::runtime_error("Invalid Enhanced record time");
            *line=EnhancedRecordLine{name,std::int64_t(time)};
        }
    }
    r.end();validate(c);return c;
}
}
NativeCareerCheckpoint decodeNativeCheckpoint(const NativeMemory& baseline,const CareerBytes& bytes){return decode(baseline,bytes,true);}
} // namespace scr::runtime
