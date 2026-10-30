#include "presentation/native_competition_render.h"
#include "render/planar_image.h"
#include <algorithm>
#include <stdexcept>

namespace scr::runtime {
namespace {
unsigned word(const NativeMemory& m,unsigned a){return (unsigned(m.at(a))<<8)|m.at(a+1);}
unsigned longword(const NativeMemory& m,unsigned a){return (word(m,a)<<16)|word(m,a+2);}
void glyph(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned v){drawNativeMenuCharacter(m,image,c,v);}
void text(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned table,unsigned offset){drawNativeMenuString(m,image,c,table,offset);}
void decimal(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned v,bool padded=false){
    if(v>=10)glyph(m,image,c,'0'+(v/10)%10);else if(padded)glyph(m,image,c,' ');
    glyph(m,image,c,'0'+v%10);
}
void name(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned id){
    for(unsigned i=0;i<13;++i)glyph(m,image,c,m[0x1ecaa+16*id+i]);
}
void strip(const NativeMemory& m,NativeMenuImage& image,unsigned row,unsigned type,bool expanded){
    const unsigned source=0x23c52-0x500+longword(m,0x64c1a+type*4);
    const int top=int(row*8)-9;const unsigned height=type==3||expanded?25:17;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<224;++x){
        const unsigned sourceRow=expanded&&y>=9?y-8:y;
        const unsigned a=source+sourceRow*160+(x/16)*8;unsigned index=0;
        for(unsigned p=0;p<4;++p)if(word(m,a+p*2)&(0x8000u>>(x%16)))index|=1u<<p;
        if(top+int(y)>=0&&top+int(y)<200)image.indices[unsigned(top+int(y))*320+x+32]=std::uint8_t(index);
    }
}
void menuLine(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned item,unsigned type=2,bool expanded=false){
    c.column=5;c.row=m[0x64af4+item];c.yOffset=4;c.colour=0;strip(m,image,c.row,type,expanded);
}
void portraitName(const NativeMemory& m,NativeMenuImage& image,unsigned id,unsigned x,unsigned y){
    unsigned spaces=0;
    for(unsigned i=12;i>0&&m[0x1ecaa+16*id+i]==' ';--i)++spaces;
    x+=3+(spaces/2)*6;y+=44;
    for(unsigned n=0;n<12;++n){
        const auto ch=m[0x1ecaa+16*id+1+n];
        if(ch>=32&&ch<128){
            const unsigned letter=ch-32,source=0x272c2+(letter/32)*0xf00;
            for(unsigned yy=0;yy<8;++yy)for(unsigned xx=0;xx<6;++xx){
                const unsigned bit=(letter%32)*6+xx;
                if(word(m,source+yy*160+(bit/16)*8)&(0x8000u>>(bit%16)))
                    if(x+n*6+xx<320&&y+yy<200)image.indices[(y+yy)*320+x+n*6+xx]=1;
            }
        }
    }
}
void portrait(const NativeMemory& m,NativeMenuImage& image,unsigned id,unsigned row,unsigned shift=0){
    if(id>=12||row>=31)throw std::invalid_argument("Original driver portrait outside range");
    const unsigned source=0x35a96+longword(m,0x58aa4+4*m[0x58b20+id]);
    const unsigned offset=longword(m,0x58aa4+4*row),dx=(offset%40)*8+shift,dy=offset/40;
    for(unsigned y=0;y<55;++y)for(unsigned x=0;x<80;++x){
        if(shift&&(x<2||x>=78))continue;
        const unsigned a=source+y*160+(x/16)*8;unsigned index=0;
        for(unsigned p=0;p<4;++p)if(word(m,a+p*2)&(0x8000u>>(x%16)))index|=1u<<p;
        if(dx+x<320&&dy+y<200)image.indices[(dy+y)*320+dx+x]=std::uint8_t(index);
    }
    if(id==11)portraitName(m,image,id,dx,dy);
}
void series(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned row,unsigned displacement){
    // Original0x58e30 changes the position operand in its string table. An
    // equivalent temporary cursor displacement keeps the asset image const.
    c.yOffset=std::uint8_t(displacement+8*(row-13));c.colour=1;text(m,image,c,0x64354,0x0d);
    decimal(m,image,c,unsigned(m[0x5eb7b])+1);text(m,image,c,0x64354,0xf4);decimal(m,image,c,m[0x5eb78]);
    c.yOffset=displacement;
}
void trackInfo(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned row=20){
    c.column=std::uint8_t(m[0x6129a+m[0x1ca33]]-6);c.row=row;c.yOffset=0;c.colour=15;
    text(m,image,c,0x5eb7f,0x93);
    for(unsigned i=0;i<15;++i)glyph(m,image,c,m[0x1edaa+16*m[0x1ca33]+i]);
}
void lapTime(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned id){
    auto bcd=[&](unsigned a){glyph(m,image,c,'0'+((m[a]>>4)&15));glyph(m,image,c,'0'+(m[a]&15));};
    if(m[0x1c908+id]<9&&(m[0x1c908+id]||m[0x1c920+id])){
        glyph(m,image,c,'0'+m[0x1c908+id]);glyph(m,image,c,':');bcd(0x1c920+id);glyph(m,image,c,'.');bcd(0x1c938+id);
    }else for(unsigned i=0;i<7;++i)glyph(m,image,c,'-');
}
void achievements(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c){
    const unsigned flags=m[0x1bbb1];if(!flags)return;
    c.colour=1;unsigned item=3,type=4;
    if(flags&1){item=16;type=1;text(m,image,c,0x5eb7f,0xe3);if((flags&0xc0)==0xc0)text(m,image,c,0x5eb7f,0x6f);}
    menuLine(m,image,c,item,type,true);
    if(flags&0x80){text(m,image,c,0x5ec92,0x23);text(m,image,c,0x5eb7f,0xd6);
        glyph(m,image,c,' ');lapTime(m,image,c,15);}
    if(flags&0x40){c.column=5;++c.row;text(m,image,c,0x5ec92,0x2f);text(m,image,c,0x5eb7f,0xc9);
        glyph(m,image,c,' ');lapTime(m,image,c,14);}
}
NativeMenuImage result(const NativeMemory& m,unsigned index){
    NativeMenuImage image;const unsigned asset=longword(m,0x56ff8+4*index);
    for(unsigned p=0;p<16;++p)image.palette[p]=originalPaletteRgb(std::uint16_t(word(m,asset+2+2*p)));
    if(m[asset]&0x80){
        const auto decoded=decodePlanarRle(m.data()+asset+0x22,m.size()-asset-0x22,320,200,4);
        const auto indexed=decodePlanarImage(decoded.planes.data(),decoded.planes.size(),320,200,4);
        std::copy(indexed.pixels.begin(),indexed.pixels.end(),image.indices.begin());
    }else for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x){
        const unsigned a=asset+0x22+y*160+(x/16)*8;unsigned colour=0;
        for(unsigned p=0;p<4;++p)if(word(m,a+2*p)&(0x8000u>>(x%16)))colour|=1u<<p;
        image.indices[y*320+x]=std::uint8_t(colour);
    }
    return image;
}
}
NativeMenuImage makeNativeCompetitionImage(const NativeMemory& m,NativeMenuPage page,unsigned division,unsigned context){
    if(division>3)throw std::invalid_argument("Invalid native competition division");
    if(page==NativeMenuPage::Results)return result(m,(m[0x1bbb4]&0x80)?(m[0x1bca2]?3:2):1);
    if(page==NativeMenuPage::SeasonAward)return result(m,4);
    if(page==NativeMenuPage::Roster){
        NativeMenuImage image;
        for(unsigned p=0;p<16;++p)image.palette[p]=originalPaletteRgb(std::uint16_t(word(m,0x35a76+2*p)));
        for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x){
            const unsigned a=0x35a96+y*160+(x/16)*8;unsigned colour=0;
            for(unsigned p=0;p<4;++p)if(word(m,a+2*p)&(0x8000u>>(x%16)))colour|=1u<<p;
            image.indices[y*320+x]=std::uint8_t(colour);
        }
        for(unsigned i=0;i<12;++i)portrait(m,image,m[0x1c9c2+i],19+i);
        return image;
    }
    if(context==~0u)context=m[0x1c9d0];
    auto image=makeNativeMenuFrame(m,division,context==0);NativeMenuTextCursor c;
    if(context){c.colour=15;text(m,image,c,0x5a69a,0xbb);glyph(m,image,c,'0'+4-division);}
    if(page==NativeMenuPage::Matchup||page==NativeMenuPage::Records){
        if(m[0x1bbb1]&1){trackInfo(m,image,c,11);achievements(m,image,c);return image;}
        series(m,image,c,11,0);c.colour=1;text(m,image,c,0x5915e,0);
        portrait(m,image,m[0x1ca27],12);portrait(m,image,m[0x1ca28],13);
        trackInfo(m,image,c);achievements(m,image,c);return image;
    }
    if(page==NativeMenuPage::RoundResults){
        series(m,image,c,10,4);menuLine(m,image,c,17);name(m,image,c,m[0x1ca27]);text(m,image,c,0x64354,0x28);name(m,image,c,m[0x1ca28]);
        c.yOffset=0;c.colour=1;text(m,image,c,0x64354,0x60);
        c.yOffset=4;c.colour=1;text(m,image,c,0x5915e,5);portrait(m,image,m[0x1ca25],14);portrait(m,image,m[0x1ca26],15);return image;
    }
    if(page==NativeMenuPage::Standings){
        const unsigned base=m[0x5ab42+division];
        portrait(m,image,m[0x1ca0e+base],16,14);portrait(m,image,m[0x1ca0e+base+1],17,9);portrait(m,image,m[0x1ca0e+base+2],18,4);
        c.yOffset=4;c.colour=1;c.horizontalOffset=4;text(m,image,c,0x5915e,0x49);c.horizontalOffset=0;
        for(unsigned i=0;i<3;++i){
            const unsigned id=m[0x1ca0e+base+i];c.column=4+11*i;c.row=19;c.yOffset=4;c.colour=15;text(m,image,c,0x5915e,0x25);
            c.colour=1;decimal(m,image,c,m[0x1c9f6+id],true);
            c.column=4+11*i;c.row=21;c.yOffset=0;c.colour=15;text(m,image,c,0x5915e,0x2e);c.colour=1;decimal(m,image,c,m[0x1c9de+id],true);
            c.column=4+11*i;c.row=22;c.yOffset=4;c.colour=15;text(m,image,c,0x5915e,0x37);c.colour=1;decimal(m,image,c,m[0x1c9ea+id],true);
            c.column=4+11*i;c.row=24;c.yOffset=0;c.colour=15;text(m,image,c,0x5915e,0x40);c.colour=1;decimal(m,image,c,m[0x1ca02+id],true);
        }
        return image;
    }
    if(page==NativeMenuPage::SeasonEnd){
        const unsigned base=m[0x5ab42+division];c.column=15;c.row=12;c.colour=1;text(m,image,c,0x64354,0xd7);
        if(!base&&m[0x1ca0e]!=m[0x5eb79]){
            menuLine(m,image,c,1);text(m,image,c,0x64354,0xc7);name(m,image,c,m[0x1ca0e+2]);
        }else if(!base&&context){menuLine(m,image,c,1);text(m,image,c,0x5a69a,0xce);}
        else {menuLine(m,image,c,1);text(m,image,c,0x64354,0xb7);name(m,image,c,m[0x1ca0e+base]);
            if(!base){menuLine(m,image,c,2);text(m,image,c,0x5a69a,0xa7);}
            else if(base+2!=11){menuLine(m,image,c,2);text(m,image,c,0x64354,0xc7);name(m,image,c,m[0x1ca0e+base+2]);}}
        return image;
    }
    throw std::invalid_argument("Page is not a native competition screen");
}
} // namespace scr::runtime
