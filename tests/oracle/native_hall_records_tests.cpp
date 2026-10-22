#include "native_race_initialization.h"
#include "race_timing_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using Ram=scr::viewer::NativeMemory;
namespace timing=scr::test::timing;
Ram original{},native{},baseline{};
std::array<std::uint8_t,4096> stackBytes{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
constexpr unsigned framebuffer=0x68000,renderBuffer=0x78000;
unsigned pc{},instructions{},maximumInstructions{},entry{};
bool invalid{},returned{},timingRun{};
std::uint64_t cases{},compared{},totalInstructions{},timingCases{},preparationCases{},transferCases{};
std::uint32_t randomState=0x48414c4c;
unsigned next(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
unsigned read(unsigned address) {
    if(address<original.size())return original[address];
    if(address>=stackBase&&address<stackTop)return stackBytes[address-stackBase];
    if(address>=sentinel&&address<sentinel+4){constexpr std::uint8_t stop[]{0x4e,0x72,0x27,0};return stop[address-sentinel];}
    invalid=true;return 0;
}
void write(unsigned address,unsigned value) {
    if(address<original.size()){original[address]=std::uint8_t(value);return;}
    if(address>=stackBase&&address<stackTop){stackBytes[address-stackBase]=std::uint8_t(value);return;}
    invalid=true;
}
void skipCall() {
    const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);
    const unsigned target=(read(sp)<<24)|(read(sp+1)<<16)|(read(sp+2)<<8)|read(sp+3);
    m68k_set_reg(M68K_REG_SP,sp+4);m68k_set_reg(M68K_REG_PC,target);
}
void hook(unsigned address) {
    pc=address;
    if(entry==0x5fbc6) {
        if(address==0x6444e){skipCall();return;}
        if(address==0x5fc30){returned=true;m68k_end_timeslice();return;}
    }
    if(address==sentinel){returned=true;m68k_end_timeslice();return;}
    if(address<0xe700||address>=0x724dc||++instructions>1000000)invalid=true;
    if(invalid)m68k_end_timeslice();
}
bool excluded(unsigned address) {
    if(entry==0x5fbc6&&address>=0x6a58c&&address<0x6a590)return true;
    if(!timingRun)return false;
    if(address>=framebuffer&&address<framebuffer+64000)return true;
    if(address>=renderBuffer&&address<renderBuffer+32000)return true;
    return address==0x1bb6d||address==0x1bb6e||address==0x1bbd3||
        (address>=0x5983e&&address<0x59845);
}
void execute(unsigned start,unsigned d0=0) {
    entry=start;instructions=0;invalid=returned=false;stackBytes.fill(0);
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    m68k_set_reg(M68K_REG_D0,d0);
    for(unsigned i=0;i<4;++i)stackBytes[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_PC,start);m68k_execute(100000000);
    const auto expectedSp=start==0x5fbc6?stackTop-8:stackTop;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=expectedSp) {
        std::ostringstream error;error<<"Original record routine failed at0x"<<std::hex<<pc
            <<" invalid="<<invalid<<" returned="<<returned<<" SP="<<m68k_get_reg(nullptr,M68K_REG_SP)<<" expected="<<expectedSp;
        throw std::runtime_error(error.str());
    }
    maximumInstructions=std::max(maximumInstructions,instructions);totalInstructions+=instructions;
}
void compare(const char* stage) {
    for(unsigned address=0;address<original.size();++address) {
        if(excluded(address))continue;
        ++compared;
        if(original[address]!=native[address]) {
            std::ostringstream error;error<<stage<<" case="<<cases<<" address=0x"<<std::hex<<address
                <<" original="<<unsigned(original[address])<<" native="<<unsigned(native[address]);
            throw std::runtime_error(error.str());
        }
    }
    ++cases;
}
void prepare(unsigned context) {
    original=baseline;
    original[0x1c9d0]=std::uint8_t(context);
    for(unsigned a=0x7a61a;a<0x7a81a;++a)original[a]=std::uint8_t(next());
    for(unsigned a=0x1c5ec;a<0x1c6ec;++a)original[a]=std::uint8_t(next());
    original[0x5d724]=std::uint8_t(next());
}
void hallPreparation() {
    timingRun=false;
    native=original;scr::viewer::prepareNativeHallOfFame(native);
    execute(0x5fbc6);compare("hall-record-bank-copy");++preparationCases;
}
void transfer(bool load) {
    timingRun=false;native=original;scr::viewer::transferNativeLapRecords(native,load);
    execute(0x5f25a,load?0x80:0);compare(load?"load-track-records":"store-track-records");++transferCases;
}
void recordCrossing(unsigned player,unsigned variation) {
    auto input=timing::readInputs(original.data());
    input.currentPlayerId=std::uint8_t(player);input.selectedTrackInDivision=0x80;
    input.segmentIndex={1,3};input.trackProgress={0x100,0x100};
    input.lapLineSegment=1;input.finishLineSegment=0;input.numberOfTrackSegments=44;
    input.maximumLaps=4;input.raceStartTimer=0;input.wheelMovementActive=0x80;
    input.raceCompletionCheckFlag=0;input.offTrackStateFlags=0;input.playerDistanceDifference=0;
    const unsigned nameLength=1+original[0x1ca33]*11/7;
    for(unsigned id=0;id<12;++id)for(unsigned character=0;character<12;++character)
        input.playerNames[16*id+character]=character<nameLength?std::uint8_t('A'+(id+character)%26):std::uint8_t(' ');
    timing::writeInputs(original.data(),input);
    original[0x57c3c]=0;original[0x5924a]=0;
    timing::putLong(original.data(),0x6a584,framebuffer);
    timing::putLong(original.data(),0x6a588,framebuffer+32000);
    timing::putLong(original.data(),0x6a58c,renderBuffer);
    auto state=timing::readState(original.data());
    state.crossingArmed={0,0};state.lapCounter={3,2};state.globalFrameCounter=0;
    state.throttleAccumulator=state.throttleFlag=state.lapDisplayDuration=0;
    state.blinkCountdown=state.raceComplete=state.winnerBits=state.outcomeFlags=0;
    state.displayFlags=0;state.messageActive=0;state.messageMode=0;
    state.frameBufferSyncMask=0x80;state.leaderIcon=11;state.resultsIcon=11;
    for(auto& time:state.times)time={9,0x59,0x99};
    state.times[0]={0,0x20,0x10};state.times[player+12]={0,0x40,0x00};
    if(variation==0){state.times[14]={0,0x20,0x23};state.times[15]={1,0,0x23};}
    if(variation==1){state.times[14]={0,0x21,0x00};state.times[15]={1,0x01,0x00};}
    if(variation==2){state.times[14]={0,0x19,0x99};state.times[15]={0,0x59,0x99};}
    timing::writeState(original.data(),state);
    native=original;const auto effects=scr::updateRaceTiming(input,state);(void)effects;
    timing::writeState(native.data(),state);
    timingRun=true;execute(timing::entry);compare("race-record-crossing");++timingCases;
    original=native;
    transfer(false);
    hallPreparation();
    if(player==11&&variation==1) {
        const unsigned slot=unsigned(native[0x648c2+native[0x1ca33]])*16;
        for(unsigned character=0;character<12;++character) {
            const auto expected=input.playerNames[16*player+character];
            if(native[0x1c5ec+slot+character]!=expected||native[0x1c66c+slot+character]!=expected)
                throw std::runtime_error("New Hall record did not preserve the complete entered name");
        }
        if(native[0x1c5ec+slot+12]!=0||native[0x1c5ec+slot+13]!=0x20||native[0x1c5ec+slot+14]!=0x23||
           native[0x1c66c+slot+12]!=1||native[0x1c66c+slot+13]!=0||native[0x1c66c+slot+14]!=0x23)
            throw std::runtime_error("New Hall record did not preserve the exact BCD lap/race time");
    }
}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){return read(a);}
unsigned int m68k_read_memory_16(unsigned int a){if(a&1)invalid=true;return (read(a)<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned int a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){write(a,v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(a&1)invalid=true;write(a,v>>8);write(a+1,v);}
void m68k_write_memory_32(unsigned int a,unsigned int v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try {
    if(argc!=2)throw std::runtime_error("Usage: native_hall_records_tests payload.bin");
    std::ifstream file(argv[1],std::ios::binary);
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    if(payload.size()!=408540)throw std::runtime_error("Payload size mismatch");
    std::copy(payload.begin(),payload.end(),baseline.begin()+0xe700);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned context=0;context<256;++context){prepare(context);hallPreparation();}
    for(unsigned context:{0u,11u})for(unsigned track=0;track<8;++track)for(unsigned variation=0;variation<8;++variation){
        prepare(context);original[0x1ca33]=std::uint8_t(track);transfer(true);
        for(unsigned a=0x5ec48;a<0x5ec61;++a)original[a]=std::uint8_t(next());
        for(unsigned field=0;field<3;++field){original[0x1c916+24*field]=std::uint8_t(next());original[0x1c917+24*field]=std::uint8_t(next());}
        transfer(false);hallPreparation();
    }
    for(unsigned context:{0u,11u})for(unsigned track=0;track<8;++track)for(unsigned player=0;player<12;++player)for(unsigned variation=0;variation<3;++variation){
        prepare(context);original[0x1ca33]=std::uint8_t(track);
        transfer(true);recordCrossing(player,variation);
    }
    std::cout<<"{\"cases\":"<<cases<<",\"preparation_cases\":"<<preparationCases<<",\"transfer_cases\":"<<transferCases
        <<",\"timing_cases\":"<<timingCases<<",\"compared_bytes\":"<<compared<<",\"original_instructions\":"<<totalInstructions
        <<",\"maximum_instructions\":"<<maximumInstructions<<",\"mismatches\":0,\"tracks\":8,\"contexts\":2,\"player_names\":12"
        <<",\"preparation_context_bytes\":256,\"record_comparisons\":[\"equal\",\"better\",\"slower\"],\"original_code_unmodified\":true"
        <<",\"preparation_fade_and_render_buffer_pointer_excluded\":true,\"timing_pixels_and_text_cursor_excluded\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
