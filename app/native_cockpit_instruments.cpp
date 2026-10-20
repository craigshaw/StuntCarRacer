#include "native_cockpit_instruments.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace scr::viewer {
namespace {
unsigned word(const NativeMemory& m,unsigned a){return (unsigned(m.at(a))<<8)|m.at(a+1);}
unsigned longword(const NativeMemory& m,unsigned a){return (word(m,a)<<16)|word(m,a+2);}
void word(NativeMemory& m,unsigned a,unsigned v){m.at(a)=std::uint8_t(v>>8);m.at(a+1)=std::uint8_t(v);}
void longword(NativeMemory& m,unsigned a,unsigned v){word(m,a,v>>16);word(m,a+2,v);}
bool negativeByte(unsigned v){return (v&128)!=0;}
void increment(NativeMemory& m,unsigned a,int amount){m.at(a)=std::uint8_t(m.at(a)+amount);}
void position(NativeMemory& m,unsigned x,unsigned y){
    drawNativeCockpitCharacter(m,31);drawNativeCockpitCharacter(m,x);drawNativeCockpitCharacter(m,y);
}
void digit(NativeMemory& m,unsigned v,bool advance=false){
    if(advance)increment(m,0x1bb6d,1);
    drawNativeCockpitCharacter(m,std::uint8_t(v+48));
}
unsigned colour(const NativeMemory& m,unsigned masks){
    unsigned result=0;
    if(word(m,masks))result|=1;if(word(m,masks+2))result|=2;
    if(word(m,masks+4))result|=4;if(word(m,masks+6))result|=8;
    return result;
}
void setPixel(NativeMemory& m,unsigned base,unsigned x,unsigned y,unsigned index){
    const unsigned offset=y*40+x/8,bit=128u>>(x&7);
    for(unsigned p=0;p<4;++p){auto& b=m.at(base+8000*p+offset);b=std::uint8_t((b&~bit)|((index&(1u<<p))?bit:0));}
}
void graphic(NativeMemory& m,unsigned id){
    unsigned source=longword(m,0x6a4ac+4*id);
    const unsigned parameters=0x6a170+16*id;
    const unsigned width=word(m,parameters)+1,height=word(m,parameters+2)+1;
    const unsigned x=(word(m,parameters+4)&255)*2,y=word(m,parameters+6)&255;
    const unsigned frame=longword(m,0x6a58c);
    for(unsigned row=0;row<height;++row)for(unsigned column=0;column<width;++column){
        const unsigned mask=word(m,source);source+=2;
        for(unsigned p=0;p<4;++p){const unsigned a=frame+8000*p+(y+row)*40+x+column*2;
            word(m,a,(word(m,a)&mask)|word(m,source));source+=2;}
    }
}
void carIcon(NativeMemory& m,unsigned id){
    const unsigned saved=longword(m,0x6a58c),first=longword(m,0x6a584);
    longword(m,0x6a58c,first);graphic(m,id);
    longword(m,0x6a58c,first+32000);graphic(m,id);
    longword(m,0x6a58c,saved);
}
bool tieNegative(const NativeMemory& m){
    unsigned d=std::uint8_t(m[0x1bb21]-m[0x1bb20]);if(d)return negativeByte(d);
    unsigned player=std::uint8_t(m[0x1bb1c]-m[0x1ca32]);if(m[0x1bb1c]<m[0x1ca32])player=std::uint8_t(player+m[0x1ca1a]);
    unsigned opponent=std::uint8_t(m[0x1bb1d]-m[0x1ca32]);if(m[0x1bb1d]<m[0x1ca32])opponent=std::uint8_t(opponent+m[0x1ca1a]);
    d=std::uint8_t(opponent-player);if(d)return negativeByte(d);
    d=word(m,0x1bc3e);if(d)return (d&32768)!=0;
    return negativeByte(m[0x57c3c]);
}
}
void drawNativeCockpitCharacter(NativeMemory& m,unsigned c){
    c&=255;
    if(m[0x59843]){
        increment(m,0x59844,1);
        if(m[0x59844]==2){m[0x5983f]=std::uint8_t(c);m[0x59843]=0;}
        else m[0x5983e]=std::uint8_t(c);
        return;
    }
    if(c==31){m[0x59843]=31;m[0x59844]=0;return;}
    if(c>=127){
        if(c==127){increment(m,0x5983e,-1);m[0x59842]=m[0x1bbd3];m[0x1bbd3]=0;
            drawNativeCockpitCharacter(m,32);m[0x1bbd3]=m[0x59842];increment(m,0x5983e,-1);}
        return;
    }
    if(m[0x5924a])throw std::invalid_argument("Cockpit primary font routine received alternate font state");
    const unsigned x=m[0x5983e]*7+unsigned(m[0x1bb6d]);
    m[0x59840]=std::uint8_t(x>>4);m[0x59841]=std::uint8_t(x&15);
    unsigned y=std::uint8_t(m[0x5983f]*8+m[0x1bb6e]);
    const unsigned first=longword(m,0x6a584);
    const bool single=negativeByte(m[0x1bbbc]);
    unsigned base=single?longword(m,0x6a58c):first+32000;
    if(m[0x5d724]==0x41)base-=320;
    const unsigned ink=colour(m,0x59836),paper=colour(m,0x5982e);
    const unsigned font=0x1fe82+std::uint8_t(c-32)*8;
    for(unsigned row=0;row<8;++row){
        if(!negativeByte(m[0x1bbd3]))for(unsigned bit=0;bit<7;++bit)setPixel(m,base,x+bit,y+row,paper);
        for(unsigned bit=0;bit<8;++bit)if(m.at(font+row)&(128u>>bit))setPixel(m,base,x+bit,y+row,ink);
        if(!single){
            const unsigned offset=(y+row)*40+(x/16)*2;
            for(unsigned p=0;p<4;++p)for(unsigned b=0;b<4;++b)m.at(base-32000+p*8000+offset+b)=m.at(base+p*8000+offset+b);
        }
    }
    m[0x5983e]=std::uint8_t(m[0x5983e]+1);if(m[0x5983e]>=45)m[0x5983e]=0;
}
void drawNativeCockpitLabels(NativeMemory& m){
    m[0x1bb6e]=m[0x1bb6d]=2;position(m,5,22);drawNativeCockpitCharacter(m,'L');
    increment(m,0x1bb6d,2);position(m,8,22);drawNativeCockpitCharacter(m,'B');
    m[0x1bb6d]=m[0x1bb6e]=0;
}
void drawNativeCockpitLapCounter(NativeMemory& m){
    position(m,6,22);m[0x1bb6d]=m[0x1bb6e]=2;digit(m,m[0x1bb20],true);m[0x1bb6d]=m[0x1bb6e]=0;
}
void drawNativeCockpitBoostDigits(NativeMemory& m){
    position(m,9,22);m[0x1bb6d]=4;m[0x1bb6e]=2;
    digit(m,m[0x1ca20]>>4,true);digit(m,m[0x1ca20]&15,true);m[0x1bb6d]=m[0x1bb6e]=0;
}
void drawNativeCockpitLapTime(NativeMemory& m,unsigned visibility,unsigned pos,unsigned index){
    if(pos>3||index>23)throw std::invalid_argument("Cockpit timer outside original table");
    m[0x1bbe8]=std::uint8_t(index);m[0x1bb89]=std::uint8_t(visibility);m[0x1bb6e]=m[0x5e470+pos];m[0x1bbd3]=128;
    position(m,34,m[0x5e46c+pos]);m[0x1bb6d]=5;drawNativeCockpitCharacter(m,':');
    increment(m,0x5983e,2);increment(m,0x1bb6e,-2);m[0x1bb6d]=2;drawNativeCockpitCharacter(m,'.');
    m[0x1bbd3]=0;increment(m,0x1bb6e,2);increment(m,0x5983e,-5);m[0x1bb6d]=6;
    digit(m,m[0x1c908+index]&15);increment(m,0x5983e,1);m[0x1bb6d]=3;
    digit(m,m[0x1c920+index]>>4);digit(m,m[0x1c920+index]&15);increment(m,0x1bb6d,4);
    digit(m,negativeByte(visibility)?m[0x1c938+index]>>4:0xf0);
    digit(m,negativeByte(visibility)?m[0x1c938+index]&15:0xf0);
    m[0x1bb6d]=m[0x1bb6e]=0;
}
void drawNativeCockpitLapTime(NativeMemory& m,PackedLapTime time,unsigned visibility,unsigned pos,unsigned index){
    if(index>23)throw std::invalid_argument("Cockpit timer outside original table");
    const PackedLapTime saved{m[0x1c908+index],m[0x1c920+index],m[0x1c938+index]};
    m[0x1c908+index]=time.minutes;m[0x1c920+index]=time.seconds;m[0x1c938+index]=time.subseconds;
    drawNativeCockpitLapTime(m,visibility,pos,index);
    m[0x1c908+index]=saved.minutes;m[0x1c920+index]=saved.seconds;m[0x1c938+index]=saved.subseconds;
}
void drawNativeCockpitLeaderIcon(NativeMemory& m,unsigned id){m[0x1bbde]=std::uint8_t(id);carIcon(m,id==7?15:16);}
void drawNativeCockpitLeaderIcon(NativeMemory& m){drawNativeCockpitLeaderIcon(m,tieNegative(m)?7:11);}
void drawNativeCockpitResultIcon(NativeMemory& m,unsigned id){m[0x1bb8c]=std::uint8_t(id);carIcon(m,id==7?17:18);}
void drawNativeCockpitResultIcon(NativeMemory& m){drawNativeCockpitResultIcon(m,m[0x1bbb2]==0?7:11);}
void drawNativeCockpitDistance(NativeMemory& m){
    if(m[0x1bbac]&3)return;
    unsigned v=word(m,0x1bc38);v=std::uint16_t(v+(v>>2))>>2;
    unsigned thousands=0,hundreds=0,tens=0;
    while(std::int16_t(v)>=1000){v=std::uint16_t(v-1000);thousands=std::uint8_t(thousands+1);}
    while(std::int16_t(v)>=100){v=std::uint16_t(v-100);hundreds=std::uint8_t(hundreds+1);}
    while(std::int8_t(v)>=10){v=(v&0xff00)|std::uint8_t(v-10);tens=std::uint8_t(tens+1);}
    m[0x1bb18]=std::uint8_t(v);m[0x1bb1a]=std::uint8_t(tens);m[0x1bb19]=std::uint8_t(hundreds);m[0x1bb17]=std::uint8_t(thousands);
    m[0x1bb6d]=1;m[0x1bb6e]=4;position(m,6,23);
    digit(m,negativeByte(m[0x1bbb8])?0xfd:0xf0,true);digit(m,thousands,true);digit(m,hundreds,true);digit(m,tens,true);digit(m,v&255,true);
    m[0x1bb6d]=m[0x1bb6e]=0;if(!m[0x1bbaf])drawNativeCockpitLeaderIcon(m);
}
void updateNativeCockpitSpeedometer(NativeMemory& m){
    unsigned speed=std::uint16_t(word(m,0x1bd30)-0x1100);if(speed&32768)speed=0;
    unsigned length=((speed*0xb700u)>>16)>>7;if(length>=128)length-=128;
    word(m,0x1bc18,length);const unsigned previous=word(m,0x1bc20);
    if(length!=previous){
        const unsigned first=longword(m,0x6a584),base=first+0x1b3c;
        const unsigned begin=std::min(length,previous)+1,end=std::max(length,previous)+1,index=length>previous?3:0;
        for(unsigned x=begin;x<end;++x)setPixel(m,base,x,0,index);
        for(unsigned p=0;p<4;++p)for(unsigned b=0;b<16;++b){const auto value=m.at(base+p*8000+b);
            m.at(base+p*8000+40+b)=value;m.at(base+p*8000+32000+b)=value;m.at(base+p*8000+32040+b)=value;}
    }
    word(m,0x1bc20,length);
}
void syncNativeCockpitInstrumentInputs(NativeMemory& dst,const NativeMemory& src){
    for(unsigned a:{0x1bb20,0x1bb21,0x1bb1c,0x1bb1d,0x1ca32,0x1ca1a,0x57c3c,
        0x1bbb2,0x1bbb8,0x1bbac,0x1bbaf,0x1ca20,0x5d724})dst[a]=src[a];
    for(unsigned a:{0x1bc38,0x1bc3e,0x1bd30}){dst[a]=src[a];dst[a+1]=src[a+1];}
    for(unsigned a:{0x1c908,0x1c920,0x1c938})std::copy_n(src.begin()+a,24,dst.begin()+a);
}
void initializeNativeCockpitInstruments(NativeMemory& m){
    // Original setForegroundColor(14) supplies the opaque cell's paper mask;
    // setBackgroundColor(0) supplies glyph ink. Their names describe the
    // original paired-plane operations, rather than modern text terminology.
    for(unsigned p=0;p<4;++p){word(m,0x5982e+2*p,p?65535:0);word(m,0x59836+2*p,0);}
    m[0x5924a]=m[0x1bbd3]=m[0x1bbbc]=m[0x59843]=m[0x59844]=0;
    drawNativeCockpitLabels(m);m[0x1bbde]=11;carIcon(m,16);m[0x1bb8c]=11;carIcon(m,18);
}
} // namespace scr::viewer
