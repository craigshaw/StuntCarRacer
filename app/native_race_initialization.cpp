#include "native_race_initialization.h"
#include "native_cockpit_presentation.h"
#include "../tests/driving_replay_fixture.h"
#include "../tests/opponent_driving_fixture.h"
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
void seasonConfiguration(NativeMemory& m) {
    if(m[0x5eb76]||m[0x57c3c])throw std::invalid_argument("Native seasons require one local player");
    if(m[0x1bb5f]>3)throw std::invalid_argument("Invalid season division");
    m[0x1bc5b]=m[0x5ab42+m[0x1bb5f]];
    m[0x1bc59]=std::uint8_t(m[0x1bc5b]+3);
}
void handicap(NativeMemory& m) {
    // Original0x5abd4, including its one random draw for human fixtures.
    m[0x1bbf7]=randomByte(m)>=0xa0?0x40:0;
    const auto first=m[0x1ca27],second=m[0x1ca28];
    ++m[0x1c9f6+first];++m[0x1c9f6+second];
    if(second==m[0x5eb79])m[0x1bbf7]=m[0x1bbb4];
    else if(first==m[0x5eb79])m[0x1bbf7]=m[0x1bbb4]^0xc0;
    else {
        const unsigned a=m[0x1c9d2+first],b=m[0x1c9d2+second];
        if(a>b||(a==b&&!(randomByte(m)&1)))m[0x1bbf7]^=0xc0;
    }
    const auto flags=m[0x1bbf7];
    m[0x1bb19]=(flags&0x80)?first:second;
    m[0x1bb18]=(flags&0x40)?first:second;
    ++m[0x1c9de+m[0x1bb19]];++m[0x1c9ea+m[0x1bb18]];
    if(m[0x1bb5f]==m[0x1c9ce]) {
        m[0x1ca26]=m[0x1bb18];m[0x1ca25]=m[0x1bb19];
    }
}
void sortSeason(NativeMemory& m) {
    // Original0x5acfe: adjacent exchanges repeat until there are no swaps.
    // Equal scores use random tie breaks, so a stable host sort is different.
    seasonConfiguration(m);
    const unsigned begin=m[0x1bc5b],end=m[0x1bc59];
    for(unsigned i=begin;i<end;++i) {
        const auto id=m[0x1c9c2+i];m[0x1ca0e+i]=id;
        m[0x1ca02+id]=std::uint8_t(2*m[0x1c9de+id]+m[0x1c9ea+id]);
    }
    do {
        m[0x1bb19]=0;
        for(unsigned i=begin;i+1<end;++i) {
            m[0x1bb18]=std::uint8_t(i);
            const auto first=m[0x1ca0e+i],second=m[0x1ca0e+i+1];
            const auto firstScore=std::int8_t(m[0x1ca02+first]),secondScore=std::int8_t(m[0x1ca02+second]);
            bool swap=firstScore<secondScore;
            if(firstScore==secondScore) {
                const auto firstWins=std::int8_t(m[0x1c9de+first]),secondWins=std::int8_t(m[0x1c9de+second]);
                swap=firstWins<secondWins||(firstWins==secondWins&&(randomByte(m)&1));
            }
            if(swap) {
                m[0x1bb1a]=second;m[0x1ca0e+i]=second;m[0x1ca0e+i+1]=first;
                ++m[0x1bb19];
            }
        }
    }while(m[0x1bb19]);
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
    if(memory[0x57c3c])throw std::invalid_argument("Native startup requires a local race");
    if(!memory[0x1ca1a]||memory[0x1ca1b]>=memory[0x1ca1a])
        throw std::invalid_argument("Native startup requires a loaded track and spawn segment");
    memory[0x57c5b]=memory[0x57c63]=0;
    initializeNativePlayerConfiguration(memory);
    memory[0x691b3]=memory[0x1c9d0]?0:9;memory[0x691b2]=memory[0x1c9d0]?8:10;
    memory[0x1bb1d]=memory[0x1ca1b];memory[0x1bb0c]=4;memory[0x1bbed]=0x4c;
    // Original initializeTrackCoordinates0x63a58 constructs road support and
    // seeds each opponent wheel before the two normal startup warm-up frames.
    scr::test::opponent::initializeCoordinates(memory,scr::test::opponent::readData(memory));
    scr::test::replay::NativeReplay state(memory);
    NativeCockpitPresentation cockpit(memory,true);
    state.presentation=[&](auto stage,const auto& ram,const auto& events){cockpit.update(stage,ram,events);};
    state.restartStage(memory[0x1ca1b]);
    memory=std::move(state.ram);
    cockpit.storeInitialState(memory);
}

