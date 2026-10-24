#include "native_career_checkpoint.h"
#include <algorithm>
#include <stdexcept>

namespace scr::viewer {
namespace {
using namespace state::checkpoint;
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
}
NativeCareerCheckpoint decode(const NativeMemory&,const CareerBytes&,bool);
}
CareerBytes encodeNativeCheckpoint(const NativeMemory& baseline,const NativeCareerCheckpoint& c){
    validate(c);
    if(!c.suspendedSeason.empty()){
        const auto season=decode(baseline,c.suspendedSeason,false);
        if(!season.menu.competitive)throw std::runtime_error("Suspended checkpoint is not a season");
    }
    Writer w;w.u32(2);w.u64(hash(baseline.data(),baseline.size()));
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
    return std::move(w.data);
}
namespace {
NativeCareerCheckpoint decode(const NativeMemory& baseline,const CareerBytes& bytes,bool allowSeason){
    Reader r{bytes};const auto version=r.u32();
    if((version!=1&&version!=2)||r.u64()!=hash(baseline.data(),baseline.size()))throw UnsupportedNativeCheckpoint("Unsupported career checkpoint or asset pack");
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
    r.end();validate(c);return c;
}
}
NativeCareerCheckpoint decodeNativeCheckpoint(const NativeMemory& baseline,const CareerBytes& bytes){return decode(baseline,bytes,true);}
} // namespace scr::viewer
