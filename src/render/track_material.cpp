#include "render/track_material.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace scr {
namespace {
struct Color { double r{},g{},b{}; };
Color blend(Color a,Color b,double t){return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};}
Color unpack(std::uint32_t c){return {double((c>>16)&255),double((c>>8)&255),double(c&255)};}
std::uint32_t pack(Color c){
    const auto byte=[](double v){return std::uint32_t(std::clamp(std::round(v),0.0,255.0));};
    return (byte(c.r)<<16)|(byte(c.g)<<8)|byte(c.b);
}
std::uint32_t noise(unsigned x,unsigned y){
    auto n=x*0x1f123bb5u+y*0x5f356495u+0x6ac690c5u;
    n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16);
}
using Mips=std::array<std::vector<Color>,7>;
Mips makeTexture(bool road){
    Mips mips;mips[0].resize(64*64);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
        const auto grain=double(noise(x,y)&255)/255.0;
        Color c;
        if(road){const auto value=58+grain*24;c={value,value-2,value-7};}
        else{
            auto value=178+grain*10;
            // Steel panel joints and recessed fasteners. The entire tile repeats.
            if(x<2||y<2)value-=45;
            if(x==2||y==2)value+=20;
            for(unsigned cy:{6u,57u})for(unsigned cx:{6u,57u}){
                const auto ax=int(x)-int(cx),ay=int(y)-int(cy);
                if(ax*ax+ay*ay<=3)value-=70;
            }
            c={value,value,value};
        }
        mips[0][y*64+x]=c;
    }
    for(unsigned level=1;level<mips.size();++level){
        const auto size=64u>>level,previous=size*2;mips[level].resize(size*size);
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
            const auto a=blend(mips[level-1][(2*y)*previous+2*x],mips[level-1][(2*y)*previous+2*x+1],0.5);
            const auto b=blend(mips[level-1][(2*y+1)*previous+2*x],mips[level-1][(2*y+1)*previous+2*x+1],0.5);
            mips[level][y*size+x]=blend(a,b,0.5);
        }
    }
    return mips;
}
Color sample(const Mips& mips,double u,double v,double footprint){
    const auto lod=std::clamp(std::log2(std::max(1.0,footprint*64)),0.0,6.0);
    const auto level=unsigned(lod);
    const auto at=[&](unsigned l){
        const auto size=64u>>l;
        const auto x=(u-std::floor(u))*size-0.5,y=(v-std::floor(v))*size-0.5;
        const auto ix=int(std::floor(x)),iy=int(std::floor(y));
        const auto texel=[&](int a,int b){return mips[l][(unsigned(b)&(size-1))*size+(unsigned(a)&(size-1))];};
        return blend(blend(texel(ix,iy),texel(ix+1,iy),x-ix),blend(texel(ix,iy+1),texel(ix+1,iy+1),x-ix),y-iy);
    };
    return blend(at(level),at(std::min(6u,level+1)),lod-level);
}
double band(double u,double width,double first,double last){
    width=std::max(width,0.000001);
    return std::clamp((std::min(u+width/2,last)-std::max(u-width/2,first))/width,0.0,1.0);
}
}
std::uint32_t sampleTrackMaterial(bool road,std::uint32_t originalColor,
    TrackTextureCoordinate uv,TrackTextureCoordinate dx,TrackTextureCoordinate dy){
    static const auto asphalt=makeTexture(true),steel=makeTexture(false);
    const auto scale=road?4.0:1.0;
    const auto footprint=std::max({std::abs(dx.u)*scale,std::abs(dx.v),std::abs(dy.u)*scale,std::abs(dy.v)});
    auto c=sample(road?asphalt:steel,uv.u*scale,uv.v,footprint);
    const auto original=unpack(originalColor);
    if(road){
        c=blend(c,original,0.12);
        const auto width=std::max(std::abs(dx.u),std::abs(dy.u));
        const auto paint=std::min(1.0,band(uv.u,width,0.035,0.052)+band(uv.u,width,0.948,0.965));
        c=blend(c,{219,193,110},paint*0.9);
    }else c={original.r*c.r/190,original.g*c.g/190,original.b*c.b/190};
    return pack(c);
}
} // namespace scr
