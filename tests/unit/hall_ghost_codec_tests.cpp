#include "hall_ghost_fixture.h"
#include <iostream>

namespace {
using namespace scr::runtime;using namespace scr::test::hall;
unsigned checks{};
void require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
template<class F>void rejected(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,"Invalid Hall ghost accepted");}
}
int main()try{
    for(unsigned track=0;track<8;++track)for(auto league:{HallLeague::Normal,HallLeague::Super}){
        const auto a=attempt(track+1,4217,{track,league,HallRecordKind::Lap});const auto g=ghost(a);const auto bytes=encodeHallGhost(g);
        const auto decoded=decodeHallGhost(bytes);requireHallGhostBinding(decoded,a,catalog);require(encodeHallGhost(decoded)==bytes,"Hall ghost round trip changed bytes");
        for(std::size_t n=0;n<bytes.size();++n){auto truncated=bytes;truncated.resize(n);rejected([&]{decodeHallGhost(truncated);});}
        auto changed=bytes;changed[changed.size()-9]^=1;rejected([&]{decodeHallGhost(changed);});
        changed=bytes;changed.push_back(0);rejected([&]{decodeHallGhost(changed);});
        auto other=a;other.id=id(900);rejected([&]{requireHallGhostBinding(decoded,other,catalog);});
        other=a;other.time=time(4218);rejected([&]{requireHallGhostBinding(decoded,other,catalog);});
        rejected([&]{requireHallGhostBinding(decoded,a,{catalog.trackContent,2});});
    }
    auto maximum=ghost(attempt(99,59999,{5,HallLeague::Super,HallRecordKind::Lap}));
    maximum.samples.resize(maxGhostLapSamples,maximum.samples.front());
    for(unsigned n=0;n<maxGhostLapSamples;++n)maximum.samples[n].microseconds=std::int64_t(n)*120000;
    maximum.durationMicroseconds=maximum.samples.back().microseconds;maximum.finish.observedMicroseconds=maximum.finish.alignedMicroseconds=maximum.durationMicroseconds;
    maximum.bridges.resize(maxGhostLapBridgeShapes,maximum.bridges.front());
    for(auto& b:maximum.bridges){b.cachedSections.resize(510,0);for(unsigned n=0;n<510;n+=3)b.cachedSections[n]=0x233;}
    const auto bytes=encodeHallGhost(maximum);require(bytes.size()==hallGhostMaximumBytes,"Maximum Hall ghost byte bound differs");
    require(encodeHallGhost(decodeHallGhost(bytes))==bytes,"Maximum recording failed round trip");
    auto bad=maximum;bad.samples.push_back(bad.samples.back());rejected([&]{encodeHallGhost(bad);});
    bad=maximum;bad.bridges.push_back(bad.bridges.back());rejected([&]{encodeHallGhost(bad);});
    bad=maximum;bad.samples[1].microseconds=-1;rejected([&]{encodeHallGhost(bad);});
    bad=maximum;bad.samples.back().bridge=65;rejected([&]{encodeHallGhost(bad);});
    bad=maximum;bad.finish.correctionHundredths=1;rejected([&]{encodeHallGhost(bad);});
    bad=maximum;bad.board.kind=HallRecordKind::Race;rejected([&]{encodeHallGhost(bad);});
    std::cout<<"Hall ghost codec: "<<checks<<" checks passed; maximum bytes "<<bytes.size()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
