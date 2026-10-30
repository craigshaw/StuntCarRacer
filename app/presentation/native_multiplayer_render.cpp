#include "presentation/native_multiplayer_render.h"
#include <algorithm>
#include <stdexcept>

namespace scr::runtime {
namespace {
unsigned word(const NativeMemory& m,unsigned a){return (unsigned(m.at(a))<<8)|m.at(a+1);}
unsigned longword(const NativeMemory& m,unsigned a){return (word(m,a)<<16)|word(m,a+2);}
void glyph(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned v){drawNativeMenuCharacter(m,image,c,v);}
void text(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned table,unsigned offset){drawNativeMenuString(m,image,c,table,offset);}
void spaces(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned n){for(unsigned i=0;i<n;++i)glyph(m,image,c,' ');}
void decimal(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned value,bool compact=false){
    if(value>=10)glyph(m,image,c,'0'+(value/10)%10);else if(!compact)glyph(m,image,c,' ');
    glyph(m,image,c,'0'+value%10);
}
void name(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned id){
    if(id>=12)throw std::invalid_argument("Multiplayer driver outside original range");
    for(unsigned i=0;i<13;++i)glyph(m,image,c,m[0x1ecaa+16*id+i]);
}
void lapTime(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned id){
    if(id>=24)throw std::invalid_argument("Multiplayer time outside original range");
    auto bcd=[&](unsigned a){glyph(m,image,c,'0'+(m[a]>>4));glyph(m,image,c,'0'+(m[a]&15));};
    if(m[0x1c908+id]<9&&(m[0x1c908+id]||m[0x1c920+id])){
        glyph(m,image,c,'0'+m[0x1c908+id]);glyph(m,image,c,':');bcd(0x1c920+id);glyph(m,image,c,'.');bcd(0x1c938+id);
    }else for(unsigned i=0;i<7;++i)glyph(m,image,c,'-');
}
void strip(const NativeMemory& m,NativeMenuImage& image,unsigned row,unsigned type,unsigned repeats=1){
    const unsigned source=0x23c52-0x500+longword(m,0x64c1a+4*type);
    const int top=int(row*8)-9;const unsigned height=(type==3?25:17)+(repeats-1)*8;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<224;++x){
        const unsigned overlay=y>=9?std::min((y-9)/8+1,repeats-1):0,sourceRow=y-overlay*8;
        const unsigned a=source+sourceRow*160+(x/16)*8;unsigned index=0;
        for(unsigned p=0;p<4;++p)if(word(m,a+2*p)&(0x8000u>>(x%16)))index|=1u<<p;
        const int yy=top+int(y);if(yy>=0&&yy<200)image.indices[unsigned(yy)*320+x+32]=std::uint8_t(index);
    }
}
void trackInfo(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned row,unsigned context){
    c.column=std::uint8_t(m[0x6129a+m[0x1ca33]]-6-(context?2:0));c.row=row;c.colour=15;
    text(m,image,c,0x5eb7f,0x93);
    for(unsigned i=0;i<15;++i)glyph(m,image,c,m[0x1edaa+16*m[0x1ca33]+i]);
    if(context){c.column=33;c.row=row;text(m,image,c,0x5eb7f,0x63);}
}
void series(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned row,unsigned displacement){
    c.yOffset=displacement;c.column=14;c.row=row;c.colour=1;text(m,image,c,0x64354,0x10);
    decimal(m,image,c,std::uint8_t(m[0x5eb7b]+1),true);text(m,image,c,0x64354,0xf4);
    decimal(m,image,c,std::uint8_t(m[0x5eb78]*2),true);c.yOffset=displacement;
}
void portraitName(const NativeMemory& m,NativeMenuImage& image,unsigned id,unsigned x,unsigned y){
    unsigned trailing=0;for(unsigned i=12;i>0&&m[0x1ecaa+16*id+i]==' ';--i)++trailing;
    x+=3+(trailing/2)*6;y+=44;
    for(unsigned n=0;n<12;++n){
        const unsigned ch=m[0x1ecab+16*id+n];if(ch<32||ch>=128)continue;
        const unsigned letter=ch-32,source=0x272c2+(letter/32)*0xf00;
        for(unsigned yy=0;yy<8;++yy)for(unsigned xx=0;xx<6;++xx){const unsigned bit=(letter%32)*6+xx;
            if(word(m,source+yy*160+(bit/16)*8)&(0x8000u>>(bit%16)))
                if(x+n*6+xx<320&&y+yy<200)image.indices[(y+yy)*320+x+n*6+xx]=1;
        }
    }
}
void portrait(const NativeMemory& m,NativeMenuImage& image,unsigned id,unsigned row){
    if(id>=12)throw std::invalid_argument("Multiplayer portrait outside original range");
    const unsigned face=m[0x57c3c]||id==m[0x5eb79]?11:m[0x59140+(id&1)];
    const unsigned source=0x35a96+longword(m,0x58aa4+4*m[0x58b20+face]);
    const unsigned offset=longword(m,0x58aa4+4*row),dx=(offset%40)*8,dy=offset/40;
    for(unsigned y=0;y<55;++y)for(unsigned x=0;x<80;++x){
        const unsigned a=source+y*160+(x/16)*8;unsigned index=0;
        for(unsigned p=0;p<4;++p)if(word(m,a+2*p)&(0x8000u>>(x%16)))index|=1u<<p;
        if(face==11&&!(word(m,0x58bd6+y*10+(x/16)*2)&(0x8000u>>(x%16))))index=m[0x58bbc+id];
        if(dx+x<320&&dy+y<200)image.indices[(dy+y)*320+dx+x]=std::uint8_t(index);
    }
    if(face==11)portraitName(m,image,id,dx,dy);
}
void achievements(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c){
    const unsigned flags=m[0x1bbb1];if(!flags)return;
    c.colour=1;unsigned item=3,type=4;
    if(flags&1){item=16;type=1;text(m,image,c,0x5eb7f,0xe3);if((flags&0xc0)==0xc0)text(m,image,c,0x5eb7f,0x6f);}
    c.column=5;c.row=m[0x64af4+item];c.yOffset=4;c.colour=0;strip(m,image,c.row,type,2);
    if(flags&0x80){text(m,image,c,0x5ec92,0x23);text(m,image,c,0x5eb7f,0xd6);spaces(m,image,c,1);lapTime(m,image,c,15);}
    if(flags&0x40){c.column=5;++c.row;text(m,image,c,0x5ec92,0x2f);text(m,image,c,0x5eb7f,0xc9);spaces(m,image,c,1);lapTime(m,image,c,14);}
}
}
NativeMenuImage makeNativeMultiplayerImage(const NativeMemory& m,NativeMultiplayerScreen screen,unsigned division,unsigned context,unsigned entryYOffset){
    if(m[0x5eb76]<1||m[0x5eb76]>7)throw std::invalid_argument("Native multiplayer requires two through eight players");
    if(division==~0u)division=m[0x1c9ce];if(context==~0u)context=m[0x1c9d0];
    auto image=makeNativeMenuFrame(m,division,false);NativeMenuTextCursor c;c.yOffset=entryYOffset;
    c.colour=15;text(m,image,c,0x5eb7f,0xa0);
    if((screen==NativeMultiplayerScreen::Matchup||screen==NativeMultiplayerScreen::Records||screen==NativeMultiplayerScreen::RoundResults)&&(m[0x1bbb1]&1)){
        trackInfo(m,image,c,11,context);achievements(m,image,c);return image;
    }
    if(screen==NativeMultiplayerScreen::Matchup||screen==NativeMultiplayerScreen::Records){
        series(m,image,c,11,0);c.colour=1;text(m,image,c,0x5915e,0);
        portrait(m,image,m[0x1ca27],12);portrait(m,image,m[0x1ca28],13);
        trackInfo(m,image,c,20,context);achievements(m,image,c);return image;
    }
    if(screen==NativeMultiplayerScreen::RoundResults){
        series(m,image,c,10,4);c.column=5;c.row=m[0x64af4+17];c.yOffset=4;c.colour=0;
        strip(m,image,c.row,2);name(m,image,c,m[0x1ca27]);text(m,image,c,0x64354,0x28);name(m,image,c,m[0x1ca28]);
        c.yOffset=0;c.colour=1;text(m,image,c,0x64354,0x60);
        c.yOffset=4;text(m,image,c,0x5915e,5);portrait(m,image,m[0x1ca25],14);portrait(m,image,m[0x1ca26],15);return image;
    }
    const bool race=screen!=NativeMultiplayerScreen::Standings,bonus=screen==NativeMultiplayerScreen::TrackResults;
    unsigned row=16,header=14;
    if(bonus){
        image=makeNativeMenuFrame(m,division,false);c.column=11;c.row=9;c.colour=1;text(m,image,c,0x5ec92,0);
        if(m[0x5eb76]>=5)c.yOffset=4;
        const unsigned trackRow=m[0x5eb64+m[0x5eb76]],recordRow=trackRow+2;
        trackInfo(m,image,c,trackRow,context);c.colour=0;c.column=5;c.row=recordRow;c.yOffset=4;
        strip(m,image,recordRow,0,2);text(m,image,c,0x5ec92,0x23);
        if(m[0x1ca24]){name(m,image,c,m[0x1ca24]);text(m,image,c,0x64354,0xe9);}
        c.column=5;c.row=recordRow+1;text(m,image,c,0x5ec92,0x2f);
        if(m[0x1ca23]){name(m,image,c,m[0x1ca23]);text(m,image,c,0x64354,0xef);}
        if(m[0x5eb76]!=5&&m[0x5eb76]!=6)c.yOffset=0;
        header=m[0x5eb6c+m[0x5eb76]];row=header+2-(m[0x5eb76]==7?1:0);
    }else if(race)trackInfo(m,image,c,11,context);
    else{
        c.colour=1;text(m,image,c,0x64354,0x86);
        if(m[0x57c67]>=3){c.colour=15;c.yOffset=4;text(m,image,c,0x5ec92,0x13);c.yOffset=0;}
    }
    if(race){
        c.colour=1;c.column=6;c.row=header;text(m,image,c,0x5ec92,0x8f);
    }
    strip(m,image,row,2,m[0x5eb76]+1);c.yOffset=4;
    for(unsigned i=0;i<=m[0x5eb76];++i){
        const unsigned id=m[0x1ca0e+m[0x1bc5b]+i];c.column=5;c.row=row+i;c.colour=0;
        name(m,image,c,id);spaces(m,image,c,1);
        if(race){lapTime(m,image,c,id);spaces(m,image,c,2);lapTime(m,image,c,id+12);}
        else{decimal(m,image,c,m[0x1c9f6+id]);spaces(m,image,c,2);decimal(m,image,c,m[0x1c9de+id]);
            spaces(m,image,c,2);decimal(m,image,c,m[0x1c9ea+id]);spaces(m,image,c,3);decimal(m,image,c,m[0x1ca02+id]);}
    }
    return image;
}
void applyNativeMultiplayerScreenState(NativeMemory& m,NativeMultiplayerScreen screen,unsigned entryYOffset){
    unsigned offset=entryYOffset;
    if(screen==NativeMultiplayerScreen::TrackResults||screen==NativeMultiplayerScreen::RaceResults||screen==NativeMultiplayerScreen::Standings)offset=4;
    else if(screen==NativeMultiplayerScreen::RoundResults){
        auto face=[&](unsigned id){return m[0x57c3c]||id==m[0x5eb79]?11:m[0x59140+(id&1)];};
        offset=(m[0x1bbb1]&1)?4:face(m[0x1ca25])==11||face(m[0x1ca26])==11?0:4;
    }else if(screen==NativeMultiplayerScreen::Matchup||screen==NativeMultiplayerScreen::Records)offset=m[0x1bbb1]?4:0;
    m[0x1bb6e]=std::uint8_t(offset);
}
} // namespace scr::runtime