void configureNativeSeasonMatch(NativeMemory& m) {
    seasonConfiguration(m);
    const unsigned round=m[0x1ca35],offset=m[0x1bc5b];
    if(round>=6)throw std::invalid_argument("Invalid original season round");
    m[0x1ca27]=m[0x1c9c2+offset+m[0x5abc8+round]];
    m[0x1ca28]=m[0x1c9c2+offset+m[0x5abce+round]];
    m[0x5eb79]=11;
}

void randomizeNativeSeasonCars(NativeMemory& m) {
    if(m[0x5eb76]||m[0x57c3c])throw std::invalid_argument("Native seasons require one local player");
    m[0x5eb78]=6;m[0x5eb7b]=0;
}

void scoreNativeSeasonRound(NativeMemory& m) {
    for(unsigned id=12;id>0;) {
        --id;m[0x1c9d2+id]=std::uint8_t((randomByte(m)&63)+m[0x5aaf4+id]);
    }
    for(unsigned division=0;division<4;++division) {
        m[0x1bb5f]=std::uint8_t(division);
        configureNativeSeasonMatch(m);handicap(m);sortSeason(m);
    }
}

void initializeNativeSeason(NativeMemory& m) {
    if(m[0x1c9ce]>3||(m[0x1c9d0]!=0&&m[0x1c9d0]!=11))
        throw std::invalid_argument("Invalid native season context");
    encodeNativeSeason(m);
    randomizeNativeSeasonCars(m);
    for(unsigned slot=24;slot>0;) {
        --slot;clearTimeSlot(m,slot);
        if(slot<16)m[0x1c908+slot]=9;
    }
    m[0x1ca23]=m[0x1ca24]=0;
}

void encodeNativeSeason(NativeMemory& m) {
    // Main-menu Start Season calls the original0x609ae encoder even without
    // disk I/O. Its random searches advance the stream consumed by the AI.
    // The packed league representation stays in local runtime RAM.
    for(unsigned attempts=0;attempts<256;++attempts) {
        m[0x1bb16]=0;
        for(unsigned i=0;i<5;++i)m[0x7a035+4-i]=m[0x625a2+i];
        for(unsigned i=0;i<12;++i)m[0x1c9b6+i]=m[0x1ed5b+i]^0x3b;
        bool failed=false;
        for(unsigned index=27;index>0;) {
            --index;m[0x1bb1a]=0;unsigned block=0;
            while(true) {
                ++m[0x1bb1a];
                if(!m[0x1bb1a]&&++block==128){failed=true;break;}
                m[0x1bb17]=randomByte(m);
                if(m[0x1bb17]!=m[0x1c9b6+index])continue;
                m[0x7a01a+index]=std::uint8_t(block);
                m[0x7a03f+index]=m[0x1bb1a];break;
            }
            if(failed)break;
        }
        if(!failed) {
            for(unsigned i=0;i<5;++i)m[0x7a03a+4-i]=m[0x625a2+i];
            m[0x1ca34]=0x80;return;
        }
        m[0x625a5]=0x3b;
    }
    throw std::runtime_error("Original league encoder could not find its random sequence");
}

void advanceNativeSeasonRound(NativeMemory& m) {
    ++m[0x5eb7b];++m[0x1ca35];
    writeWord(m,0x57c72,std::uint16_t(readWord(m,0x57c72)+1));
}

