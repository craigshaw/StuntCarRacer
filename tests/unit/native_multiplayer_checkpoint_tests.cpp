#include "persistence/native_multiplayer_checkpoint.h"
#include "persistence/native_career_checkpoint.h"
#include "persistence/native_checkpoint_identity.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>

namespace {
using namespace scr::runtime;using namespace scr::state::checkpoint;
unsigned checks{},roundtrips{},rejections{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F action,const char* message){++rejections;bool caught=false;try{action();}catch(const std::exception&){caught=true;}check(caught,message);}
NativeMultiplayerCheckpoint fixture(unsigned players=2,unsigned division=0,unsigned league=0,NativeMenuPage page=NativeMenuPage::Matchup,bool end=false){
    NativeMultiplayerCheckpoint c;c.memory=std::make_unique<NativeMemory>();auto& m=*c.memory;c.menu.page=page;c.menu.competitive=page!=NativeMenuPage::Main;c.menu.division=division;c.menu.context=league;
    m[0x57c62]=std::uint8_t(page==NativeMenuPage::Main?0x10:page==NativeMenuPage::Records?1:page==NativeMenuPage::RoundResults?2:page==NativeMenuPage::Standings||page==NativeMenuPage::RaceResults||page==NativeMenuPage::TrackResults?3:0x11);
    m[0x5eb76]=std::uint8_t(players-1);m[0x1c9ce]=std::uint8_t(division);m[0x1c9d0]=std::uint8_t(league);m[0x1c9cf]=7;
    for(unsigned n=0;n<12;++n){m[0x1c9c2+n]=m[0x1ca0e+n]=std::uint8_t(n);m[0x1ca36+n]=std::uint8_t(n%11);std::fill_n(m.begin()+0x1ecab+16*n,12,'D');}
    for(unsigned n=0;n<8;++n){m[0x648c2+n]=std::uint8_t(n);m[0x648ca+n]=std::uint8_t(n);}
    m[0x625a2]=0x93;m[0x625a3]=0x17;m[0x625a6]=0xfe;m[0x636b8]=0x34;m[0x636bb]=0x82;
    if(c.menu.competitive){m[0x1ca22]=0x80;m[0x5eb78]=std::uint8_t(players);m[0x5eb77]=1;m[0x5eb79]=m[0x1ca27]=11;m[0x1ca28]=m[0x1ca29]=1;
        m[0x1bc5b]=std::uint8_t(12-players);m[0x1bc59]=12;m[0x1ca33]=std::uint8_t(2*division);m[0x1ca21]=m[0x648ca+(2*division+(league?1:0))];
        c.menu.multiplayerTrackEnd=end||page==NativeMenuPage::TrackResults;m[0x5eb7a]=c.menu.multiplayerTrackEnd?0x80:0;
        m[0x1bbcb]=(page==NativeMenuPage::RaceResults||page==NativeMenuPage::TrackResults)?0x80:0;
    }return c;
}
Bytes wrap(const Bytes& body){Writer w;w.u32(0x504d4353);w.u32(1);w.u32(unsigned(body.size()));w.u64(hash(body.data(),body.size()));w.bytes(body.data(),body.size());return std::move(w.data);}
void u32(Bytes& data,std::size_t at,unsigned value){for(unsigned n=0;n<4;++n)data.at(at+n)=std::uint8_t(value>>(8*n));}
Bytes changedField(Bytes bytes,std::size_t at,unsigned value){u32(bytes,at,value);return wrap(Bytes(bytes.begin()+20,bytes.end()));}
Bytes changedMemory(const NativeMemory& baseline,const Bytes& bytes,unsigned address,std::uint8_t value){
    auto c=decodeNativeMultiplayerCheckpoint(baseline,bytes);(*c.memory)[address]=value;Writer w;w.bytes(bytes.data()+20,80);writeMemoryDelta(w,baseline,*c.memory);return wrap(w.data);
}
void matrix(const NativeMemory& baseline){
    for(unsigned count=2;count<=8;++count)for(unsigned division=0;division<4;++division)for(unsigned league:{0u,11u})
    for(auto page:{NativeMenuPage::Main,NativeMenuPage::Matchup,NativeMenuPage::Results,NativeMenuPage::Records,NativeMenuPage::RoundResults,NativeMenuPage::Standings,NativeMenuPage::RaceResults,NativeMenuPage::TrackResults}){
        auto c=fixture(count,division,league,page);const auto unchanged=std::make_unique<NativeMemory>(*c.memory);
        if(page==NativeMenuPage::Results){c.finishPending=true;c.finishPixels.assign(64000,0x123456);c.finishTiming=EnhancedClockDisplay{{123456789,true,false},{99876543,true,true}};}
        if(page==NativeMenuPage::Results||page==NativeMenuPage::Records)c.enhancedRecordNotice=EnhancedRecordNotice{{EnhancedRecordLine{"DRIVER",123457}},{EnhancedRecordLine{"OTHER",400001}}};
        const auto bytes=encodeNativeMultiplayerCheckpoint(baseline,c);auto restored=decodeNativeMultiplayerCheckpoint(baseline,bytes);
        check(*restored.memory==*unchanged&&*c.memory==*unchanged,"Checkpoint changed native bytes or RNG");
        check(restored.menu.page==page&&restored.menu.multiplayerTrackEnd==c.menu.multiplayerTrackEnd&&restored.menu.division==division&&restored.menu.context==league,"Checkpoint lost host continuation");
        check(encodeNativeMultiplayerCheckpoint(baseline,restored)==bytes,"Multiplayer checkpoint round trip changed bytes");++roundtrips;
    }
    auto trackEnd=fixture(8,3,11,NativeMenuPage::Standings,true);const auto bytes=encodeNativeMultiplayerCheckpoint(baseline,trackEnd);
    check(decodeNativeMultiplayerCheckpoint(baseline,bytes).menu.multiplayerTrackEnd,"Track-end standings lost committed bonuses");
    auto prepared=fixture(8,2,11);prepared.previewPrepared=true;prepared.preparedTrack=4;prepared.preparedLeague=11;(*prepared.memory)[0x1ca1a]=20;(*prepared.memory)[0x1ca1b]=3;
    check(decodeNativeMultiplayerCheckpoint(baseline,encodeNativeMultiplayerCheckpoint(baseline,prepared)).previewPrepared,"Prepared matchup was not retained");
    for(unsigned n=0x30000;n<0x50000;n+=2)(*prepared.memory)[n]^=0x55;
    check(*decodeNativeMultiplayerCheckpoint(baseline,encodeNativeMultiplayerCheckpoint(baseline,prepared)).memory==*prepared.memory,"Fragmented memory delta lost native bytes");
}
void invalid(const NativeMemory& baseline){
    auto c=fixture();const auto bytes=encodeNativeMultiplayerCheckpoint(baseline,c);
    for(std::size_t size:{0u,1u,19u,20u,99u})rejected([&]{(void)decodeNativeMultiplayerCheckpoint(baseline,Bytes(bytes.begin(),bytes.begin()+std::min(size,bytes.size())));},"Truncated checkpoint accepted");
    auto damaged=bytes;damaged.back()^=1;rejected([&]{(void)decodeNativeMultiplayerCheckpoint(baseline,damaged);},"Checksum corruption accepted");
    rejected([&]{(void)decodeNativeMultiplayerCheckpoint(baseline,Bytes(nativeMultiplayerCheckpointMaximumBytes+1));},"Oversize checkpoint accepted");
    auto future=bytes;u32(future,4,2);bool unsupported=false;try{(void)decodeNativeMultiplayerCheckpoint(baseline,future);}catch(const UnsupportedNativeMultiplayerCheckpoint&){unsupported=true;}check(unsupported,"Future format was not classified unsupported");
    auto other=std::make_unique<NativeMemory>(baseline);(*other)[123]^=1;rejected([&]{(void)decodeNativeMultiplayerCheckpoint(*other,bytes);},"Incompatible baseline accepted");
    for(unsigned page:{unsigned(NativeMenuPage::Race),unsigned(NativeMenuPage::Preview),unsigned(NativeMenuPage::SeasonEnd),unsigned(NativeMenuPage::LoadSave),999u})
        rejected([&]{(void)decodeNativeMultiplayerCheckpoint(baseline,changedField(bytes,28,page));},"Wrong-mode/unsafe stage accepted");
    for(unsigned at:{32u,36u,76u,88u,92u,96u})rejected([&]{(void)decodeNativeMultiplayerCheckpoint(baseline,changedField(bytes,at,2));},"Invalid boolean accepted");
    for(auto field:{std::pair<unsigned,std::uint8_t>{0x5eb76,0},{0x5eb76,8},{0x57c3c,1},{0x1c9ce,4},{0x1c9d0,1},{0x1c9c2,11},{0x1ca0e,11},{0x1ca36+11,11},{0x1ecab+11*16,0},{0x1ca35,2},{0x5eb79,9},{0x5eb77,2},{0x5eb78,3},{0x1ca33,7},{0x1ca21,7},{0x57c67,4},{0x5eb7a,0x80},{0x1bbcb,0x80}})
        rejected([&]{(void)decodeNativeMultiplayerCheckpoint(baseline,changedMemory(baseline,bytes,field.first,field.second));},"Invalid native multiplayer state accepted");
    auto body=Bytes(bytes.begin()+20,bytes.begin()+100);Writer ranges;ranges.data=body;ranges.u32(2);ranges.u32(30);ranges.u32(2);ranges.data.insert(ranges.data.end(),2,0);ranges.u32(29);ranges.u32(2);ranges.data.insert(ranges.data.end(),2,0);
    rejected([&]{(void)decodeNativeMultiplayerCheckpoint(baseline,wrap(ranges.data));},"Overlapping memory ranges accepted");
    for(const auto& edit:std::vector<std::function<void(NativeMultiplayerCheckpoint&)>>{[](auto& f){f.finishTiming.emplace();},[](auto& f){f.previewPrepared=true;},[](auto& f){f.menu.trackSelections[0]=3;},[](auto& f){f.enhancedRecordNotice.emplace();},[](auto& f){f.finishPixels.push_back(0);}}){auto bad=fixture();edit(bad);rejected([&]{(void)encodeNativeMultiplayerCheckpoint(baseline,bad);},"Inconsistent host continuation encoded");}
    NativeCareerCheckpoint career;career.memory=std::make_unique<NativeMemory>(*c.memory);(*career.memory)[0x5eb76]=0;career.menu.page=NativeMenuPage::Main;
    const auto old=encodeNativeCheckpoint(baseline,career);rejected([&]{(void)decodeNativeMultiplayerCheckpoint(baseline,old);},"Career bytes accepted as multiplayer");
    rejected([&]{(void)decodeNativeCheckpoint(baseline,bytes);},"Multiplayer bytes accepted as career");
}
void metadata(const NativeMemory& baseline){
    auto c=fixture(8,3,11);NativeMultiplayerSave game{{std::string(32,'a'),"Friday Night",123456789},42,encodeNativeMultiplayerCheckpoint(baseline,c)};
    const auto bytes=encodeNativeMultiplayerSave(baseline,game);const auto saved=decodeNativeMultiplayerSave(baseline,bytes);
    check(saved.info.id==game.info.id&&saved.info.title==game.info.title&&saved.info.lastPlayed==game.info.lastPlayed&&saved.generation==42&&saved.checkpoint==game.checkpoint,"Saved game lost identity/metadata/generation");
    game.info.id=std::string(32,'b');const auto rekeyed=decodeNativeMultiplayerSave(baseline,encodeNativeMultiplayerSave(baseline,game));check(rekeyed.checkpoint==saved.checkpoint,"Host rekey changed native checkpoint");
    for(const auto& title:{std::string{},std::string(33,'X'),std::string("   "),std::string("bad\nname")}){game.info.title=title;rejected([&]{(void)encodeNativeMultiplayerSave(baseline,game);},"Invalid saved-game title accepted");}game.info.title="Restored";
    game.generation=0;rejected([&]{(void)encodeNativeMultiplayerSave(baseline,game);},"Zero generation accepted");game.generation=1;
    game.info.id="../../escape";rejected([&]{(void)encodeNativeMultiplayerSave(baseline,game);},"Invalid saved-game identity accepted");
    auto changed=bytes;changed[30]^=1;rejected([&]{(void)decodeNativeMultiplayerSave(baseline,changed);},"Damaged saved-game metadata accepted");
    rejected([&]{(void)decodeNativeMultiplayerCheckpoint(baseline,bytes);},"Outer game envelope accepted as checkpoint");
}
Bytes previousCareerWire(const NativeMemory& baseline,const NativeCareerCheckpoint& c,unsigned version){
    // Retain the pre-refactor field/run encoding independently of the shared
    // delta helper: existing v1-v4 career bytes must remain identical/readable.
    Writer w;w.u32(version);w.u64(nativeCheckpointBaselineIdentity(baseline));w.u32(unsigned(c.menu.page));w.u32(c.menu.competitive);w.u32(c.menu.division);w.u32(c.menu.context);
    for(int value:{c.menu.mainSelection,c.menu.divisionSelection,c.menu.saveSelection,c.menu.trackSelections[0],c.menu.trackSelections[1],c.menu.trackSelections[2],c.menu.trackSelections[3]})w.u32(unsigned(value+1));
    w.u32(c.previewPrepared);w.u32(c.preparedTrack);w.u32(c.preparedLeague);w.u32(c.finishPending);
    Writer ranges;unsigned count=0;for(unsigned n=0;n<baseline.size();){if((*c.memory)[n]==baseline[n]){++n;continue;}const auto first=n;while(n<baseline.size()&&(*c.memory)[n]!=baseline[n])++n;ranges.u32(first);ranges.u32(n-first);ranges.bytes(c.memory->data()+first,n-first);++count;}
    w.u32(count);w.bytes(ranges.data.data(),ranges.data.size());if(c.finishPending)for(auto colour:c.finishPixels)w.u32(colour);
    if(version>=2)w.u32(0);if(version==4)w.u32(c.finishTiming.has_value());
    if(c.finishTiming){w.u32(enhancedTimingMethodVersion);for(const auto value:{c.finishTiming->lap,c.finishTiming->record}){w.u64(std::uint64_t(value.microseconds));w.u32(value.available);w.u32(value.approximate);}}
    if(c.enhancedRecordNotice){w.u32(enhancedTimingMethodVersion);for(const auto* line:{&c.enhancedRecordNotice->lap,&c.enhancedRecordNotice->race}){w.u32(line->has_value());if(*line){w.text((*line)->name);w.u64(std::uint64_t((*line)->milliseconds));}}}
    return std::move(w.data);
}
void careerCompatibility(const NativeMemory& baseline){
    auto fixtureState=fixture();NativeCareerCheckpoint c;c.memory=std::move(fixtureState.memory);(*c.memory)[0x5eb76]=0;c.menu.page=NativeMenuPage::Main;
    const auto v1=previousCareerWire(baseline,c,1);check(*decodeNativeCheckpoint(baseline,v1).memory==*c.memory,"Previous v1 career bytes no longer load");
    check(encodeNativeCheckpoint(baseline,c)==previousCareerWire(baseline,c,2),"Version-2 career wire format changed");
    c.menu.page=NativeMenuPage::Results;c.menu.competitive=true;c.finishPending=true;c.finishPixels.assign(64000,0x123456);c.finishTiming=EnhancedClockDisplay{{100000,true,false},{200000,true,true}};
    check(encodeNativeCheckpoint(baseline,c)==previousCareerWire(baseline,c,3),"Version-3 career wire format changed");
    c.enhancedRecordNotice=EnhancedRecordNotice{{EnhancedRecordLine{"DRIVER",1234}},{}};
    check(encodeNativeCheckpoint(baseline,c)==previousCareerWire(baseline,c,4),"Version-4 career wire format changed");c.finishTiming.reset();
    check(encodeNativeCheckpoint(baseline,c)==previousCareerWire(baseline,c,4),"Version-4 career optional timing flag changed");
    for(const auto version:{2u,3u,4u}){
        if(version<4)c.enhancedRecordNotice.reset();else c.enhancedRecordNotice.emplace();if(version==3)c.finishTiming=EnhancedClockDisplay{};
        const auto previous=previousCareerWire(baseline,c,version);check(*decodeNativeCheckpoint(baseline,previous).memory==*c.memory,"Previous career bytes no longer restore full native memory");
    }
}
void finishValidation(const NativeMemory& baseline){
    auto c=fixture(2,0,0,NativeMenuPage::Results);c.finishPending=true;c.finishPixels.assign(64000,0x123456);c.finishTiming=EnhancedClockDisplay{};
    const auto bytes=encodeNativeMultiplayerCheckpoint(baseline,c);const Bytes body(bytes.begin()+20,bytes.end());Reader r{body};r.position=80;auto memory=std::make_unique<NativeMemory>(baseline);readMemoryDelta(r,*memory);
    rejected([&]{(void)decodeNativeMultiplayerCheckpoint(baseline,changedField(bytes,20+r.position,0xff123456));},"Invalid finish colour accepted");
    bool unsupported=false;try{(void)decodeNativeMultiplayerCheckpoint(baseline,changedField(bytes,20+r.position+64000*4,2));}catch(const UnsupportedNativeMultiplayerCheckpoint&){unsupported=true;}check(unsupported,"Future finish timing method was not unsupported");
    rejected([&]{(void)decodeNativeMultiplayerCheckpoint(baseline,changedField(bytes,20+r.position+64000*4+8,0x80000000));},"Overflowed finish time accepted");
}
}
int main()try{
    const auto baseline=std::make_unique<NativeMemory>();matrix(*baseline);invalid(*baseline);metadata(*baseline);careerCompatibility(*baseline);finishValidation(*baseline);
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"checkpoint_roundtrips\":"<<roundtrips<<",\"rejected_invalid_cases\":"<<rejections<<",\"all_counts_divisions_leagues\":true,\"full_native_memory_rng\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
