#include "persistence/ghost_lap_codec.h"
#include "ghost_lap_fixture.h"
#include <iostream>
#include <limits>

namespace {
using namespace scr::runtime;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F call){bool caught=false;try{call();}catch(const std::exception&){caught=true;}require(caught,"Invalid ghost recording was accepted");}
}
int main()try{
    const std::string id(32,'a');
    for(unsigned track:{0u,5u})for(bool super:{false,true}){
        const auto lap=scr::test::ghost::lap(id,track,super);const auto bytes=encodeGhostLap(lap);
        const auto copy=decodeGhostLap(bytes,lap.context.key,1234);
        require(encodeGhostLap(copy)==bytes,"Ghost codec lost timestamps, pose bits, bridge or metadata");
        rejected([&]{decodeGhostLap(bytes,{std::string(32,'b'),track,super},1234);});
        rejected([&]{decodeGhostLap(bytes,lap.context.key,5678);});
        auto truncated=bytes;truncated.pop_back();rejected([&]{decodeGhostLap(truncated);});
        auto trailing=bytes;trailing.push_back(0);rejected([&]{decodeGhostLap(trailing);});
        for(unsigned offset:{4u,8u}){
            auto future=bytes;future[offset]=2;bool unsupported=false;
            try{decodeGhostLap(future);}catch(const UnsupportedGhostLap&){unsupported=true;}require(unsupported,"Future format/rules were not distinguished from damage");
        }
    }
    const auto original=scr::test::ghost::lap(id);
    auto hugeTime=encodeGhostLap(original);const auto second=hugeTime.size()-original.samples.size()*36+36;
    for(unsigned n=0;n<8;++n)hugeTime[second+n]=std::uint8_t(std::uint64_t(std::numeric_limits<std::int64_t>::max())>>(n*8));
    rejected([&]{decodeGhostLap(hugeTime);});
    for(unsigned kind=0;kind<7;++kind){auto bad=original;
        if(kind==0)bad.durationMicroseconds=0;
        if(kind==1)bad.samples[1].microseconds=-240000;
        if(kind==2)bad.samples.back().microseconds+=240000;
        if(kind==3)bad.completedTime.seconds=0x60;
        if(kind==4)bad.samples[0].bridge=0;
        if(kind==5)bad.finish.correctionHundredths=1;
        if(kind==6)bad.context.key.career="../invalid";
        rejected([&]{encodeGhostLap(bad);});
    }
    auto maximum=original;maximum.samples.insert(maximum.samples.begin()+1,maxGhostLapSamples-3,original.samples[1]);
    const auto bytes=encodeGhostLap(maximum);require(decodeGhostLap(bytes).samples.size()==maxGhostLapSamples,"Maximum bounded recording did not roundtrip");
    maximum.samples.insert(maximum.samples.begin()+1,original.samples[1]);rejected([&]{encodeGhostLap(maximum);});
    auto bridge=scr::test::ghost::lap(id,5);bridge.bridges.resize(maxGhostLapBridgeShapes,bridge.bridges.front());
    require(decodeGhostLap(encodeGhostLap(bridge)).bridges.size()==maxGhostLapBridgeShapes,"Maximum bridge table did not roundtrip");
    bridge.bridges.push_back(bridge.bridges.front());rejected([&]{encodeGhostLap(bridge);});
    std::cout<<"Ghost codec compatibility, validation and resource bounds passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