bool seekNativeSeasonRace(NativeMemory& m) {
    while(m[0x1ca35]<m[0x5eb78]) {
        m[0x1bb5f]=m[0x1c9ce];configureNativeSeasonMatch(m);
        const auto local=m[0x5eb79],first=m[0x1ca27],second=m[0x1ca28];
        if(first==local||second==local) {
            m[0x1ca29]=first==local?second:first;m[0x1bbb1]=0xc0;
            const unsigned ordinal=2*m[0x1c9ce]+(m[0x1ca35]&1);
            m[0x1ca33]=m[0x648c2+ordinal];
            m[0x1ca21]=m[0x648ca+(ordinal^(m[0x1c9d0]?1:0))];
            transferNativeLapRecords(m,true);
            return true;
        }
        scoreNativeSeasonRound(m);advanceNativeSeasonRound(m);
    }
    m[0x1ca35]=0;return false;
}

void transferNativeLapRecords(NativeMemory& m,bool load) {
    if(m[0x1ca33]>7)throw std::invalid_argument("Invalid original record track");
    const unsigned slot=std::uint8_t((m[0x648c2+m[0x1ca33]]+(m[0x1c9d0]?8:0))<<4);
    m[0x1bb1b]=load?0x80:0;
    for(unsigned i=0;i<12;++i){
        if(load){m[0x5ec48+i]=m[0x7a61a+slot+i];m[0x5ec55+i]=m[0x7a71a+slot+i];}
        else {m[0x7a61a+slot+i]=m[0x5ec48+i];m[0x7a71a+slot+i]=m[0x5ec55+i];}
    }
    for(unsigned field=0;field<3;++field){
        const unsigned lap=0x1c916+24*field,race=lap+1;
        if(load){m[lap]=m[0x7a61a+slot+12+field];m[race]=m[0x7a71a+slot+12+field];}
        else {m[0x7a61a+slot+12+field]=m[lap];m[0x7a71a+slot+12+field]=m[race];}
    }
}

void prepareNativeHallOfFame(NativeMemory& m) {
    m[0x5d724]=0x42;
    const unsigned bank=m[0x1c9d0]?128:0;
    std::copy_n(m.begin()+0x7a61a+bank,128,m.begin()+0x1c5ec);
    std::copy_n(m.begin()+0x7a71a+bank,128,m.begin()+0x1c66c);
}

void finishNativeSeason(NativeMemory& m) {
    // Gameplay effects of0x64916; font/cursor/framebuffer effects belong to
    // the native menu renderer. Preserve its promotion order before finding
    // the local player's next division.
    m[0x593f6]=0x80;
    for(unsigned id=0;id<12;++id)m[0x1c9c2+id]=m[0x1ca0e+id];
    const unsigned oldOffset=m[0x5ab42+m[0x1c9ce]];
    m[0x1bb16]=1;
    if(m[0x1ca0e+oldOffset]==m[0x5eb79])m[0x593f6]=m[0x5eb79];
    for(int division=2;division>=0;--division) {
        m[0x1bb5f]=std::uint8_t(division);seasonConfiguration(m);
        const unsigned offset=m[0x1bc5b];std::swap(m[0x1c9c2+offset],m[0x1c9c2+offset-1]);
    }
    m[0x1bb5f]=0xff;
    if(m[0x5eb79]==m[0x1ca0e]) {
        if(m[0x1c9d0]){m[0x1c9cf]=10;return;}
        m[0x1c9d0]=m[0x5eb79];
        for(unsigned id=0;id<12;++id)m[0x1c9c2+id]=std::uint8_t(id);
        m[0x1c9ce]=0;
    }else {
        unsigned index=11;
        while(index&&m[0x1c9c2+index]!=m[0x5eb79])--index;
        m[0x1c9ce]=m[0x64ac6+index];
    }
    if(!m[0x1c9ce])m[0x1c9cf]=10;
}
} // namespace scr::viewer
