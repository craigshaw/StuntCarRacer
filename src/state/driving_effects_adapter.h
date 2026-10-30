#pragma once
#include "race/driving_effects.h"
#include "track_sampling_adapter.h"
#include <stdexcept>
#include <vector>

namespace scr::state::effects {
using Ram=scr::state::memory::ChipRam;
using scr::state::memory::readWord;using scr::state::memory::readLong;using scr::state::memory::writeWord;using scr::state::memory::writeLong;
inline DrivingEffectsInputs readInputs(const Ram& r){
    DrivingEffectsInputs in{readWord(r,0x1bd30),readWord(r,0x1bd5c),readWord(r,0x1bc34),readWord(r,0x1bc3a),
        {readWord(r,0x1bd14),readWord(r,0x1bd16)},r[0x1bb7e],r[0x1bb47],r[0x1bb9c],r[0x57c3c],r[0x1bbdf]};
    for(unsigned id=0;id<4;++id){const auto source=readLong(r,0x6a4ac+(0x17+id)*4);
        if(source<0x1000||(source&1)||source>r.size()-160||readWord(r,0x6a16c+(0x17+id)*16+4)!=1||readWord(r,0x6a16c+(0x17+id)*16+6)!=7)
            throw std::invalid_argument("Hole sprite is outside recovered32x8 asset domain");
        for(unsigned n=0;n<16;++n){auto& word=in.holeSprites[id][n];word.preserveMask=readWord(r,source+10*n);
            for(unsigned plane=0;plane<4;++plane)word.planes[plane]=readWord(r,source+10*n+2+2*plane);}}
    return in;
}
inline void writeInputs(Ram& r,const DrivingEffectsInputs& in){
    writeWord(r,0x1bd30,in.localForwardSpeed);writeWord(r,0x1bd5c,in.speedMagnitude);writeWord(r,0x1bc34,in.enginePitchAccumulator);
    writeWord(r,0x1bc3a,in.maxCompression);writeWord(r,0x1bd14,in.wheelTravel[0]);writeWord(r,0x1bd16,in.wheelTravel[1]);
    r[0x1bb7e]=in.wheelMovementActive;r[0x1bb47]=in.inputFlags;r[0x1bb9c]=in.offTrackFlags;r[0x57c3c]=in.networkMode;r[0x1bbdf]=in.raceStartTimer;
}
#define SCR_DRIVING_EFFECT_WORDS(X) \
 X(enginePitchDelta,0x1bbf4) X(edgeSamplePeriod,0x1b6e2) X(groundSamplePeriod,0x1b732)
#define SCR_DRIVING_EFFECT_BYTES(X) \
 X(randomTail,0x625a6) X(engineNoiseFlag,0x5b646) X(grindSample,0xf360) X(impactLevel,0x1bbd7) \
 X(particleLimit,0x1bba7) X(particleScratch,0x1bbe4) X(bouncePhase,0x1bbda) X(wheelBaseHeight,0x1bbdd) X(spriteUpdatePending,0x69f41) \
 X(damageActive,0x1bb54) X(targetDamage,0x1bbb6) X(carDamage,0x1bb55) X(damageVerticalOffset,0x662aa) X(damageColor,0x662ab) \
 X(majorImpactCooldown,0x1bb73) X(holePosition,0x1c9cf) X(creakVolume,0x1b715) X(blinkCountdown,0x1bb6c) \
 X(gameModeFlags,0x1bbe2) X(postWreck,0x1bca2) X(visualEffectHighByte,0x1bc56) X(messageIndex,0x5e65b) X(messageMode,0x5e65a) X(messageActive,0x1bb8e)
inline unsigned damageBuffer(const Ram& r){
    const auto base=readLong(r,0x6a584);
    // Two captured 320x200 four-plane frames; the original accesses the second
    // through a fixed +0x7d00 displacement, independent of its pointer slot.
    if((base&1)||base<0x1000||base>r.size()-0xfa00)throw std::invalid_argument("Damage framebuffer pair is outside Chip RAM");
    return base;
}
inline unsigned damagePixelAddress(unsigned frame,unsigned x,unsigned y,unsigned plane){return frame+4+y*40+x/8+plane*0x1f40;}
inline bool occupied(const Ram& r,unsigned frame,unsigned x,unsigned y){
    const auto mask=std::uint8_t(0x80u>>(x&7));
    for(unsigned plane=0;plane<4;++plane)if(r[damagePixelAddress(frame,x,y,plane)]&mask)return true;return false;
}
inline std::uint8_t pixelColor(const Ram& r,unsigned frame,unsigned x,unsigned y){
    const auto mask=std::uint8_t(0x80u>>(x&7));std::uint8_t color=0;
    for(unsigned plane=0;plane<4;++plane)if(r[damagePixelAddress(frame,x,y,plane)]&mask)color=std::uint8_t(color|(1u<<plane));return color;
}
inline void setDamagePixel(Ram& r,unsigned frame,unsigned x,unsigned y,std::uint8_t color){
    const auto mask=std::uint8_t(0x80u>>(x&7));
    for(unsigned plane=0;plane<4;++plane){auto& byte=r[damagePixelAddress(frame,x,y,plane)];
        byte=std::uint8_t((byte&~mask)|((color&(1u<<plane))?mask:0));}
}
inline DrivingEffectsState readState(const Ram& r){
    DrivingEffectsState s{};s.randomSeed=readLong(r,0x625a2);
#define READ_WORD(field,address) s.field=readWord(r,address);
    SCR_DRIVING_EFFECT_WORDS(READ_WORD)
#undef READ_WORD
#define READ_BYTE(field,address) s.field=r[address];
    SCR_DRIVING_EFFECT_BYTES(READ_BYTE)
#undef READ_BYTE
    for(unsigned i=0;i<32;++i)s.debris[i]={readWord(r,0x1c380+2*i),readWord(r,0x1c3c0+2*i),readWord(r,0x1c400+2*i),readWord(r,0x1c440+2*i)};
    s.wheelVisualHeight={readWord(r,0x6a1a6),readWord(r,0x6a176)};
    for(unsigned i=0;i<3;++i)s.accumulatedForce[i]=r[0x1bb4f+i];
    const auto frame=damageBuffer(r);for(unsigned y=0;y<8;++y)for(unsigned x=0;x<256;++x){const unsigned pixel=y*256+x;
        s.damagePixelColors[pixel]=pixelColor(r,frame,x,y);s.damageOtherPixelColors[pixel]=pixelColor(r,frame+0x7d00,x,y);
        s.damagePixels[pixel]=s.damagePixelColors[pixel]!=0;}
    return s;
}
inline void writeState(Ram& r,const DrivingEffectsState& s){
    writeLong(r,0x625a2,s.randomSeed);
#define WRITE_WORD(field,address) writeWord(r,address,s.field);
    SCR_DRIVING_EFFECT_WORDS(WRITE_WORD)
#undef WRITE_WORD
#define WRITE_BYTE(field,address) r[address]=s.field;
    SCR_DRIVING_EFFECT_BYTES(WRITE_BYTE)
#undef WRITE_BYTE
    for(unsigned i=0;i<32;++i){const auto& p=s.debris[i];writeWord(r,0x1c380+2*i,p.x);writeWord(r,0x1c3c0+2*i,p.y);writeWord(r,0x1c400+2*i,p.velocityX);writeWord(r,0x1c440+2*i,p.velocityY);}
    for(unsigned i=0;i<2;++i){
        const auto height=s.wheelVisualHeight[i];const unsigned wheel=i==0?0x30:0;
        const unsigned secondary=0x250+(height<0x7e?0x60:0);
        for(unsigned part=0;part<3;++part){const unsigned record=0x6a16c+wheel+part*0x10;
            writeWord(r,record+0xa,height);writeWord(r,record+secondary+0xa,height);writeWord(r,record+6,std::uint16_t(0x9e-height));}
    }
    for(unsigned i=0;i<3;++i)r[0x1bb4f+i]=s.accumulatedForce[i];
    const auto frame=damageBuffer(r);
    for(unsigned pixel=0;pixel<s.damagePixelTouched.size();++pixel)if(s.damagePixelTouched[pixel]){
        setDamagePixel(r,frame,pixel%256,pixel/256,s.damagePixelColors[pixel]);
        setDamagePixel(r,frame+0x7d00,pixel%256,pixel/256,s.damageOtherPixelColors[pixel]);
    }
}
#undef SCR_DRIVING_EFFECT_WORDS
#undef SCR_DRIVING_EFFECT_BYTES
} // namespace scr::state::effects
