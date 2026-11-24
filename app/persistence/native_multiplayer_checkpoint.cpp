#include "persistence/native_multiplayer_checkpoint.h"
#include "persistence/native_checkpoint_identity.h"
#include "persistence/player_preferences.h"
#include <algorithm>
#include <limits>

namespace scr::runtime {
namespace {
using namespace state::checkpoint;
constexpr std::uint32_t checkpointMagic=0x504d4353,saveMagic=0x474d4353,formatVersion=1;
bool league(unsigned value){return value==0||value==11;}
bool allowed(NativeMenuPage page){
    switch(page){
    case NativeMenuPage::Main:case NativeMenuPage::Matchup:case NativeMenuPage::Results:case NativeMenuPage::Records:
    case NativeMenuPage::RoundResults:case NativeMenuPage::Standings:case NativeMenuPage::RaceResults:case NativeMenuPage::TrackResults:return true;
    default:return false;
    }
}
bool flag(Reader& r){const auto value=r.u32();if(value>1)throw std::runtime_error("Invalid multiplayer checkpoint flag");return value!=0;}
bool printable(const std::string& text){return std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32&&c<127;});}
void validate(const NativeMultiplayerCheckpoint& c){
    if(!c.memory||!allowed(c.menu.page)||c.menu.division>3||!league(c.menu.context)||c.preparedTrack>7||!league(c.preparedLeague)||
       c.menu.competitive!=(c.menu.page!=NativeMenuPage::Main))throw std::runtime_error("Invalid multiplayer continuation");
    const auto& m=*c.memory;const unsigned additional=m[0x5eb76],count=additional+1,begin=12-count;
    if(additional<1||additional>7||m[0x57c3c]||m[0x1c9ce]>3||!league(m[0x1c9d0])||m[0x1ca33]>7||
       m[0x5eb7c]>3||m[0x1bbd6]>3||m[0x1c9cf]>10||m[0x57c67]>3||m[0x5eb7e]>2||
       m[0x1ca23]>11||m[0x1ca24]>11||m[0x1ca25]>11||m[0x1ca26]>11)throw std::runtime_error("Invalid multiplayer state");
    unsigned roster=0,standings=0;
    for(unsigned n=0;n<12;++n){const unsigned id=m[0x1c9c2+n],ranked=m[0x1ca0e+n];
        if(id>11||(roster&(1u<<id))||ranked>11||(standings&(1u<<ranked))||m[0x1ca36+n]>10)throw std::runtime_error("Invalid multiplayer roster or damage");
        roster|=1u<<id;standings|=1u<<ranked;
        if(n>=begin){
            if(id<begin||ranked<begin)throw std::runtime_error("Multiplayer standings contain an inactive driver");
            bool named=false;for(unsigned ch=0;ch<12;++ch){const auto value=m[0x1ecab+16*n+ch];
                if(value<32||value>126)throw std::runtime_error("Invalid multiplayer driver name");named|=value!=' ';}
            if(!named)throw std::runtime_error("Unnamed multiplayer driver");
        }
    }
    for(int value:{c.menu.mainSelection,c.menu.divisionSelection,c.menu.saveSelection})if(value< -1||value>3)throw std::runtime_error("Invalid multiplayer selection");
    for(int value:c.menu.trackSelections)if(value< -1||value>2)throw std::runtime_error("Invalid multiplayer track selection");
    for(unsigned n=0;n<8;++n)if(m[0x648c2+n]>7)throw std::runtime_error("Invalid multiplayer track map");
    const auto state=m[0x57c62];
    if(c.menu.page==NativeMenuPage::Main&&(m[0x1bb8d]>2||m[0x1bb8b]))throw std::runtime_error("Invalid multiplayer main menu");
    const bool report=c.menu.page==NativeMenuPage::Standings||c.menu.page==NativeMenuPage::TrackResults;
    const unsigned expected=c.menu.page==NativeMenuPage::Main?0x10:c.menu.page==NativeMenuPage::Records?1:c.menu.page==NativeMenuPage::RoundResults?2:c.menu.page==NativeMenuPage::RaceResults?3:0x11;
    if(report?(state!=3&&state!=0x11):state!=expected)throw std::runtime_error("Multiplayer page and report state differ");
    if(c.menu.competitive){
        const unsigned ordinal=2*m[0x1c9ce]+((m[0x5eb77]^1)&1);
        if(c.menu.division!=m[0x1c9ce]||c.menu.context!=m[0x1c9d0]||!(m[0x1ca22]&0x80))throw std::runtime_error("Multiplayer championship context differs");
        if(m[0x5eb78]!=count||m[0x1ca35]>=count||m[0x5eb77]>1||m[0x5eb7b]>2*count)throw std::runtime_error("Invalid multiplayer round configuration");
        // Overview preparation clears the original shared UI/configuration
        // scratch range. Scoring rebuilds these standings bounds before reports.
        const bool bounds=m[0x1bc5b]==begin&&m[0x1bc59]==12;
        if((!bounds&&!(c.previewPrepared&&!m[0x1bc5b]&&!m[0x1bc59]))||m[0x5eb79]<begin||m[0x5eb79]>11)throw std::runtime_error("Invalid multiplayer active driver configuration");
        if(m[0x59140]>11||m[0x59141]>11||m[0x1ca33]!=m[0x648c2+ordinal]||
           m[0x1ca21]!=m[0x648ca+(ordinal^(m[0x1c9d0]?1:0))])throw std::runtime_error("Multiplayer track configuration differs");
        const bool trackEnd=c.menu.page==NativeMenuPage::TrackResults||(c.menu.page==NativeMenuPage::Standings&&c.menu.multiplayerTrackEnd);
        if(c.menu.multiplayerTrackEnd!=trackEnd||m[0x5eb7a]!=(trackEnd?0x80:0)||
           m[0x1bbcb]!=((c.menu.page==NativeMenuPage::RaceResults||c.menu.page==NativeMenuPage::TrackResults)?0x80:0))throw std::runtime_error("Invalid multiplayer report stage");
        if(!trackEnd&&(m[0x5eb79]!=11-m[0x1ca35]||m[0x1ca27]!=m[0x5eb79]||m[0x1ca28]!=m[0x5eb77]||m[0x1ca29]!=m[0x5eb77]))throw std::runtime_error("Invalid multiplayer turn");
    }else if(c.menu.multiplayerTrackEnd)throw std::runtime_error("Track-end flag outside multiplayer championship");
    if(c.previewPrepared&&(c.menu.page!=NativeMenuPage::Matchup||c.preparedTrack!=m[0x1ca33]||c.preparedLeague!=m[0x1c9d0]||
       !m[0x1ca1a]||m[0x1ca1b]>=m[0x1ca1a]))throw std::runtime_error("Invalid prepared multiplayer matchup");
    if(c.finishPending?(c.menu.page!=NativeMenuPage::Results||c.finishPixels.size()!=64000):!c.finishPixels.empty())throw std::runtime_error("Invalid multiplayer finish display");
    if(std::any_of(c.finishPixels.begin(),c.finishPixels.end(),[](auto colour){return colour>0xffffff;}))throw std::runtime_error("Invalid multiplayer finish colour");
    if(c.finishTiming){
        if(!c.finishPending||c.finishTiming->lap.microseconds<0||c.finishTiming->record.microseconds<0)throw std::runtime_error("Invalid multiplayer finish timing");
    }
    if(c.enhancedRecordNotice){
        if(c.menu.page!=NativeMenuPage::Results&&c.menu.page!=NativeMenuPage::Records)throw std::runtime_error("Invalid multiplayer record notice stage");
        for(const auto* line:{&c.enhancedRecordNotice->lap,&c.enhancedRecordNotice->race})if(*line){const auto& value=**line;
            if(normaliseHallName(value.name).display!=value.name||value.milliseconds<0||value.milliseconds>std::numeric_limits<std::int64_t>::max()/1000+1)throw std::runtime_error("Invalid multiplayer record notice");}
    }
}
Bytes envelope(std::uint32_t magic,const Bytes& body,std::size_t maximum){
    if(body.size()>maximum-20)throw std::runtime_error("Multiplayer checkpoint exceeds limit");
    Writer w;w.u32(magic);w.u32(formatVersion);w.u32(std::uint32_t(body.size()));w.u64(hash(body.data(),body.size()));w.bytes(body.data(),body.size());return std::move(w.data);
}
Bytes payload(std::uint32_t magic,const Bytes& bytes,std::size_t maximum){
    if(bytes.size()<20||bytes.size()>maximum)throw std::runtime_error("Invalid multiplayer checkpoint size");
    Reader r{bytes};if(r.u32()!=magic)throw std::runtime_error("Not a native multiplayer checkpoint");
    if(r.u32()!=formatVersion)throw UnsupportedNativeMultiplayerCheckpoint("Unsupported multiplayer checkpoint version");
    const auto size=r.u32();const auto checksum=r.u64();if(size!=bytes.size()-r.position||checksum!=hash(bytes.data()+r.position,size))throw std::runtime_error("Damaged multiplayer checkpoint");
    return {bytes.begin()+r.position,bytes.end()};
}
void validateInfo(const NativeMultiplayerInfo& info,std::uint64_t generation){
    if(!validPlayerIdentity(info.id)||info.title.empty()||info.title.size()>32||!printable(info.title)||
       info.title.find_first_not_of(' ')==std::string::npos||!generation)throw std::runtime_error("Invalid multiplayer saved-game metadata");
}
}
MultiplayerBytes encodeNativeMultiplayerCheckpoint(const NativeMemory& baseline,const NativeMultiplayerCheckpoint& c){
    validate(c);Writer w;w.u64(nativeCheckpointBaselineIdentity(baseline));
    w.u32(unsigned(c.menu.page));w.u32(c.menu.competitive);w.u32(c.menu.multiplayerTrackEnd);w.u32(c.menu.division);w.u32(c.menu.context);
    for(int value:{c.menu.mainSelection,c.menu.divisionSelection,c.menu.saveSelection,c.menu.trackSelections[0],c.menu.trackSelections[1],c.menu.trackSelections[2],c.menu.trackSelections[3]})w.u32(unsigned(value+1));
    w.u32(c.previewPrepared);w.u32(c.preparedTrack);w.u32(c.preparedLeague);w.u32(c.finishPending);w.u32(c.finishTiming.has_value());w.u32(c.enhancedRecordNotice.has_value());
    writeMemoryDelta(w,baseline,*c.memory);
    for(auto colour:c.finishPixels)w.u32(colour);
    if(c.finishTiming){w.u32(enhancedTimingMethodVersion);for(const auto value:{c.finishTiming->lap,c.finishTiming->record}){
        w.u64(std::uint64_t(value.microseconds));w.u32(value.available);w.u32(value.approximate);}}
    if(c.enhancedRecordNotice){w.u32(enhancedTimingMethodVersion);for(const auto* line:{&c.enhancedRecordNotice->lap,&c.enhancedRecordNotice->race}){
        w.u32(line->has_value());if(*line){w.text((*line)->name);w.u64(std::uint64_t((*line)->milliseconds));}}}
    return envelope(checkpointMagic,w.data,nativeMultiplayerCheckpointMaximumBytes);
}
NativeMultiplayerCheckpoint decodeNativeMultiplayerCheckpoint(const NativeMemory& baseline,const MultiplayerBytes& bytes){
    const auto body=payload(checkpointMagic,bytes,nativeMultiplayerCheckpointMaximumBytes);Reader r{body};
    if(r.u64()!=nativeCheckpointBaselineIdentity(baseline))throw UnsupportedNativeMultiplayerCheckpoint("Multiplayer checkpoint uses an incompatible asset pack");
    NativeMultiplayerCheckpoint c;c.memory=std::make_unique<NativeMemory>(baseline);c.menu.page=NativeMenuPage(r.u32());c.menu.competitive=flag(r);c.menu.multiplayerTrackEnd=flag(r);
    c.menu.division=r.u32();c.menu.context=r.u32();
    const auto selection=[&](){const auto value=r.u32();if(value>4)throw std::runtime_error("Invalid multiplayer saved selection");return int(value)-1;};
    c.menu.mainSelection=selection();c.menu.divisionSelection=selection();c.menu.saveSelection=selection();for(auto& value:c.menu.trackSelections)value=selection();
    c.previewPrepared=flag(r);c.preparedTrack=r.u32();c.preparedLeague=r.u32();c.finishPending=flag(r);const bool timing=flag(r),notice=flag(r);
    readMemoryDelta(r,*c.memory);
    if(c.finishPending){c.finishPixels.resize(64000);for(auto& colour:c.finishPixels)colour=r.u32();}
    const auto time=[&](){const auto value=r.u64();if(value>std::uint64_t(std::numeric_limits<std::int64_t>::max()))throw std::runtime_error("Invalid multiplayer saved time");return std::int64_t(value);};
    const auto method=[&](){if(r.u32()!=enhancedTimingMethodVersion)throw UnsupportedNativeMultiplayerCheckpoint("Unsupported multiplayer timing method");};
    if(timing){method();c.finishTiming.emplace();for(auto* value:{&c.finishTiming->lap,&c.finishTiming->record}){value->microseconds=time();value->available=flag(r);value->approximate=flag(r);}}
    if(notice){method();c.enhancedRecordNotice.emplace();for(auto* line:{&c.enhancedRecordNotice->lap,&c.enhancedRecordNotice->race})if(flag(r)){const auto name=r.text(12);*line=EnhancedRecordLine{name,time()};}}
    r.end();validate(c);return c;
}
MultiplayerBytes encodeNativeMultiplayerSave(const NativeMemory& baseline,const NativeMultiplayerSave& saved){
    validateInfo(saved.info,saved.generation);(void)decodeNativeMultiplayerCheckpoint(baseline,saved.checkpoint);
    Writer w;w.text(saved.info.id);w.text(saved.info.title);w.u64(saved.info.lastPlayed);w.u64(saved.generation);w.u32(std::uint32_t(saved.checkpoint.size()));w.bytes(saved.checkpoint.data(),saved.checkpoint.size());
    return envelope(saveMagic,w.data,nativeMultiplayerSaveMaximumBytes);
}
NativeMultiplayerSave decodeNativeMultiplayerSave(const NativeMemory& baseline,const MultiplayerBytes& bytes){
    const auto body=payload(saveMagic,bytes,nativeMultiplayerSaveMaximumBytes);Reader r{body};NativeMultiplayerSave saved;
    saved.info.id=r.text(32);saved.info.title=r.text(32);saved.info.lastPlayed=r.u64();saved.generation=r.u64();const auto size=r.u32();
    if(size>nativeMultiplayerCheckpointMaximumBytes)throw std::runtime_error("Multiplayer checkpoint exceeds limit");r.require(size);
    saved.checkpoint.assign(body.begin()+r.position,body.begin()+r.position+size);r.position+=size;r.end();validateInfo(saved.info,saved.generation);
    (void)decodeNativeMultiplayerCheckpoint(baseline,saved.checkpoint);return saved;
}
} // namespace scr::runtime
