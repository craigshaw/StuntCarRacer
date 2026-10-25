#include "native_driving_particles.h"
#include <cmath>
#include <stdexcept>

namespace scr::viewer {
DrivingParticleDraws interpolateNativeDrivingParticles(const DrivingParticleDraws& from,
        const DrivingParticleDraws& to,double fraction){
    if(!std::isfinite(fraction))throw std::invalid_argument("Particle fraction must be finite");
    if(fraction<=0)return from;if(fraction>=1)return to;
    std::array<int,32> last{},continuing{};last.fill(-1);continuing.fill(-1);
    for(unsigned n=0;n<from.size();++n){
        if(from[n].index>=32)throw std::invalid_argument("Particle slot outside original pool");
        last[from[n].index]=int(n);
    }
    for(unsigned n=0;n<to.size();++n){
        if(to[n].index>=32)throw std::invalid_argument("Particle slot outside original pool");
        if(!to[n].spawned)continuing[to[n].index]=int(n);
    }
    auto result=from;
    for(unsigned n=0;n<result.size();++n){
        auto& draw=result[n];const int next=continuing[draw.index];
        // A slot can draw its departing particle and its replacement in one
        // tick. Only the final draw owns the lifetime that can continue.
        if(last[draw.index]!=int(n)||next<0||to[unsigned(next)].dust!=draw.dust)continue;
        const auto& target=to[unsigned(next)];
        draw.x=std::uint16_t(int(draw.x)+std::lround((int(target.x)-int(draw.x))*fraction));
        draw.y=std::uint16_t(int(draw.y)+std::lround((int(target.y)-int(draw.y))*fraction));
    }
    return result;
}
NativeDrivingParticleData decodeNativeDrivingParticleData(const NativeMemory& memory){
    NativeDrivingParticleData data;
    for(unsigned n=0;n<data.offsets.size();++n)data.offsets[n]=std::uint16_t((unsigned(memory[0x60f9c+n*2])<<8)|memory[0x60f9d+n*2]);
    for(unsigned n=0;n<data.animation.size();++n){
        data.animation[n]=memory[0x60fac+n];
        if(data.animation[n]>=data.offsets.size())throw std::invalid_argument("Dust animation selects an invalid original object");
    }
    return data;
}
void composeNativeDrivingParticles(const ReplayRenderAssets& assets,const NativeDrivingParticleData& data,
        const DrivingParticleDraws& draws,std::uint8_t frameCounter,std::vector<std::uint32_t>& pixels){
    if(pixels.size()!=320*200)throw std::invalid_argument("Particle rendering requires the original canvas");
    const auto plot=[&](int x,int y,unsigned colour){
        if(x>=32&&x<288&&y>=16&&y<144)pixels[unsigned(y)*320+unsigned(x)]=assets.palette[colour];
    };
    for(const auto& draw:draws){
        if(draw.index>=32||draw.x>=256||draw.y<1||draw.y>=128)throw std::invalid_argument("Particle draw is outside the admitted viewport");
        if(!draw.dust){
            if(draw.x>=254)throw std::invalid_argument("Edge spark exceeds its original clipping boundary");
            const int x=32+draw.x,y=16+draw.y;
            // The original plot helper retains its word pointer when D4 is
            // incremented. At bit 15 the second column wraps within that word.
            const int right=32+int((draw.x&~15u)|((draw.x+1u)&15u));
            plot(x,y,3);plot(right,y,3);plot(x,y-1,3);plot(right,y-1,15);continue;
        }
        const auto phase=data.animation[(unsigned(draw.index)+frameCounter)&15];
        if(phase>=data.offsets.size())throw std::invalid_argument("Dust animation selects an invalid original object");
        const auto& object=assets.objects[29+phase];
        const int left=32+int(draw.x)-data.offsets[phase],top=16+draw.y;
        for(unsigned y=0;y<object.sourceRows;++y)for(unsigned group=0;group<object.widthWords;++group){
            const auto& word=object.words[y*object.widthWords+group];
            for(unsigned bit=0;bit<16;++bit){
                const auto mask=0x8000u>>bit;if(word.preserve&mask)continue;
                unsigned colour=0;for(unsigned plane=0;plane<4;++plane)if(word.planes[plane]&mask)colour|=1u<<plane;
                plot(left+int(group*16+bit),top+int(y),colour);
            }
        }
    }
}
} // namespace scr::viewer
