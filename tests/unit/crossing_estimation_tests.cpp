#include "race/crossing_timing.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace scr::timing;
unsigned cases{};
void check(bool condition,const char* message){++cases;if(!condition)throw std::runtime_error(message);}
Observation at(const Gate& g,std::int64_t distance,bool after){
    Observation o;o.us=after?tickUs:0;o.segment=after?g.lap:g.previous;
    o.position[g.axis]=std::uint32_t(g.coordinate+g.direction*distance);
    o.position[g.transverse]=std::uint32_t(g.transverseMinimum+cellSize/2);return o;
}
void geometry(unsigned axis,int direction){
    Gate g{axis,2-axis,0,43,44,direction,5*cellSize,3*cellSize,true};
    const std::array<std::array<std::int64_t,3>,7> examples{{{1,1,60000},{2,1,80000},{0,1,0},{1,0,120000},{1,255999,0},{1,239999,1},{1,79999,2}}};
    for(const auto& item:examples){
        auto a=at(g,-item[0],false),b=at(g,item[1],true);
        const auto result=world(g,a,b,true);
        check(result.reason==Reason::reliable&&result.offsetUs==item[2],"Incorrect rational crossing or half-up rounding");
        a.position[1]=0xffffffffu;b.position[1]=1000u<<16;
        check(world(g,a,b,true).offsetUs==result.offsetUs,"Height changed the horizontal timing plane");
    }
    auto a=at(g,-64,false),b=at(g,64,true);
    check(world(g,a,b,false).reason==Reason::unaccepted,"Invented a crossing without a native event");
    check(world(g,{},b,true).reason==Reason::missingPrevious,"Missing bracket was accepted");
    auto invalid=g;invalid.valid=false;check(world(invalid,a,b,true).reason==Reason::unsupportedGate,"Unsupported gate was accepted");
    auto changed=a;changed.found=false;check(world(g,changed,b,true).reason==Reason::trackLookup,"Stale progress was accepted");
    changed=a;changed.offTrack=0x40;check(world(g,changed,b,true).reason==Reason::trackLookup,"Off-grid flag was accepted");
    changed=a;changed.offTrack=0x80;check(world(g,changed,b,true).reason==Reason::reliable,"Native-eligible side flags were rejected");
    changed=a;changed.recovery=true;check(world(g,changed,b,true).reason==Reason::recovery,"Recovery placement was blended");
    changed=a;++changed.restarts;check(world(g,changed,b,true).reason==Reason::recovery,"Restart identity change was accepted");
    changed=b;changed.recovery=true;check(world(g,a,changed,true).reason==Reason::recovery,"Recovery callback was accepted");
    for(auto interval:{std::int64_t(0),tickUs-1,tickUs+1,2*tickUs}){
        changed=b;changed.us=interval;check(world(g,a,changed,true).reason==Reason::interval,"Missing or duplicate tick was accepted");
    }
    changed=a;changed.segment=42;check(world(g,changed,b,true).reason==Reason::segments,"Skipped segment was accepted");
    changed=b;changed.segment=g.previous;check(world(g,a,changed,true).reason==Reason::segments,"Same segment was accepted");
    changed=b;changed.position[g.axis]=std::uint32_t(worldSize);check(world(g,a,changed,true).reason==Reason::position,"Out-of-world position was accepted");
    check(world(g,at(g,64,false),at(g,-64,true),true).reason==Reason::forwardBracket,"Reverse crossing was accepted");
    check(world(g,at(g,0,false),at(g,0,true),true).reason==Reason::forwardBracket,"Stationary reference divided by zero");
    check(world(g,at(g,-128,false),at(g,-64,true),true).reason==Reason::forwardBracket,"Unbracketed event was accepted");
    a=at(g,-64,false);b=at(g,64,true);
    a.position[g.transverse]=b.position[g.transverse]=std::uint32_t(g.transverseMinimum-1);
    check(world(g,a,b,true).reason==Reason::gateExtent,"Crossing outside the shared cell edge was accepted");
    a.position[g.transverse]=std::uint32_t(g.transverseMinimum-64);b.position[g.transverse]=std::uint32_t(g.transverseMinimum+64);
    check(world(g,a,b,true).reason==Reason::reliable,"Exact transverse endpoint was rejected");
    a=at(g,-64,false);b=at(g,64,true);a.us+=1000000000000;b.us+=1000000000000;
    check(world(g,a,b,true).offsetUs==60000,"Clock origin affected crossing fraction");
}
}
int main()try{
    for(auto axis:{0u,2u})for(auto direction:{-1,1})geometry(axis,direction);
    std::cout<<"Crossing prototype passed "<<cases<<" mathematical and rejection cases\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
