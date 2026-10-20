#include "native_hall_of_fame.h"
#include "planar_image.h"
#include <algorithm>

namespace scr::viewer {
namespace {
unsigned word(const NativeMemory& m,unsigned a){return (unsigned(m.at(a))<<8)|m.at(a+1);}
void glyph(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned value){
    drawNativeMenuCharacter(m,image,c,value);
}
void time(const NativeMemory& m,NativeMenuImage& image,NativeMenuTextCursor& c,unsigned record){
    if(m[record]>=9||(m[record]|m[record+1])==0){
        for(unsigned i=0;i<7;++i)glyph(m,image,c,'-');
        return;
    }
    auto bcd=[&](unsigned value){glyph(m,image,c,'0'+(value>>4));glyph(m,image,c,'0'+(value&15));};
    glyph(m,image,c,'0'+m[record]);glyph(m,image,c,':');
    bcd(m[record+1]);glyph(m,image,c,'.');bcd(m[record+2]);
}
}
NativeMenuImage makeNativeHallOfFameImage(const NativeMemory& m){
    NativeMenuImage image;
    for(unsigned i=0;i<16;++i)image.palette[i]=originalPaletteRgb(std::uint16_t(word(m,0x30bd0+i*2)));
    const auto decoded=decodePlanarRle(m.data()+0x30bf0,m.size()-0x30bf0,320,200,4);
    const auto background=decodePlanarImage(decoded.planes.data(),decoded.planes.size(),320,200,4);
    std::copy(background.pixels.begin(),background.pixels.end(),image.indices.begin());
    NativeMenuTextCursor cursor;
    cursor.yOffset=8;cursor.horizontalOffset=2;
    if(m[0x1c9d0]){
        cursor.colour=7;drawNativeMenuString(m,image,cursor,0x5ec92,0x4b);
    }
    cursor.colour=8;drawNativeMenuString(m,image,cursor,0x5ec92,0x5b);
    const unsigned bank=m[0x1c9d0]?128:0;
    for(unsigned row=0;row<8;++row){
        const unsigned track=7-row;
        cursor.column=0;cursor.row=9+row*2;cursor.horizontalOffset=1;cursor.colour=15;
        glyph(m,image,cursor,m[0x5fde0+track*2]);glyph(m,image,cursor,m[0x5fde1+track*2]);
        glyph(m,image,cursor,' ');cursor.horizontalOffset=4;
        for(unsigned type=0;type<2;++type){
            const unsigned record=(type?0x7a71a:0x7a61a)+bank+track*16;
            cursor.colour=7;
            for(unsigned i=0;i<12;++i)glyph(m,image,cursor,m[record+i]);
            cursor.colour=15;glyph(m,image,cursor,' ');--cursor.horizontalOffset;
            time(m,image,cursor,record+12);
            if(!type){glyph(m,image,cursor,' ');glyph(m,image,cursor,' ');cursor.horizontalOffset=2;}
        }
    }
    return image;
}
} // namespace scr::viewer
