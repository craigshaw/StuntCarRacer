#include "native_wheel_sprites.h"
#include <algorithm>
#include <stdexcept>

namespace scr::viewer {
namespace {
void chipExtent(const std::uint8_t* r,std::size_t size) {
    if(!r||size!=0x80000)throw std::invalid_argument("Expected 512 KiB initialized Chip RAM image");
}
std::uint16_t word(const std::uint8_t* r,std::size_t size,std::size_t address) {
    if(address>=size||size-address<2)throw std::invalid_argument("Wheel sprite word outside local assets");
    return static_cast<std::uint16_t>((unsigned(r[address])<<8)|r[address+1]);
}
std::uint32_t longword(const std::uint8_t* r,std::size_t size,std::size_t address) {
    return (std::uint32_t(word(r,size,address))<<16)|word(r,size,address+2);
}
std::uint32_t hardwareColour(std::uint16_t colour) {
    return (((colour>>8)&15)*17u<<16)|(((colour>>4)&15)*17u<<8)|((colour&15)*17u);
}
}
NativeWheelSprites decodeNativeWheelSprites(const std::uint8_t* ram,std::size_t size) {
    chipExtent(ram,size);NativeWheelSprites sprites;
    for(unsigned i=0;i<4;++i)sprites.palette[i]=hardwareColour(word(ram,size,0x69eb4+2*i));
    for(unsigned i=0;i<12;++i) {
        auto& image=sprites.images[i];const unsigned id=37+i;
        image.address=longword(ram,size,0x6a4ac+4*id);sprites.animation.spritePointers[i]=image.address;
        const auto parameters=0x6a16c+16*id;
        const unsigned width=word(ram,size,parameters+4)+1,rows=word(ram,size,parameters+6)+1;
        if(width!=1||rows>256||image.address<0x1000||image.address>=size||
           size-image.address<8+rows*4)throw std::invalid_argument("Wheel sprite extent outside local assets");
        image.rows.resize(rows);
        for(unsigned row=0;row<rows;++row)image.rows[row]={{word(ram,size,image.address+4+4*row),
            word(ram,size,image.address+6+4*row)}};
        if(longword(ram,size,image.address+4+4*rows)!=0)
            throw std::invalid_argument("Wheel sprite has no original DMA terminator");
    }
    return sprites;
}
NativeWheelSpriteFrame captureNativeWheelSpriteFrame(const NativeWheelSprites& sprites,
        const std::uint8_t* ram,std::size_t size) {
    chipExtent(ram,size);NativeWheelSpriteFrame frame;
    const auto displayTop=word(ram,size,0x69ec4);
    for(unsigned i=0;i<12;++i) {
        const auto address=sprites.images[i].address;
        auto position=word(ram,size,address),control=word(ram,size,address+2);
        if(ram[0x69f41]) {
            // Original updateSpritePositions [0x69f04,0x69f40) writes only
            // header bytes 0 and 2, retaining the previous byte-sized height.
            const auto oldRows=static_cast<std::uint8_t>((control>>8)-(position>>8));
            const auto top=static_cast<std::uint8_t>(word(ram,size,0x6a16c+16*(37+i)+10)+displayTop);
            position=static_cast<std::uint16_t>((unsigned(top)<<8)|(position&255));
            control=static_cast<std::uint16_t>((unsigned(static_cast<std::uint8_t>(top+oldRows))<<8)|(control&255));
        }
        if(control&0x80)throw std::invalid_argument("Original wheel sprites use independent two-plane mode");
        const unsigned start=(position>>8)|((control&4)<<6),end=(control>>8)|((control&2)<<7);
        auto& placement=frame.placements[i];
        placement.x=int(((position&255)<<1)|(control&1))-0x80;
        placement.y=int(start)-int(displayTop);
        // Hardware stops DMA at VSTOP. Startup stores source height minus one
        // in that field, so the source's final row is intentionally not drawn.
        placement.rows=(end-start)&511;
        if(placement.rows>sprites.images[i].rows.size())
            throw std::invalid_argument("Wheel sprite header exceeds immutable source rows");
    }
    return frame;
}
void composeNativeWheelSprites(const NativeWheelSprites& sprites,const NativeWheelSpriteFrame& frame,
        const WheelAnimationState& animation,std::vector<std::uint32_t>& pixels) {
    if(pixels.size()!=320*200)throw std::invalid_argument("Expected 320 x 200 replay canvas");
    // Sprite 0 has priority over sprite 1. They do not overlap in the original
    // wheel geometry, but preserve hardware priority for clipped edge cases.
    for(unsigned wheel=2;wheel>0;--wheel) {
        const auto pointer=animation.spritePointers[wheel-1];
        const auto found=std::find(sprites.animation.spritePointers.begin(),sprites.animation.spritePointers.end(),pointer);
        if(found==sprites.animation.spritePointers.end()) {
            // Before the first display VBlank both pointers select the original
            // empty sprite. Its address is deliberately not a wheel image.
            if(pointer==0||pointer==0xf4b4)continue;
            throw std::invalid_argument("Wheel sprite pointer is outside initialized wheel assets");
        }
        const auto index=static_cast<unsigned>(found-sprites.animation.spritePointers.begin());
        const auto& image=sprites.images[index];const auto& placement=frame.placements[index];
        if(placement.rows>image.rows.size())throw std::invalid_argument("Wheel sprite placement exceeds source image");
        for(unsigned row=0;row<placement.rows;++row)for(unsigned bit=0;bit<16;++bit) {
            const int x=placement.x+int(bit),y=placement.y+int(row);
            if(x<0||x>=320||y<0||y>=200)continue;
            const auto mask=1u<<(15-bit);
            const unsigned colour=((image.rows[row][0]&mask)?1u:0u)|((image.rows[row][1]&mask)?2u:0u);
            if(colour)pixels[unsigned(y)*320+unsigned(x)]=sprites.palette[colour];
        }
    }
}
} // namespace scr::viewer
