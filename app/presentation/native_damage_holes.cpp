#include "presentation/native_damage_holes.h"
#include <stdexcept>

namespace scr::runtime {
namespace {
unsigned word(const NativeMemory& memory,unsigned a){return (unsigned(memory.at(a))<<8)|memory.at(a+1);}
unsigned longword(const NativeMemory& memory,unsigned a){return (word(memory,a)<<16)|word(memory,a+2);}
void word(NativeMemory& memory,unsigned a,unsigned value){memory.at(a)=std::uint8_t(value>>8);memory.at(a+1)=std::uint8_t(value);}
}
void initializeNativeDamageHoles(NativeMemory& memory){
    if(memory[0x1c9cf]>10)throw std::invalid_argument("Damage holes require an original position0..10");
    const auto frame=longword(memory,0x6a584);
    if((frame&1)||frame<0x1000||frame>memory.size()-64000)
        throw std::invalid_argument("Damage holes require the original framebuffer pair");
    for(unsigned id:{23u,24u,27u,28u}){
        const auto record=0x6a16c+16*id,source=longword(memory,0x6a4ac+4*id);
        if(word(memory,record+4)!=1||word(memory,record+6)!=7||word(memory,record+10)!=0||
                (source&1)||source<0x1000||source>memory.size()-160)
            throw std::invalid_argument("Damage holes require the original32x8 masked assets");
    }
    // Original0x60988 walks right to left. The selected parity variant is
    // already aligned to its 16-pixel destination word by asset initialization.
    for(int position=9;position>=0;--position){
        const unsigned coordinate=unsigned(position)*3+6;
        const unsigned id=(unsigned(position)>=memory[0x1c9cf]?23u:27u)+(coordinate&1u);
        const auto record=0x6a16c+16*id,source=longword(memory,0x6a4ac+4*id);
        word(memory,record+8,coordinate/2);
        for(unsigned screen=0;screen<2;++screen)for(unsigned row=0;row<8;++row)for(unsigned group=0;group<2;++group){
            const auto input=source+(row*2+group)*10;
            const auto preserve=word(memory,input);
            for(unsigned plane=0;plane<4;++plane){
                const auto output=frame+screen*32000+plane*8000+row*40+coordinate/2*2+group*2;
                word(memory,output,(word(memory,output)&preserve)|word(memory,input+2+plane*2));
            }
        }
    }
}
} // namespace scr::runtime
