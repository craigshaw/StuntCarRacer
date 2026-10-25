#include "native_wheel_sprites.h"
#include "native_driving_particles.h"
#include "native_cockpit_chains.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace scr::viewer;
unsigned cases{};
void check(bool value,const char* message){++cases;if(!value)throw std::runtime_error(message);}
bool same(const scr::DrivingParticleDraws& a,const scr::DrivingParticleDraws& b){
    if(a.size()!=b.size())return false;
    for(unsigned n=0;n<a.size();++n)if(a[n].x!=b[n].x||a[n].y!=b[n].y||a[n].index!=b[n].index||
        a[n].dust!=b[n].dust||a[n].spawned!=b[n].spawned)return false;
    return true;
}
void wheels(){
    NativeWheelSpriteFrame a,b;CockpitFrame cockpitA,cockpitB;
    for(unsigned id:{0u,3u}){cockpitA.objects[id]={32,120,1,15};cockpitB.objects[id]={40,104,1,15};}
    for(unsigned n=0;n<12;++n){a.placements[n]={32+int(n),120,15};b.placements[n]={40+int(n),104,12};}
    const auto quarter=interpolateNativeWheelSprites(a,b,cockpitA,cockpitB,0.25);
    for(unsigned n=0;n<12;++n)check(quarter.placements[n].x==34+int(n)&&quarter.placements[n].y==116&&
        quarter.placements[n].rows==15,"Wheel placement did not move independently of its source span");
    check(interpolateNativeWheelSprites(a,b,cockpitA,cockpitB,0).placements[0].y==120&&
        interpolateNativeWheelSprites(a,b,cockpitA,cockpitB,1).placements[0].rows==12,"Wheel endpoints differ");
    check(a.placements[0].y==120&&b.placements[0].y==104,"Wheel interpolation mutated its inputs");
    cockpitA.objects[0].y=128;cockpitB.objects[0].y=120;
    a.placements[0].y=b.placements[0].y=128; // Now inactive, the next normal header stays stale.
    a.placements[6].y=100;b.placements[6].y=120;
    const auto raised=interpolateNativeWheelSprites(a,b,cockpitA,cockpitB,0.5);
    check(raised.placements[0].y==124,"Normal-to-raised wheel followed its stale inactive header");
    cockpitA.objects[0].y=120;cockpitB.objects[0].y=128;
    a.placements[6].y=b.placements[6].y=120;
    check(interpolateNativeWheelSprites(a,b,cockpitA,cockpitB,0.5).placements[6].y==124,
        "Raised-to-normal wheel followed its stale inactive header");
}
void particles(){
    const scr::DrivingParticleDraws a{{240,120,0,true,false},{100,118,0,true,true},
        {40,90,1,true,false},{60,80,2,true,false},{70,90,4,false,false}};
    const scr::DrivingParticleDraws b{{110,100,0,true,false},{190,118,0,true,true},
        {200,118,1,true,true},{80,60,2,false,false},{90,70,3,true,true},{80,80,4,false,false}};
    const auto mid=interpolateNativeDrivingParticles(a,b,0.5);
    check(mid.size()==a.size(),"Future particle birth appeared before its tick");
    check(mid[0].x==240&&mid[0].y==120,"Departing draw joined its replacement's trajectory");
    check(mid[1].x==105&&mid[1].y==109&&mid[1].spawned,"Continuing replacement did not interpolate");
    check(mid[2].x==40&&mid[2].y==90,"Recycled slot streaked across the screen");
    check(mid[3].x==60&&mid[3].y==80,"Dust interpolated into a spark");
    check(mid[4].x==75&&mid[4].y==85,"Continuing spark did not interpolate");
    check(same(interpolateNativeDrivingParticles(a,b,0),a)&&same(interpolateNativeDrivingParticles(a,b,1),b),"Particle endpoints differ");
    check(same(interpolateNativeDrivingParticles(a,{},0.75),a),"Retired particle invented a future position");
    check(interpolateNativeDrivingParticles({},b,0.75).empty(),"New particle appeared early");
    check(same(interpolateNativeDrivingParticles(a,b,0.5),mid),"Particle queries are not deterministic");

    scr::DrivingEffectsInputs inputs;inputs.offTrackFlags=128;inputs.wheelMovementActive=128;inputs.speedMagnitude=0x800;
    scr::DrivingEffectsState state;state.randomSeed=0x12345678;
    for(auto& particle:state.debris)particle={128,64,0,0};
    state.debris[0].y=127; // Draw, move out, then recycle the same slot this tick.
    scr::DrivingParticleDraws draws;(void)scr::updateGroundAndEdgeEffects(inputs,state,&draws);
    check(draws.size()==17,"Spawn metadata changed admitted draw count");
    check(std::count_if(draws.begin(),draws.end(),[](const auto& d){return d.spawned;})==1&&
        draws.back().index==0&&draws.back().spawned,"Particle lifetime boundary was not recorded");
    check(draws[15].index==0&&!draws[15].spawned&&draws[15].y==127,"Existing particle was labelled as a birth");
}
void chains(){
    ReplayRenderAssets assets;assets.palette[1]=0xff0000;
    CockpitFrame cockpit;
    for(unsigned id=19;id<=22;++id){
        auto& object=assets.objects[id];object.widthWords=1;object.sourceRows=8;object.words.resize(8);
        for(auto& word:object.words)word.planes[0]=0xffff;
        cockpit.objects[id]={std::uint16_t(id<21?64:240),0,1,8};
    }
    NativeChainState state{176,8};
    const auto a=advanceNativeChains(1,state,cockpit),b=advanceNativeChains(0,state,cockpit);
    std::vector<std::uint32_t> exact(64000,0x123456),image=exact;
    composeNativeChains(assets,a,exact);composeInterpolatedNativeChains(assets,a,b,0,image);
    check(image==exact,"Chain first endpoint differs");
    image.assign(64000,0x123456);composeInterpolatedNativeChains(assets,a,b,0.5,image);
    check(image[139*320+64]==assets.palette[1]&&image[140*320+64]==0x123456,"Chains did not move four pixels at the midpoint");
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<320;++x)if(image[y*320+x]!=0x123456)
        throw std::runtime_error("Interpolated chains leaked above the viewport");
    image.assign(64000,0x123456);exact=image;composeNativeChains(assets,b,exact);
    composeInterpolatedNativeChains(assets,a,b,1,image);check(image==exact,"Chain last endpoint differs");
    state={96,0};const auto final=advanceNativeChains(1,state,cockpit);
    image.assign(64000,0x123456);composeInterpolatedNativeChains(assets,final,{},0.5,image);
    check(image[39*320+64]==assets.palette[1]&&image[40*320+64]==0x123456,"Final chain links did not retract towards the top");
    image.assign(64000,0x123456);composeInterpolatedNativeChains(assets,final,{},1,image);
    check(std::count(image.begin(),image.end(),0x123456)==64000,"Final chain links survived removal");
    composeInterpolatedNativeChains(assets,{},a,0.5,image);
    check(std::count(image.begin(),image.end(),0x123456)==64000,"Hanging chains appeared before their tick");
}
}
int main()try{
    wheels();particles();chains();
    unsigned rejected=0;const auto nan=std::numeric_limits<double>::quiet_NaN();
    try{(void)interpolateNativeWheelSprites({},{},{},{},nan);}catch(const std::invalid_argument&){++rejected;}
    try{(void)interpolateNativeDrivingParticles({},{},nan);}catch(const std::invalid_argument&){++rejected;}
    try{std::vector<std::uint32_t> pixels;composeInterpolatedNativeChains({}, {}, {},nan,pixels);}catch(const std::invalid_argument&){++rejected;}
    check(rejected==3,"Nonfinite effect fraction was accepted");
    std::cout<<"{\"cases\":"<<cases<<",\"mismatches\":0,\"particle_lifetimes\":true,\"exact_endpoints\":true,\"chain_exit_clipped\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
