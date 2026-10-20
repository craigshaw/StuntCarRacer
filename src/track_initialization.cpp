#include "track_initialization.h"
#include "track_sampling.h"
#include <algorithm>
#include <stdexcept>

namespace scr {
namespace {
using Ram=NativeMemory;
std::uint8_t b(int value){return static_cast<std::uint8_t>(value);}
std::uint16_t w(int value){return static_cast<std::uint16_t>(value);}
int sw(std::uint16_t value){return value<0x8000?int(value):int(value)-65536;}
std::uint16_t word(const Ram& r,unsigned a){return std::uint16_t((unsigned(r.at(a))<<8)|r.at(a+1));}
std::uint32_t longword(const Ram& r,unsigned a){return (std::uint32_t(word(r,a))<<16)|word(r,a+2);}
void putWord(Ram& r,unsigned a,std::uint16_t value){r.at(a)=std::uint8_t(value>>8);r.at(a+1)=std::uint8_t(value);}
void putLong(Ram& r,unsigned a,std::uint32_t value){putWord(r,a,std::uint16_t(value>>16));putWord(r,a+2,std::uint16_t(value));}
unsigned address(std::uint16_t encoded){return 0x1ef82+std::uint16_t(((encoded<<8)|(encoded>>8))-0xb100);}
std::uint8_t randomByte(Ram& r){
    const auto result=b(int((word(r,0x625a4)>>4)^(word(r,0x625a2)>>1)));
    putLong(r,0x625a2,(longword(r,0x625a2)<<8)|r[0x625a6]);r[0x625a6]=result;return result;
}
TrackGeometryData decodeData(const Ram& r){
    TrackGeometryData d;d.segmentCount=r[0x1ca1a];
    for(unsigned n=0;n<100;++n){d.primaryGeometry[n]=r[0x1c4c0+n];d.alternateGeometry[n]=r[0x1c524+n];d.properties[n]=r[0x1c5ec+n];d.primaryBase[n]=word(r,0x1c650+2*n);d.alternateBase[n]=word(r,0x1c718+2*n);}
    d.geometry.assign(r.begin()+0x1ef82,r.begin()+0x2f082);return d;
}
void configuration(Ram& r,const TrackSegmentConfiguration& c){
    putWord(r,0x1bc8c,c.primaryOffset);putWord(r,0x1bc90,c.alternateOffset);putWord(r,0x1bcbc,c.descriptorOffset);
    putWord(r,0x1bc0e,c.primaryBase);putWord(r,0x1bc10,c.alternateBase);
    r[0x1bb79]=c.format;r[0x1bbdc]=c.alternateFlag;r[0x1bc4a]=c.slopeFlags;r[0x1bc32]=c.reverseDirection;r[0x1bb86]=c.geometryType;
    r[0x1bb4d]=c.steeringFlags;r[0x1bb97]=c.limit;r[0x1bb59]=c.limitDoubled;r[0x1bb98]=c.maximum;r[0x1bb5a]=c.maximumDoubled;r[0x1bb6a]=c.mode;
    r[0x1bc44]=c.directionInversion;r[0x1bb7b]=c.primaryOrientation;r[0x1bbd9]=c.alternateOrientation;r[0x1bbd4]=c.steeringBase;
}
std::uint16_t rawHeight(Ram& r,const TrackSegmentConfiguration& c,bool alternate,unsigned section){
    const auto base=address(alternate?c.alternateOffset:c.primaryOffset);std::uint16_t result;
    if(c.format&0x80){result=std::uint16_t(((r.at(base+2*section)&0x7f)<<8)|r.at(base+2*section+1));}
    else{const auto value=r.at(base+section);result=std::uint16_t(((value&15)<<8)|((value<<1)&0xe0));}
    return result;
}
std::uint16_t interpolation(Ram& r,const TrackSegmentConfiguration& c,bool alternate,unsigned section){
    if(alternate)putWord(r,0x1bc8c,c.alternateOffset);
    const auto value=rawHeight(r,c,alternate,section);r[0x1bb1b]=b(value);r[0x1bb1a]=b(value>>8);return value;
}
void difficulty(Ram& r,unsigned trackId){
    // Original0x63a8e; only subsequent heats with additional players retain
    // their previously chosen difficulty. Practice/fresh race takes this path.
    if(r[0x5eb76]&&r[0x1ca35])return;
    for(unsigned i=0;i<256;++i)(void)randomByte(r);
    unsigned index=trackId;auto damage=r[0x1ca2a];
    if(r[0x1c9d0]){index+=32;damage=r[0x1ca2b];}
    r[0x63ce2]=damage;r[0x63ce0]=b((randomByte(r)&r.at(0x1fe2c+index))+r.at(0x1fe34+index));
    r[0x63ce1]=b((randomByte(r)&r.at(0x1fe3c+index))+r.at(0x1fe44+index));
}
void parse(Ram& r,unsigned trackId){
    const auto encoded=word(r,0x1f0a2+2*trackId);putWord(r,0x1bcc0,encoded);auto cursor=address(encoded);
    auto read=[&](){return r.at(cursor++);};
    for(unsigned i=1;i<=4;++i)r[0x1ca19+i]=read();
    if(!r[0x1ca1a]||r[0x1ca1a]>100)throw std::runtime_error("Encoded track has invalid segment count");
    const auto low=read();r[0x1bbf7]=low;r[0x1bbf9]=low;const auto high=read();r[0x1bbf6]=high;r[0x1bbf8]=high;
    r[0x1bbdc]=0;putWord(r,0x1bc2a,0);r[0x1bb48]=0;
    for(unsigned index=0;index<r[0x1ca1a];++index){
        unsigned repeats=0;std::uint8_t coordinate{};
        for(;;){
            if(++repeats>16)throw std::runtime_error("Invalid encoded track repeat stream");
            if(r[0x1bb48]){
                --r[0x1bb48];auto properties=r[0x1bc55];r[0x1bb1a]=properties;r[0x1c5ec+index]=properties;
                if(properties&0x10)r[0x1bb1a]^=0xc0;
                const int delta=(r[0x1bb1a]&0x40)?1:16;
                coordinate=b(r[0x1bb8b]+((r[0x1bb1a]&0x80)?-delta:delta));break;
            }
            const auto properties=read();r[0x1bb1a]=properties;r[0x1c5ec+index]=properties;
            if((properties&15)==15){r[0x1bb48]=properties>>4;continue;}
            r[0x1bc55]=properties;coordinate=read();break;
        }
        r[0x1c588+index]=coordinate;r[0x1bb8b]=coordinate;r[0x1bb1b]=b((r[0x1bbdc]&2)<<6);
        const auto type=r[0x1bb1a]&15;std::uint8_t alternate;
        if(type>=12){r[0x1c5ec+index]&=0xf0;r[0x1c4c0+index]=r.at(0x5b2b8+type);alternate=r.at(0x5b2ba+type);}
        else{r[0x1c4c0+index]=read();alternate=(r[0x1bb1a]&0x20)?r[0x1c4c0+index]:read();}
        r[0x1c524+index]=b((alternate&0x7f)|r[0x1bb1b]);putWord(r,0x1c7e0+2*index,w(word(r,0x1bc2a)<<5));
        const auto c=loadTrackSegmentConfiguration(decodeData(r),b(index));configuration(r,c);
        r[0x1bbdc]=b((c.alternateFlag+c.maximum)&2);putWord(r,0x1bc2a,w(word(r,0x1bc2a)+c.mode));
        const auto primary0=interpolation(r,c,false,0);const auto primaryBase=w(word(r,0x1bbf6)-primary0);putWord(r,0x1c650+2*index,primaryBase);
        putWord(r,0x1bbf6,w(primaryBase+interpolation(r,c,false,c.mode)));
        const auto alternate0=interpolation(r,c,true,0);const auto alternateBase=w(word(r,0x1bbf8)-alternate0);putWord(r,0x1c718+2*index,alternateBase);
        putWord(r,0x1bbf8,w(alternateBase+interpolation(r,c,true,c.mode)));
    }
    r[0x1ca32]=b((r[0x1ca1c]+1)%r[0x1ca1a]);putWord(r,0x1ca1e,w(word(r,0x1bc2a)<<5));
    for(unsigned i=0;i<6;++i)r[0x1ca2a+i]=read();
    if(r[0x1ca2e]>32||r[0x1ca2f]>32)throw std::runtime_error("Encoded track metadata exceeds buffers");
    for(unsigned i=0;i<r[0x1ca2e];++i){r[0x1c8a8+i]=read();r[0x1c8c8+i]=read();}
    for(unsigned i=0;i<r[0x1ca2f];++i)r[0x1c8e8+i]=read();
    difficulty(r,trackId);r[0x1bb48]=0;r[0x1bb1b]=0x7c;
    // Entry branches directly to the loop tail:2 decrements to1 before the
    // first backward sweep, then to0 when that single sweep is complete.
    {
        r[0x1bb90]=1;
        for(int i=r[0x1ca1a]-1;i>=0;--i){
            int obstacle=-1;for(int n=r[0x1ca2e]-1;n>=0;--n)if(r[0x1c8a8+unsigned(n)]==i){obstacle=n;break;}
            if(obstacle>=0){const auto value=r[0x1c8c8+unsigned(obstacle)];r[0x1c952+unsigned(i)]=value;if(value&0x80)r[0x1bb48]=3;r[0x1bb1b]=value&0x7f;}
            else{
                std::uint8_t value;
                if(r.at(0x1f0c2+(r[0x1c5ec+unsigned(i)]&15))&0x80){r[0x1bb1b]=b(r[0x63ce1]-10);value=r[0x63ce1];}
                else{const auto increment=b(r[0x1bb1b]+10);if(!(increment&0x80))r[0x1bb1b]=increment;value=r[0x1bb1b];}
                if(r[0x1bb48]){--r[0x1bb48];value|=0x80;}r[0x1c952+unsigned(i)]=value;
            }
        }
    }
    r[0x1bb90]=0;
}
void denseHeights(Ram& r,const TrackSegmentConfiguration& c,unsigned segment){
    putWord(r,0x1bc26,c.mode);r[0x1bdd0+c.maximumDoubled]=segment==r[0x1ca1c]?1:0;
    for(unsigned section=0;section<=c.mode;++section){
        const unsigned index=4*section;const auto raw=r.at(address(c.primaryOffset)+((c.format&0x80)?2*section:section));
        if(raw&0x80)r[0x1bdd0+index]=0;
        const auto height=w(rawHeight(r,c,false,section)+c.primaryBase);putWord(r,0x1be70+index,height);
        if(!(r[0x1bdd0+index]&0x80)&&index>=4){
            auto previous=word(r,0x1be70+index-4);
            if(!(previous&0x8000)||r[0x1bb7f]){
                if(previous&0x8000)previous=w(rawHeight(r,c,false,section-1)+c.primaryBase);
                auto difference=w(height-previous);if(difference&0x8000)difference=w(-int(difference));
                if(sw(difference)>=0x280)r[0x1bdd0+index]|=0x20;
            }
        }
        putWord(r,0x1be72+index,w(rawHeight(r,c,true,section)+c.alternateBase));
    }
    r[0x1bb7a]=(c.format&0x80)?b(c.mode+1):0;
    const auto last=r.at(address(c.primaryOffset)+((c.format&0x80)?2*c.mode:c.mode));if(last&0x80)r[0x1bdd0+c.maximumDoubled]=0x80;
}
void cache(Ram& r,unsigned trackId){
    const auto count=r[0x1ca1a];std::fill_n(r.begin()+0x65e70,count,std::uint8_t(0x78));
    for(unsigned n=0;n<4;++n){const auto segment=r[0x65e30+trackId*8+n*2];if(segment&0x80)break;r.at(0x65e70+segment)=r[0x65e31+trackId*8+n*2];}
    auto write=0x7abdaU;putLong(r,0x66102,write);r[0x1bbc5]=0;
    const auto d=decodeData(r);
    for(unsigned segment=0;segment<count;++segment){
        r[0x1bb85]=b(segment);putLong(r,0x7aa1a+4*segment,write);putWord(r,0x66100,0);
        const auto next=loadTrackSegmentConfiguration(d,b((segment+1)%count));
        if(next.steeringFlags&0x80)r[0x66100]=((next.directionInversion^next.reverseDirection)&0x80)?0x80:0x40;
        const auto c=loadTrackSegmentConfiguration(d,b(segment));configuration(r,c);r[0x1bbf2]=b(-int(c.slopeFlags));denseHeights(r,c,segment);
        auto coordinateBase=b(r.at(address(c.descriptorOffset))+7);if(c.reverseDirection)coordinateBase=b(coordinateBase+(c.limit-1)*4);r[0x1bb91]=coordinateBase;
        for(unsigned index=0;index<c.limitDoubled;index+=4){
            const auto flags=r[0x1bdd0+index];if(flags&0x80)continue;r[0x1bdd0+index]=0x80;if(index<4)continue;
            bool emit=index>=c.maximumDoubled;
            if(!emit&&(r[0x1bdd0+c.maximumDoubled]&0x80)&&index==unsigned(c.maximumDoubled-4))emit=true;
            if(!emit&&(c.steeringFlags&0x80))emit=true;
            if(!emit&&d.primaryGeometry[segment]==0x25&&index==0x18)emit=true;
            if(!emit){auto curve=w((word(r,0x1be70+index)>>1)-(w(word(r,0x1be70+index-4)+word(r,0x1be70+index+4))>>2));if(curve&0x8000)curve=w(-int(curve));emit=sw(curve)>=0x50;}
            if(!emit)continue;
            auto header=std::uint16_t(((flags&0x3f)<<8)|segment);if(!(c.steeringFlags&0x80))header|=word(r,0x66100);putWord(r,write,header);write+=2;
            for(unsigned side=0;side<2;++side){
                const auto vertex=index+side*2;const auto coordinateIndex=c.reverseDirection?b(coordinateBase-int(vertex)*2):b(coordinateBase+int(vertex)*2);
                const auto base=address(c.descriptorOffset);const auto x=std::uint16_t(r.at(base+coordinateIndex)|(unsigned(r.at(base+b(coordinateIndex+1)))<<8));
                const auto z=std::uint16_t(r.at(base+b(coordinateIndex+2))|(unsigned(r.at(base+b(coordinateIndex+3)))<<8));
                std::uint16_t px{},pz{};
                switch(r[0x1bbf2]&0xc0){case 0:px=w(word(r,0x1bb22)+x);pz=w(word(r,0x1bb26)+z);break;case 0x40:px=w(word(r,0x1bb22)-z+0x800);pz=w(word(r,0x1bb26)+x);break;case 0x80:px=w(word(r,0x1bb22)-x+0x800);pz=w(word(r,0x1bb26)-z+0x800);break;default:px=w(word(r,0x1bb22)+z);pz=w(word(r,0x1bb26)-x+0x800);break;}
                putWord(r,0x1bbf6,px);putWord(r,0x1bbf8,pz);putWord(r,write,px);putWord(r,write+2,pz);putWord(r,write+4,word(r,0x1be70+vertex));write+=6;
            }
        }
        putLong(r,0x66102,write);
    }
}
} // namespace
void initializeNativeTrack(NativeMemory& r,unsigned trackId){
    if(trackId>7)throw std::out_of_range("Original track ID must be0..7");r[0x1ca33]=b(trackId);
    // initializeSegmentFlags original0x5d85a marks the lower vertex buffer.
    for(unsigned i=0;i<=0x3e;i+=2){putWord(r,0x1bee8+i,0x8000);r[0x1bdd0+i]=0x80;}
    parse(r,trackId);std::fill_n(r.begin()+0x1c280,256,std::uint8_t(0xff));
    for(unsigned i=0;i<r[0x1ca1a];++i)r[0x1c280+r[0x1c588+i]]=b(i);
    cache(r,trackId);
}
void rebuildNativeTrackGeometryCache(NativeMemory& r){cache(r,r[0x1ca33]);}
} // namespace scr
