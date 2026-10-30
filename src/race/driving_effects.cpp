#include "race/driving_effects.h"
#include <stdexcept>

namespace scr {
namespace {
int signedWord(std::uint16_t x) { return x<0x8000 ? int(x) : int(x)-65536; }
int signedByte(std::uint8_t x) { return x<0x80 ? int(x) : int(x)-256; }
int floorDivide(int x,int divisor) { return x/divisor-(x%divisor<0); }
std::uint8_t randomByte(DrivingEffectsState& s) {
    const auto result=std::uint8_t((s.randomSeed>>4)^((s.randomSeed>>16)>>1));
    s.randomSeed=(s.randomSeed<<8)|s.randomTail;s.randomTail=result;return result;
}
bool validateParticle(unsigned i,const DrivingEffectsInputs& in,DrivingEffectsState& s,DrivingParticleDraws* draws,bool spawned=false) {
    auto& p=s.debris[i];s.particleScratch=std::uint8_t(i*2);
    if(p.y>=128||p.y<1||p.x>=256||(!(in.offTrackFlags&0x80)&&p.x>=254)) {
        p.y=210;return false;
    }
    if(draws)draws->push_back({p.x,p.y,std::uint8_t(i),(in.offTrackFlags&0x80)!=0,spawned});
    return true;
}
void updateParticles(unsigned count,const DrivingEffectsInputs& in,DrivingEffectsState& s,DrivingParticleDraws* draws) {
    s.particleLimit=std::uint8_t((count-1)*2);
    if(!in.wheelMovementActive) { for(auto& p:s.debris)p.y=212;return; }
    for(unsigned n=count;n>0;--n) {
        auto& p=s.debris[n-1];
        if(validateParticle(n-1,in,s,draws)) {
            p.velocityY=std::uint16_t(p.velocityY+2);
            p.y=std::uint16_t(p.y+p.velocityY);p.x=std::uint16_t(p.x+p.velocityX);
        }
    }
    for(unsigned n=count;n>0;--n) {
        auto& p=s.debris[n-1];if(p.y<128)continue;
        const auto jitter=randomByte(s)&7;
        auto speed=unsigned(s.impactLevel)>>1;
        if(!(in.offTrackFlags&0x80))speed>>=1;
        p.velocityY=std::uint16_t(~(speed+jitter));
        if(in.offTrackFlags&0x80) {
            p.x=randomByte(s);p.y=std::uint16_t((randomByte(s)&7)+118);
        } else {
            p.x=std::uint16_t((randomByte(s)&127)+64);
            p.y=std::uint16_t((std::uint16_t(randomByte(s))|0xfff8u)+127);
        }
        p.velocityX=std::uint16_t(floorDivide(signedWord(std::uint16_t(p.x-128)),8));
        validateParticle(n-1,in,s,draws,true);
    }
}
void wreck(const DrivingEffectsInputs& in,DrivingEffectsState& s) {
    if(s.blinkCountdown)return;
    if(s.gameModeFlags!=0x40&&s.gameModeFlags!=0x80)s.gameModeFlags=in.networkMode;
    s.postWreck=2;s.wheelBaseHeight=0x92;s.visualEffectHighByte=0x82;s.blinkCountdown=0x3c;
    s.messageIndex=0;s.messageMode=2;s.messageActive=0x80;
}
void drawHole(const DrivingEffectsInputs& in,DrivingEffectsState& s,bool fresh) {
    const unsigned coordinate=std::uint8_t(unsigned(s.holePosition)*3+6);
    const unsigned variant=(fresh?2u:0u)+(coordinate&1u);
    const int startX=int((coordinate>>1)*16)-32;
    const auto& sprite=in.holeSprites[variant];
    for(unsigned row=0;row<8;++row)for(unsigned column=0;column<32;++column){
        // Position10 draws at physical x288..319, outside the feedback strip.
        // Its visual event is still returned; those raster pixels cannot be
        // read by the damage routine (whose largest feedback x is279).
        const int x=startX+int(column);if(x<0||x>=256)continue;
        const auto& word=sprite[row*2+column/16];const unsigned bit=0x8000u>>(column&15);
        std::uint8_t color=0;for(unsigned plane=0;plane<4;++plane)if(word.planes[plane]&bit)color=std::uint8_t(color|(1u<<plane));
        const auto pixel=row*256+unsigned(x);
        const auto preserve=std::uint8_t((word.preserveMask&bit)?15:0);
        s.damagePixelColors[pixel]=std::uint8_t((s.damagePixelColors[pixel]&preserve)|color);
        s.damageOtherPixelColors[pixel]=std::uint8_t((s.damageOtherPixelColors[pixel]&preserve)|color);
        s.damagePixels[pixel]=s.damagePixelColors[pixel]!=0;s.damagePixelTouched[pixel]=true;
    }
}
void advanceDamageBar(const DrivingEffectsInputs& in,DrivingEffectsState& s,DrivingVisualEvents& events) {
    for(unsigned steps=0;steps<512;++steps) {
        if(s.targetDamage<=s.carDamage)return;
        ++s.carDamage;auto y=s.damageVerticalOffset;
        if(!(s.carDamage&1)) {
            auto r=randomByte(s);
            if(r&1) {
                r>>=1;
                if(r&1) {
                    if(y>=5) { r>>=1;if(r&1)--y;else goto moveUp; }
                    ++y;
                } else {
moveUp:
                    if(y<3) { r>>=1;if(!(r&1)){++y;goto choseY;}++y; }
                    --y;
                }
            }
        }
choseY:
        s.damageColor=signedByte(y)>signedByte(s.damageVerticalOffset)?12:11;s.damageVerticalOffset=y;
        if(s.carDamage>=240) { --s.carDamage;wreck(in,s);return; }
        const auto x=unsigned(s.carDamage)+8;const auto row=unsigned(y&7);
        if(s.damagePixels[row*256+x]) {
            if(row<2)throw std::out_of_range("Damage strip draws above its admitted eight-row canvas");
            s.damagePixels[row*256+x]=false;s.damagePixels[(row-1)*256+x]=false;
            s.damagePixels[(row-2)*256+x]=true;events.damageBarChanged=true;
            for(unsigned offset=0;offset<3;++offset){
                const auto pixel=(row-offset)*256+x;s.damagePixelTouched[pixel]=true;
                s.damagePixelColors[pixel]=offset==2?s.damageColor:0;
                s.damageOtherPixelColors[pixel]=s.damagePixelColors[pixel];
            }
        } else {
            ++s.targetDamage;
            if(!s.targetDamage){wreck(in,s);return;}
            s.accumulatedForce.fill(s.targetDamage);
        }
    }
    throw std::runtime_error("Damage progression did not converge");
}
} // namespace

void updateEnginePitch(const DrivingEffectsInputs& in,DrivingEffectsState& s) {
    std::uint16_t speed;
    if(in.wheelMovementActive) {
        speed=std::uint16_t(in.localForwardSpeed&0xfff0);
        if(speed&0x8000)speed=std::uint16_t(0-speed);
    } else speed=(in.inputFlags&3)?0x9000:0;
    speed=std::uint16_t(speed+0x580);speed>>=3;
    auto delta=signedWord(in.enginePitchAccumulator)<0xc0 ? std::uint16_t(2)
        : std::uint16_t(floorDivide(signedWord(std::uint16_t(speed-in.enginePitchAccumulator)),8));
    const auto high=std::uint8_t(delta>>8);
    if(high&&!(high&0x80))delta=0x100;
    else if(high&0x80) {
        if(in.wheelMovementActive) { if(high!=0xff)delta=0xff00; }
        else if(high!=0xff||std::uint8_t(delta)<0xe0)delta=0xffe0;
    }
    s.enginePitchDelta=delta;randomByte(s);s.engineNoiseFlag=0;
}
std::vector<std::uint8_t> updateGroundAndEdgeEffects(const DrivingEffectsInputs& in,DrivingEffectsState& s,DrivingParticleDraws* draws) {
    std::vector<std::uint8_t> sounds;
    const auto speedHigh=std::uint8_t(in.speedMagnitude>>8);
    if(!in.raceStartTimer&&(in.offTrackFlags&0x80)) {
        s.grindSample=6;s.impactLevel=signedByte(speedHigh)<16?speedHigh:16;
        s.groundSamplePeriod=std::uint16_t((randomByte(s)&0x1c)+0x1c2);
        if(in.wheelMovementActive)sounds.push_back(6);
        updateParticles(in.networkMode?4:16,in,s,draws);
    }
    s.grindSample=1;
    if((s.bouncePhase||s.postWreck)&&!(in.offTrackFlags&0x80)&&signedByte(speedHigh)>=1) {
        s.impactLevel=signedByte(speedHigh)<50?speedHigh:50;randomByte(s);
        const auto pitch=std::uint8_t((s.impactLevel>>1)^31);
        s.edgeSamplePeriod=std::uint16_t(pitch*4+0xaa);
        if(in.wheelMovementActive)sounds.push_back(1);
        updateParticles(32,in,s,draws);
    }
    return sounds;
}
void updateWheelVisuals(const DrivingEffectsInputs& in,const TrigTable& trig,DrivingEffectsState& s) {
    for(unsigned i=0;i<2;++i) {
        auto travel=std::uint16_t(in.wheelTravel[i]+0x100);
        if(travel&0x8000)travel=0;
        if(travel>=0x800)travel=0x7ff;
        const auto sample=trig[(travel>>3)^0xff];
        const auto rotated=std::uint16_t((unsigned(sample)<<5)|(sample>>11));
        auto height=std::uint8_t(std::uint8_t(~(rotated&31))+s.wheelBaseHeight);
        const bool bounced=(s.bouncePhase&0x80)!=0;s.bouncePhase=std::uint8_t(s.bouncePhase<<1);
        if(bounced||height>=0xba)height=0xb9;
        if(height<0x97)height=0x97;
        s.wheelVisualHeight[i]=std::uint8_t(height-0x32);
    }
    s.spriteUpdatePending=0x80;
}
std::vector<std::uint8_t> updateDamageFeedback(const DrivingEffectsInputs& in,DrivingEffectsState& s,DrivingVisualEvents& visual) {
    std::vector<std::uint8_t> sounds;
    s.damagePixelTouched.fill(false);
    if(s.damageActive) {
        const auto front=(unsigned(s.accumulatedForce[0])+s.accumulatedForce[1])/2;
        s.targetDamage=std::uint8_t((front+s.accumulatedForce[2])/2);
        advanceDamageBar(in,s,visual);
    }
    if(s.majorImpactCooldown) {
        const auto before=s.majorImpactCooldown--;
        if(before==0x45){visual.redrawExistingHole=true;drawHole(in,s,false);}
        else if(!s.damageActive)return sounds;
    } else {
        if(!s.damageActive)return sounds;
        if(in.maxCompression>=0x1400&&s.holePosition) {
            --s.holePosition;visual.drawNewHole=true;drawHole(in,s,true);s.majorImpactCooldown=0x45;
            sounds.push_back(5);s.damageActive=0;return sounds;
        }
    }
    auto volume=std::uint8_t(in.maxCompression>>8);if(volume<7)volume=7;
    volume=std::uint8_t(volume<<2);if(volume>=64)volume=64;
    s.creakVolume=volume;sounds.push_back(4);s.damageActive=0;return sounds;
}
} // namespace scr
