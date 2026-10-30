#include "assets/native_asset_initialization.h"
#include "audio/engine_audio.h"
#include "render/planar_image.h"
#include <algorithm>
#include <stdexcept>

namespace scr::runtime {
namespace {
constexpr unsigned graphicsParameters=0x6a16c,graphicsPointers=0x6a4ac;
constexpr unsigned graphicsSource=0x6a5b6,graphicsDestination=0x6490;
constexpr unsigned frameBuffer=0x6a594,frameBytes=32000;
std::uint16_t word(const NativeMemory& memory,unsigned address) {
    if(address>=memory.size()-1)throw std::invalid_argument("Native asset word outside memory");
    return static_cast<std::uint16_t>((unsigned(memory[address])<<8)|memory[address+1]);
}
void putWord(NativeMemory& memory,unsigned address,std::uint16_t value) {
    if(address>=memory.size()-1)throw std::invalid_argument("Native asset word outside memory");
    memory[address]=static_cast<std::uint8_t>(value>>8);memory[address+1]=static_cast<std::uint8_t>(value);
}
void putLong(NativeMemory& memory,unsigned address,std::uint32_t value) {
    putWord(memory,address,static_cast<std::uint16_t>(value>>16));
    putWord(memory,address+2,static_cast<std::uint16_t>(value));
}
void decompressBackgroundToGraphicsSource(NativeMemory& memory,const DecodedPlanarRle& background) {
    // Original decompressRLEObjectToMask [0x59450,0x594c6): interleave
    // four plane words for each group of sixteen pixels.
    for(unsigned y=0;y<200;++y)for(unsigned group=0;group<20;++group)for(unsigned plane=0;plane<4;++plane) {
        const auto source=plane*8000+y*40+group*2;
        const auto destination=graphicsSource+y*160+group*8+plane*2;
        memory[destination]=background.planes[source];memory[destination+1]=background.planes[source+1];
    }
}
void loadGraphicsElement(NativeMemory& memory,unsigned id,unsigned& destination) {
    const auto parameters=graphicsParameters+16*id;
    const auto x=unsigned(word(memory,parameters)&255),y=unsigned(word(memory,parameters+2)&255);
    const auto width=unsigned(word(memory,parameters+4))+1,height=unsigned(word(memory,parameters+6))+1;
    if(width>20||height>200||x+width>20||y+height>200)
        throw std::invalid_argument("Native asset graphic extent outside original image");
    auto source=graphicsSource+x*8+y*160;
    putLong(memory,graphicsPointers+4*id,destination);
    const bool hardware=id>=37&&id<=48;memory[0x69f40]=hardware?0x80:0;
    if(hardware) {
        // Original loadHardwareSprite [0x69e30,0x69e9a). The terminating
        // position word uses the original stored height minus one.
        const auto horizontal=static_cast<std::uint16_t>((word(memory,parameters+8)<<4)+0x80);
        const auto top=static_cast<std::uint16_t>(word(memory,parameters+10)+word(memory,0x69ec4));
        const auto end=static_cast<std::uint16_t>(top+height-1);
        putWord(memory,destination,static_cast<std::uint16_t>((top<<8)|(horizontal>>1)));destination+=2;
        putWord(memory,destination,static_cast<std::uint16_t>((end<<8)|((top&0x100)?4:0)|((end&0x100)?2:0)|(horizontal&1)));destination+=2;
        for(unsigned row=0;row<height;++row) {
            putWord(memory,destination,static_cast<std::uint16_t>(~word(memory,source)));destination+=2;
            putWord(memory,destination,word(memory,source+4));destination+=2;source+=160;
        }
        putLong(memory,destination,0);destination+=4;return;
    }
    // Original loadBitmapData [0x69dbe,0x69e30). Palette index 1 is
    // transparent; emit its preserve mask followed by the four masked planes.
    for(unsigned row=0;row<height;++row)for(unsigned group=0;group<width;++group) {
        const auto at=source+row*160+group*8;
        const std::array<std::uint16_t,4> planes{{word(memory,at),word(memory,at+2),word(memory,at+4),word(memory,at+6)}};
        const auto preserve=static_cast<std::uint16_t>(planes[0]&~planes[1]&~planes[2]&~planes[3]);
        putWord(memory,destination,preserve);destination+=2;
        for(auto plane:planes){putWord(memory,destination,static_cast<std::uint16_t>(plane&~preserve));destination+=2;}
    }
}
}
void initializeNativeAssets(NativeMemory& memory) {
    // Recreate only the memory effects of original initialize [0xed56,0xee8a].
    // Hardware vectors, CIA and custom-register writes belong to the Amiga OS
    // and display device, so the native application does not install them.
    putLong(memory,0x6a584,frameBuffer);putLong(memory,0x6a58c,frameBuffer);
    putLong(memory,0x6a588,frameBuffer+frameBytes);putLong(memory,0x6a590,frameBuffer+0x284);
    putLong(memory,0x1b812,frameBuffer+frameBytes);memory[0x1b816]=0x80;
    putWord(memory,0x69ec4,0x3c);
    for(unsigned sprite=0;sprite<8;++sprite) {
        putWord(memory,0xe7f0+8*sprite,0);putWord(memory,0xe7f4+8*sprite,0xf4b4);
        putWord(memory,0xe7b0+4*sprite,word(memory,0x69eb4+2*sprite));
    }
    for(unsigned at=0xf4b8;at<0x1aa4a;++at)memory[at]^=0x80;
    const auto engine=generateEngineSamples(memory.data()+originalEngineSampleAddress,originalEngineSampleBytes);
    std::copy(engine.generatedSamples.begin(),engine.generatedSamples.end(),memory.begin()+generatedEngineSampleAddress);
    for(unsigned level=0;level<engine.levels.size();++level) {
        putLong(memory,0xf474+8*level,engine.levels[level].address);
        putLong(memory,0xf478+8*level,engine.levels[level].wordLength);
    }
    putLong(memory,0x1b73a,originalEngineSampleAddress);putLong(memory,0x1b73e,originalEngineSampleBytes);
    std::fill(memory.begin()+0xead6,memory.begin()+0xeb56,0);
    std::fill(memory.begin()+0x7a01a,memory.begin()+0x7b6fa,0);
    // initializeGraphicsData first extracts the sprites/wheels from the disk's
    // original interleaved sheet, then decodes the cockpit sheet into that work
    // area and extracts its five masked objects.
    const auto background=decodePlanarRle(memory.data()+0x201a4,memory.size()-0x201a4,320,200,4);
    unsigned destination=graphicsDestination;
    for(unsigned id=0;id<52;++id)if(id<10||id>=15)loadGraphicsElement(memory,id,destination);
    decompressBackgroundToGraphicsSource(memory,background);
    for(unsigned id=10;id<15;++id)loadGraphicsElement(memory,id,destination);
    // Original startup's font patch [0x5aa02,0x5aa38), including the complete
    // longword clears even where the original file already contains zero.
    memory[0x1fef7]=0;memory[0x1fef8]=0x10;memory[0x1feed]=0x7e;
    putLong(memory,0x2007a,0);putLong(memory,0x2007e,0);memory[0x20080]=0x7e;
    // Original copyMainGameBackground [0x1bace,0x1baf8). This deliberately
    // overwrites the temporary sheet after all masked objects are extracted.
    std::copy(background.planes.begin(),background.planes.end(),memory.begin()+frameBuffer);
    std::copy(background.planes.begin(),background.planes.end(),memory.begin()+frameBuffer+frameBytes);
}
} // namespace scr::runtime
