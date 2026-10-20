#include "native_opening_render.h"
#include <algorithm>
#include <stdexcept>

namespace scr::viewer {
namespace {
unsigned word(const NativeMemory& m,unsigned a){return (unsigned(m.at(a))<<8)|m.at(a+1);}
unsigned longword(const NativeMemory& m,unsigned a){return (word(m,a)<<16)|word(m,a+2);}
void strip(const NativeMemory& m,NativeMenuImage& image,unsigned row,unsigned type,unsigned repeats=1){
    const unsigned source=0x23c52-0x500+longword(m,0x64c1a+4*type);
    const int top=int(row*8)-9;
    const unsigned baseHeight=type==3?25:17;
    const unsigned height=baseHeight+(repeats-1)*8;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<224;++x){
        const unsigned overlay=(repeats>1&&y>=9)?std::min((y-9)/8+1,repeats-1):0;
        const unsigned sourceRow=y-overlay*8;
        const unsigned a=source+sourceRow*160+(x/16)*8;unsigned index=0;
        for(unsigned p=0;p<4;++p)if(word(m,a+2*p)&(0x8000u>>(x%16)))index|=1u<<p;
        const int yy=top+int(y);
        if(yy>=0&&yy<200)image.indices[unsigned(yy)*320+x+32]=std::uint8_t(index);
    }
}
void glyph(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned v){drawNativeMenuCharacter(m,image,c,v);}
void text(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned table,unsigned offset){drawNativeMenuString(m,image,c,table,offset);}
void decimal(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned v){
    glyph(m,image,c,v>=10?'0'+(v/10)%10:' ');glyph(m,image,c,'0'+v%10);
}
void name(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned id){
    for(unsigned i=0;i<13;++i)glyph(m,image,c,m.at(0x1ecaa+16*id+i));
}
}
NativeOpeningNameScreen::NativeOpeningNameScreen(const NativeMemory& m)
    :memory_(m),image_(makeNativeMenuFrame(m,m[0x1c9ce],false)){
    cursor_.colour=1;text(m,image_,cursor_,0x64354,0xe0);
    cursor_.column=5;cursor_.row=m[0x64af4+1];cursor_.yOffset=4;cursor_.colour=0;
    strip(m,image_,cursor_.row,2);
    // drawHorizontalLine's right endpoint is exclusive (0x66454..0x66468).
    for(unsigned x=106;x<190;++x)image_.indices[133*320+x]=10;
}
void NativeOpeningNameScreen::beginAttempt(){
    paper_=2;cursor_.column=14;cursor_.row=16;cursor_.yOffset=4;cursor_.colour=0;
    drawCharacter('>');
}
void NativeOpeningNameScreen::drawCharacter(unsigned ch){
    if(ch==127){
        if(cursor_.column==0)throw std::invalid_argument("Original name cursor underflow");
        --cursor_.column;
        const unsigned x=cursor_.column*7+cursor_.horizontalOffset;
        const int y=int((cursor_.row*8+cursor_.yOffset)&255)-8;
        for(unsigned row=0;row<8;++row)for(unsigned bit=0;bit<7;++bit)
            if(x+bit<320&&y+int(row)>=0&&y+int(row)<200)
                image_.indices[unsigned(y+int(row))*320+x+bit]=std::uint8_t(paper_);
        return;
    }
    if(ch<32||ch>=127)throw std::invalid_argument("Invalid original name glyph event");
    glyph(memory_,image_,cursor_,ch);
}
void NativeOpeningNameScreen::complete(){cursor_.yOffset=0;paper_=3;}

NativeMenuImage makeNativeOpeningStandingsImage(const NativeMemory& m){
    // The table uses the same frame/font as the menus. Its variable-length
    // cursor object and row spacing are recovered below, independently of the
    // single-player three-portrait standings page.
    if(m[0x5eb76]<1||m[0x5eb76]>7)throw std::invalid_argument("Original local player count outside range");
    auto image=makeNativeMenuFrame(m,m[0x1c9ce],false);
    NativeMenuTextCursor c;c.colour=15;text(m,image,c,0x5eb7f,0xa0);
    c.colour=1;text(m,image,c,0x64354,0x86);
    c.column=5;c.row=16;c.yOffset=4;c.colour=0;
    strip(m,image,c.row,2,m[0x5eb76]+1);
    for(unsigned i=0;i<=m[0x5eb76];++i){
        const unsigned id=m[0x1ca0e+m[0x1bc5b]+i];
        c.column=5;c.row=16+i;c.yOffset=4;c.colour=0;
        name(m,image,c,id);
        glyph(m,image,c,' ');decimal(m,image,c,m[0x1c9f6+id]);
        glyph(m,image,c,' ');glyph(m,image,c,' ');decimal(m,image,c,m[0x1c9de+id]);
        glyph(m,image,c,' ');glyph(m,image,c,' ');decimal(m,image,c,m[0x1c9ea+id]);
        for(unsigned j=0;j<3;++j)glyph(m,image,c,' ');
        decimal(m,image,c,m[0x1ca02+id]);
    }
    return image;
}
} // namespace scr::viewer
