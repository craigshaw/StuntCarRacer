#include "race/race_restart.h"
#include "physics/motion_transforms.h"
#include <stdexcept>
namespace scr {
void preparePlayerRestart(const RaceRestartData& d,std::uint8_t requested,RaceRestartState& s) {
    if(!d.track.segmentCount||d.track.segmentCount>100||requested>=d.track.segmentCount)
        throw std::invalid_argument("Restart requires a loaded track and valid segment");
    auto segment=requested;bool found=false;
    for(unsigned attempts=0;attempts<d.track.segmentCount;++attempts) {
        const auto type=d.track.properties[segment]&15;
        bool feature=false;
        if(d.featureCount){auto index=std::uint8_t(d.featureCount-1);do {
            if(d.features[index]==segment){feature=true;break;}
            index=std::uint8_t(index-1);
        }while(!(index&0x80));}
        if(!(d.geometryRestartFlags[type]&0x80)&&!feature){found=true;break;}
        segment=segment?std::uint8_t(segment-1):std::uint8_t(d.track.segmentCount-1);
    }
    if(!found)throw std::invalid_argument("No restartable segment in loaded track");
    s.currentSegment=s.playerSegment=segment;s.vehicle={};s.raceStartTimer=0xf0;
    s.segment=loadTrackSegmentConfiguration(d.track,segment);
    const auto coordinates=d.segmentCoordinates[segment];s.gridLookupX=coordinates&15;s.gridLookupZ=coordinates>>4;
    s.vehicle.worldPosition={std::uint32_t(s.gridLookupX*128+64)<<16,0x04000000,std::uint32_t(s.gridLookupZ*128+64)<<16};
    const auto diagonal=(s.segment.geometryType==4||s.segment.geometryType==10)?32:0;
    s.vehicle.angles[1]=std::uint16_t(std::uint8_t((s.segment.slopeFlags^s.segment.reverseDirection)+diagonal))<<8;
}
void applyRestartLateralOffset(const TransformMatrices& m,RaceRestartState& s) noexcept {
    // Original negates the Z coefficient as a word before Q15 multiplication.
    const auto side=std::uint16_t((s.trackSide&0x80)?0xb000:0x5000);
    const auto x=multiplyMatrixWord(m.words[3],side);
    const auto z=multiplyMatrixWord(std::uint16_t(0-m.words[2]),side);
    const auto expand=[](std::uint16_t v){const int n=v<0x8000?int(v):int(v)-65536;return std::uint32_t(n*64);};
    s.vehicle.worldPosition[0]+=expand(x);s.vehicle.worldPosition[2]+=expand(z);
    s.trackSideCopy=s.trackSide;s.multiplierScratch=0xa0;
}
void finishPlayerRestart(bool complete,const TransformMatrices& matrices,RaceRestartState& s) noexcept {
    const auto rear=s.wheelHeights[2];s.vehicle.worldPosition[1]=0x00100000;
    if(complete){s.vehicle.worldPosition[1]=(rear<<9)+0x00180000;s.raceStartTimer=0xe6;}
    s.trackSurfaceHeight=std::uint16_t(rear>>2);applyRestartLateralOffset(matrices,s);
    s.wheelHeights.fill(0x1000);s.trackSurfaceHeights.fill(0x1000);s.vehicle.previousSuspension.fill(0);
}
void finishRestartChain(RaceRestartState& s) noexcept {s.chainVerticalPosition=0xb0;s.chainLiftVelocity=8;}
void initializeRestartDebris(std::array<std::uint16_t,32>& positions) noexcept {positions.fill(0xd4);}
} // namespace scr
