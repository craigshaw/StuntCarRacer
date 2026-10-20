#include "native_menu_render.h"
#include "planar_image.h"
#include <stdexcept>

namespace scr::viewer {
namespace {
constexpr unsigned menuImage=0x23c52,menuPalette=0x23c32,font=0x1fe82;
std::uint16_t word(const NativeMemory& m,unsigned a) {
    if(a>=m.size()-1)throw std::invalid_argument("Menu word outside native memory");
    return static_cast<std::uint16_t>((unsigned(m[a])<<8)|m[a+1]);
}
std::uint32_t longword(const NativeMemory& m,unsigned a) {
    return (std::uint32_t(word(m,a))<<16)|word(m,a+2);
}
void rectangle(NativeMenuImage& image,unsigned x,unsigned y,unsigned w,unsigned h,std::uint8_t colour) {
    if(x+w>320||y+h>200)throw std::invalid_argument("Menu rectangle outside screen");
    for(unsigned row=y;row<y+h;++row)for(unsigned column=x;column<x+w;++column)
        image.indices[row*320+column]=colour;
}
void character(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& cursor,unsigned c) {
    if(c<32||c>=127)return;
    const unsigned x=cursor.column*7+cursor.horizontalOffset;
    const int y=int((cursor.row*8+cursor.yOffset)&255)-8;
    // Cursor advance is seven pixels, but original glyph bytes retain all
    // eight bits: '+' deliberately extends into the next advance column.
    for(unsigned row=0;row<8;++row)for(unsigned bit=0;bit<8;++bit) {
        const int yy=y+int(row);
        if(x+bit<320&&yy>=0&&yy<200&&(m[font+(c-32)*8+row]&(0x80u>>bit)))
            image.indices[unsigned(yy)*320+x+bit]=static_cast<std::uint8_t>(cursor.colour);
    }
    cursor.column=(cursor.column+1)%45;
}
void strip(const NativeMemory& m,NativeMenuImage& image,unsigned row,unsigned type) {
    // Original 0x64b4a..0x64bf6 copies fourteen interleaved words per plane,
    // with a forty-eight-byte source gap and twelve-byte destination gap.
    const unsigned source=menuImage-0x500+longword(m,0x64c1a+type*4);
    const int top=int(row*8)-9;
    const unsigned height=type==3?25:17;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<224;++x) {
        const unsigned a=source+y*160+(x/16)*8;
        unsigned index=0;
        for(unsigned p=0;p<4;++p)if(word(m,a+p*2)&(0x8000u>>(x%16)))index|=1u<<p;
        if(top+int(y)>=0&&top+int(y)<200)image.indices[unsigned(top+int(y))*320+x+32]=static_cast<std::uint8_t>(index);
    }
}
}
NativeMenuImage makeNativeMenuBackground(const NativeMemory& m) {
    NativeMenuImage image;
    for(unsigned i=0;i<16;++i)image.palette[i]=originalPaletteRgb(word(m,menuPalette+2*i));
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x) {
        const unsigned a=menuImage+y*160+(x/16)*8;unsigned colour=0;
        for(unsigned p=0;p<4;++p)if(word(m,a+p*2)&(0x8000u>>(x%16)))colour|=1u<<p;
        image.indices[y*320+x]=static_cast<std::uint8_t>(colour);
    }
    return image;
}
void drawNativeMenuString(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& cursor,
        unsigned table,unsigned offset) {
    unsigned controls=0;
    for(unsigned i=0;i<512;++i) {
        const unsigned a=table+((offset+i)&255);
        if(a>=m.size())throw std::invalid_argument("Menu string outside native memory");
        const auto c=m[a];
        if(c==255)return;
        if(controls==1){cursor.column=c;controls=2;}
        else if(controls==2){cursor.row=c;controls=0;}
        else if(c==31)controls=1;
        else character(m,image,cursor,c);
    }
    throw std::invalid_argument("Unterminated menu string");
}
void drawNativeMenuCharacter(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& cursor,unsigned c) {
    character(m,image,cursor,c);
}
void packNativeMenuPixels(const NativeMenuImage& image,std::vector<std::uint32_t>& pixels) {
    pixels.resize(image.indices.size());
    for(unsigned i=0;i<image.indices.size();++i)pixels[i]=image.palette.at(image.indices[i]);
}
NativeMenuImage makeNativeMenuFrame(const NativeMemory& m,unsigned division,bool header) {
    if(division>3)throw std::invalid_argument("Native menu division outside original range");
    auto image=makeNativeMenuBackground(m);
    // Exact filled area of original drawScreenFrame 0x64c6e..0x64d08.
    rectangle(image,64,65,192,2,3);rectangle(image,32,67,224,125,3);
    NativeMenuTextCursor cursor{};
    if(header) {
        cursor.colour=15;drawNativeMenuString(m,image,cursor,0x64354,0);
        character(m,image,cursor,'0'+4-division);
    }
    return image;
}
NativeMenuImage makeNativeMenuImage(const NativeMemory& m,const NativeMenuRenderState& state) {
    if(state.count>7||state.selected>state.count||state.division>3||state.selectedDivision>3||state.menuOffset+state.count>=256)
        throw std::invalid_argument("Native menu state outside original row range");
    auto image=makeNativeMenuFrame(m,state.division,!state.menuStrings&&state.menuOffset!=0x1c);
    NativeMenuTextCursor cursor{};
    cursor.colour=1;drawNativeMenuString(m,image,cursor,state.menuStrings?0x5eb7f:0x5a69a,0);
    for(unsigned option=0;option<=state.count;++option) {
        const unsigned row=m[0x64af4+option];
        strip(m,image,row,option==state.selected?(state.confirmed?0:1):2);
        cursor.column=5;cursor.row=row;cursor.yOffset=4;cursor.colour=0;
        character(m,image,cursor,'1'+option);character(m,image,cursor,'.');character(m,image,cursor,' ');
        const unsigned offset=m[0x5bcd0+state.menuOffset+option];
        drawNativeMenuString(m,image,cursor,state.menuStrings?0x5eb7f:0x5a69a,offset);
        if(state.menuOffset==0x18)character(m,image,cursor,'1'+option);
        if(state.menuOffset==0x1c&&option<state.count) {
            drawNativeMenuString(m,image,cursor,0x64354,0x23);
            const unsigned track=m[0x648c2+state.selectedDivision*2+option];
            for(unsigned i=0;i<15;++i)character(m,image,cursor,m[0x1edaa+track*16+i]);
        }
    }
    return image;
}
void renderNativeMenu(const NativeMemory& m,const NativeMenuRenderState& state,std::vector<std::uint32_t>& pixels) {
    packNativeMenuPixels(makeNativeMenuImage(m,state),pixels);
}
void drawNativeGameMessage(const NativeMemory& m,std::vector<std::uint32_t>& pixels,
        const std::array<std::uint32_t,16>& palette,unsigned index,unsigned mode,bool blink) {
    if(pixels.size()!=320*200||mode==0||mode>4||index+mode*4>0xe4)
        throw std::invalid_argument("Native game message outside original range");
    NativeMenuImage mask;mask.indices.fill(16);
    NativeMenuTextCursor cursor;
    cursor.column=19;cursor.row=4;cursor.horizontalOffset=3;
    // The shared glyph helper includes display mode 0x41's -8 offset.
    // In-race mode has no displacement, hence add eight here.
    cursor.yOffset=mode==2?13:8;cursor.colour=blink?15:0;
    for(unsigned group=0;group<mode;++group) {
        if(m[0x5e65c+index]==0x21) {
            cursor.column=19;cursor.row=5;cursor.horizontalOffset=3;cursor.yOffset=10;
        }
        ++index;
        for(unsigned i=0;i<3;++i,++index) {
            const auto c=m[0x5e65c+index];
            if(c==0x3c)cursor.horizontalOffset+=4;
            else {character(m,mask,cursor,c);++cursor.horizontalOffset;}
        }
    }
    for(unsigned i=0;i<mask.indices.size();++i)if(mask.indices[i]<16)pixels[i]=palette[mask.indices[i]];
}
} // namespace scr::viewer
