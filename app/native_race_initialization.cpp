#include "native_race_initialization.h"
#include "../tests/driving_replay_fixture.h"
#include <algorithm>
#include <stdexcept>

namespace scr::viewer {
namespace {
using scr_test::readWord;
using scr_test::readLong;
using scr_test::writeWord;
using scr_test::writeLong;

std::uint8_t randomByte(NativeMemory& memory) {
    const auto seed=readLong(memory,0x625a2);
    const auto result=std::uint8_t((std::uint16_t(seed)>>4)^((seed>>16)>>1));
    writeLong(memory,0x625a2,(seed<<8)|memory[0x625a6]);
    memory[0x625a6]=result;
    return result;
}
void clearTimeSlot(NativeMemory& memory,unsigned slot) {
    memory[0x1c908+slot]=memory[0x1c920+slot]=memory[0x1c938+slot]=0;
}
void resetMatrices(NativeMemory& memory) {
    TrigTable trig{};
    for(unsigned i=0;i<trig.size();++i)trig[i]=readWord(memory,0x1ca42+2*i);
    auto matrices=scr::test::motion::readMatrices(memory.data());
    updateTransformMatrices({readWord(memory,0x1bce4),readWord(memory,0x1bce6),
        readWord(memory,0x1bce8),readWord(memory,0x1bd5a)},trig,matrices);
    scr::test::motion::writeMatrices(memory.data(),matrices);
}
std::uint8_t bcdFuel(std::uint8_t count) {
    // Original ABCD loop executes 256 times for a zero byte. Its first SUBQ
    // underflows 0 to 255, setting X and adding an extra one on the next ABCD.
    // Retain that edge case in the early, pre-track game-data reset.
    if(!count)return 0x57;
    unsigned value=0;
    do {value=(value+1)%100;count=std::uint8_t(count-1);}while(count);
    return std::uint8_t((value/10)*16+value%10);
}
} // namespace

void initializeNativeLoadState(NativeMemory& memory) {
    // Original success path [0x5c960,0x5c96e) carries this disk immediate
    // through hardware cleanup and stores it at0x5cefa. Copying raw payload
    // bytes alone leaves the unrelated on-disk word at0x64aec, disabling yaw.
    // Amiga exception-vector/CIA/custom-chip writes have no native counterpart.
    writeLong(memory,0x64aec,readLong(memory,0x5c962));
    memory[0x1bb94]=0;
    const auto checksum=readLong(memory,0x7a21a)+readLong(memory,0x7a416);
    writeLong(memory,0x5cf2c,readLong(memory,0x5cf2c)^checksum);
    memory[0x64af0]=0x80;
}

void initializeNativePlayerConfiguration(NativeMemory& memory) {
    // Original 0x5d726..0x5d87e. Addresses describe its serialization format;
    // constants come from its operands, while configuration/sample data remain
    // in the local asset pack. CIA/audio hardware writes are handled by native
    // backends and have no game-memory result.
    std::fill(memory.begin()+0x1baf8,memory.begin()+0x1bdd0,std::uint8_t{});
    const unsigned context=memory[0x1c9d0];
    if(context!=0&&context!=11)throw std::invalid_argument("Unsupported player configuration context");
    std::copy_n(memory.begin()+0x1fe6c+context,11,memory.begin()+0x1baf8);
    writeWord(memory,0x1bc20,0x7f);memory[0x1bbdd]=0xba;
    std::fill_n(memory.begin()+0x1c908,3,std::uint8_t{9});
    const unsigned player=memory[0x5eb79];
    if(player>11)throw std::invalid_argument("Invalid local player ID");
    clearTimeSlot(memory,player+12);
    if(memory[0x57c3c]) {
        const unsigned opponent=memory[0x1ca29];
        if(opponent>11)throw std::invalid_argument("Invalid opponent ID");
        clearTimeSlot(memory,opponent+12);
    }
    resetMatrices(memory);
    writeWord(memory,0x1bc42,0x400);writeWord(memory,0x1bc56,0xff00);
    memory[0x1bb99]=4;
    // Original 0x636a2 reads a long and an overlapping byte, not the enhanced
    // source's separate savedRandomSeed3 symbol.
    writeLong(memory,0x625a2,readLong(memory,0x636b8));
    memory[0x625a6]=memory[0x636b9];memory[0x625a5]=0x3b;
    const auto fuel=bcdFuel(memory[context?0x1ca2d:0x1ca2c]);
    memory[0x1ca20]=memory[0x1bb95]=fuel;
    memory[0x1bbb5]=memory[0x1c9cf];
    std::fill_n(memory.begin()+0xead6,128,std::uint8_t{});
    std::fill_n(memory.begin()+0x3d80,10000,std::uint8_t{});
    // Original loadPaletteColors copies eight words into the copper list.
    for(unsigned i=0;i<8;++i)writeWord(memory,0xe7b0+4*i,readWord(memory,0x69eb4+2*i));
    for(unsigned offset=0;offset<64;offset+=2) {
        writeWord(memory,0x1be70+0x78+offset,0x8000);
        memory[0x1bdd0+offset]=0x80;
    }
}

void initializeNativeGameTables(NativeMemory& memory) {
    // Original 0x5aa38..0x5aa84.
    memory[0x1ca35]=memory[0x57c67]=0;writeWord(memory,0x57c72,0);
    std::fill_n(memory.begin()+0x1c9de,60,std::uint8_t{});
    for(unsigned i=0;i<12;++i) {
        memory[0x1ca0e+i]=std::uint8_t(i);
        memory[0x1ca36+i]=10;
    }
}

void initializeNativeGameData(NativeMemory& memory) {
    // Original 0x5a9b6..0x5aa38. Native startup chooses the local default car;
    // this choice is menu input, not saved-game state.
    memory[0x57c3c]=0;memory[0x1c9d0]=0;
    std::fill(memory.begin()+0x1baf8,memory.begin()+0x1ca36,std::uint8_t{});
    initializeNativePlayerConfiguration(memory);memory[0x5d724]=0;
    for(unsigned i=12;i>0;--i) {memory[0x1c9c2+i-1]=std::uint8_t(i-1);(void)randomByte(memory);}
    initializeNativeGameTables(memory);
    memory[0x1c9cf]=10;memory[0x1fef7]=0;memory[0x1fef8]=16;memory[0x1feed]=0x7e;
    writeLong(memory,0x2007a,0);writeLong(memory,0x2007e,0);memory[0x20080]=0x7e;
}

void initializeNativeRace(NativeMemory& memory) {
    if(memory[0x57c3c]||memory[0x1ca22]&0x80)
        throw std::invalid_argument("Native startup currently supports local Practice");
    if(!memory[0x1ca1a]||memory[0x1ca1b]>=memory[0x1ca1a])
        throw std::invalid_argument("Native startup requires a loaded track and spawn segment");
    memory[0x57c5b]=memory[0x57c63]=0;
    initializeNativePlayerConfiguration(memory);
    memory[0x691b3]=9;memory[0x691b2]=10;
    memory[0x1bb1d]=memory[0x1ca1b];memory[0x1bb0c]=4;memory[0x1bbed]=0x4c;
    // The original also builds opponent look-ahead geometry here. Practice has
    // no opponent simulation; preserve its one random advance for the startup
    // stream without executing that unused renderer/AI scratch path.
    const auto jitter=std::uint16_t((randomByte(memory)&0x7f)+0x68);
    for(unsigned i=0;i<4;++i)writeWord(memory,0x1bd66+2*i,std::uint16_t(readWord(memory,0x1bd86+2*i)+jitter));
    scr::test::replay::NativeReplay state(memory);
    state.restartStage(memory[0x1ca1b]);
    memory=std::move(state.ram);
}
} // namespace scr::viewer
