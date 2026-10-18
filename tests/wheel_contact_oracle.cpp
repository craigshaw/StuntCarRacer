// Execute original MC68000 interpolation/contact bytes and compare their stored
// results with the native functions. Musashi is a test-only dependency.
#include "wheel_contact.h"
#include "track_interpolation.h"
#include "m68k.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr unsigned base=0xe700,payloadSize=408540;
constexpr unsigned entry=0x5c5f2,postInterpolation=0x5c5f8,end=0x5c65a;
constexpr unsigned interpolation=0x5c554,bounce=0x5c808,bounceEnd=0x5c890;
constexpr unsigned scratch=0x1bb18,heights=0x1bca4,flag=0x1bb65,mode=0x1bb9a,phase=0x1bbda;
constexpr unsigned lateral=0x1bc22,direction=0x1bc32,speed=0x1bd5c,pitch=0x1bce4;
constexpr unsigned stack=0x7f000,sentinel=0x7e000;
constexpr std::uint32_t initialSeed=0x434f4e54,initialCpuSeed=0x36383034;
struct Case {
    scr::TrackInterpolationInputs track;
    scr::WheelContactInputs inputs;
    scr::WheelContactState state;
    std::uint32_t rawHeight;
    unsigned slot;
    bool interpolate;
};
std::array<std::uint8_t,0x80000> ram{};
std::uint32_t randomState=initialSeed,cpuRandomState=initialCpuSeed;
std::uint64_t totalCases=0,fullCases=0,directCases=0,totalInstructions=0;
std::array<std::uint64_t,5> paths{}; // average, replace, near edge, clamp, no edge
unsigned currentPc=0,instructions=0,selectedHeight=heights,heightMask=0,flagWrites=0;
unsigned minimumInstructions=1000,maximumInstructions=0;
bool executing=false,invalidAccess=false,returned=false;
std::string accessFailure;

std::uint32_t random32(std::uint32_t& state) {
    state^=state<<13; state^=state>>17; state^=state<<5; return state;
}
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
bool contains(unsigned address,unsigned size,unsigned begin,unsigned limit) {
    return address>=begin && address<limit && size<=limit-address;
}
std::uint16_t word(unsigned address) { return std::uint16_t((unsigned(ram[address])<<8)|ram[address+1]); }
std::uint32_t longword(unsigned address) { return (std::uint32_t(word(address))<<16)|word(address+2); }
void putWord(unsigned address,std::uint16_t value) {
    ram[address]=std::uint8_t(value>>8); ram[address+1]=std::uint8_t(value);
}
void putLong(unsigned address,std::uint32_t value) {
    putWord(address,std::uint16_t(value>>16)); putWord(address+2,std::uint16_t(value));
}
void failAccess(const char* description,unsigned address,unsigned size) {
    invalidAccess=true;
    if(accessFailure.empty()) {
        std::ostringstream message;
        message<<description<<" address=0x"<<std::hex<<address<<" size="<<std::dec<<size
               <<" instruction_pc=0x"<<std::hex<<currentPc;
        accessFailure=message.str();
    }
}
bool permittedRead(unsigned address,unsigned size) {
    if(!contains(address,size,0,unsigned(ram.size())) || (size>1 && (address&1))) return false;
    if(!executing) return true;
    return contains(address,size,interpolation,end+4) || contains(address,size,bounce,bounceEnd+4)
        || contains(address,size,sentinel,sentinel+8) || contains(address,size,stack-4,stack+4)
        || contains(address,size,scratch,scratch+4) || contains(address,size,heights,heights+12)
        || contains(address,size,0x1bc02,0x1bc0a) || contains(address,size,lateral,lateral+2)
        || (size==1 && (address==0x1bc4d || address==0x1bc41 || address==flag || address==mode
                        || address==phase || address==direction || address==speed || address==pitch));
}
bool permittedWrite(unsigned address,unsigned size) {
    bool permitted=false;
    if(executing && !(size>1 && (address&1))) {
        if((currentPc==entry || currentPc==0x5c60c) && contains(address,size,stack-4,stack)) permitted=true;
        if((currentPc==0x5c5ea || currentPc==0x5c84a || currentPc==0x5c872)
           && contains(address,size,scratch,scratch+4)) permitted=true;
        if((currentPc==0x5c626 || currentPc==0x5c652)
           && contains(address,size,selectedHeight,selectedHeight+4)) {
            permitted=true;
            for(unsigned i=0;i<size;++i) {
                const unsigned bit=1u<<(address+i-selectedHeight);
                if(heightMask&bit) { failAccess("Repeated height write",address,size); return false; }
                heightMask|=bit;
            }
        }
        if(currentPc==0x5c600 && address==flag && size==1) { permitted=true; ++flagWrites; }
        if(currentPc==0x5c86a && address==phase && size==1) permitted=true;
        if(currentPc==0x5c888 && address==mode && size==1) permitted=true;
    }
    if(!permitted) failAccess("Write outside contact contract",address,size);
    return permitted;
}
void instructionHook(unsigned pc) {
    currentPc=pc;
    if(invalidAccess) { m68k_end_timeslice(); return; }
    if(pc==sentinel) { returned=true; m68k_end_timeslice(); return; }
    if((!(pc>=interpolation && pc<end) && !(pc>=bounce && pc<bounceEnd)) || (pc&1) || ++instructions>110) {
        failAccess("Instruction outside routine/budget",pc,2); m68k_end_timeslice(); return;
    }
    switch(pc) {
    case 0x5c646: ++paths[0]; break;
    case 0x5c620: ++paths[1]; break;
    case 0x5c84a: ++paths[2]; break;
    case 0x5c872: ++paths[3]; break;
    default: break;
    }
}
scr::WheelContactState original(const Case& c) {
    const auto& t=c.track;
    putWord(0x1bc02,t.control1); putWord(0x1bc04,t.control2);
    putWord(0x1bc06,t.control3); putWord(0x1bc08,t.control4);
    ram[0x1bc4d]=t.lateral; ram[0x1bc41]=t.blend;
    putWord(lateral,c.inputs.lateralPosition); ram[direction]=c.inputs.reverseDirection;
    ram[speed]=c.inputs.speedHighByte; ram[pitch]=c.inputs.pitchHighByte;
    ram[flag]=c.state.outOfBoundsFlag; ram[mode]=c.state.renderingOrderMode; ram[phase]=c.state.bouncePhase;
    putLong(scratch,c.interpolate ? c.state.sampledHeight : c.rawHeight);
    std::array<std::uint32_t,3> before{};
    for(unsigned slot=0;slot<3;++slot) {
        before[slot]=slot==c.slot ? c.state.height : random32(randomState);
        putLong(heights+slot*4,before[slot]);
    }
    selectedHeight=heights+c.slot*4;
    const unsigned start=c.interpolate ? entry : postInterpolation;
    putLong(0,stack); putLong(4,start); putLong(stack,sentinel);
    putWord(sentinel,0x4e72); putWord(sentinel+2,0x2700);
    executing=false; m68k_pulse_reset();
    for(unsigned reg=0;reg<15;++reg)
        m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+reg),random32(cpuRandomState));
    // The actual caller selects D1.W=0,2,4. Bits above the word are unrelated.
    m68k_set_reg(M68K_REG_D1,(random32(cpuRandomState)&0xffff0000u)|(c.slot*2));
    m68k_set_reg(M68K_REG_SR,0x2700|(random32(cpuRandomState)&31));
    m68k_set_reg(M68K_REG_SP,stack); m68k_set_reg(M68K_REG_PC,start);
    instructions=heightMask=flagWrites=0; invalidAccess=returned=false; accessFailure.clear();
    currentPc=start; executing=true; m68k_execute(7000); executing=false;
    if(invalidAccess || !returned || heightMask!=15 || flagWrites!=1
       || m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4
       || (m68k_get_reg(nullptr,M68K_REG_D1)&0xffffu)!=c.slot*2) {
        std::ostringstream error;
        error<<"Contact oracle contract failure: "<<accessFailure<<" returned="<<returned
             <<" instructions="<<instructions<<" case="<<totalCases;
        throw std::runtime_error(error.str());
    }
    for(unsigned slot=0;slot<3;++slot)
        if(slot!=c.slot && longword(heights+slot*4)!=before[slot]) throw std::runtime_error("Unselected height changed");
    if(!(c.state.outOfBoundsFlag&0x80u)) ++paths[4];
    totalInstructions+=instructions;
    minimumInstructions=std::min(minimumInstructions,instructions);
    maximumInstructions=std::max(maximumInstructions,instructions);
    return {longword(selectedHeight),longword(scratch),ram[flag],ram[mode],ram[phase]};
}
scr::WheelContactState check(const Case& c) {
    const auto expected=original(c);
    auto actual=c.state;
    scr::applyWheelHeightContact(c.interpolate ? scr::interpolateTrackCoordinate(c.track) : c.rawHeight,c.inputs,actual);
    if(actual.height!=expected.height || actual.sampledHeight!=expected.sampledHeight
       || actual.outOfBoundsFlag!=expected.outOfBoundsFlag || actual.renderingOrderMode!=expected.renderingOrderMode
       || actual.bouncePhase!=expected.bouncePhase) {
        std::cerr<<"Contact mismatch case="<<totalCases<<" lateral="<<c.inputs.lateralPosition
                 <<" raw="<<c.rawHeight<<" speed="<<unsigned(c.inputs.speedHighByte)
                 <<" pitch="<<unsigned(c.inputs.pitchHighByte)<<" original_height="<<expected.height
                 <<" native_height="<<actual.height<<" original_scratch="<<expected.sampledHeight
                 <<" native_scratch="<<actual.sampledHeight<<" original_phase="<<unsigned(expected.bouncePhase)
                 <<" native_phase="<<unsigned(actual.bouncePhase)<<'\n';
        throw std::runtime_error("Wheel contact differential mismatch");
    }
    ++totalCases; if(c.interpolate) ++fullCases; else ++directCases;
    return actual;
}
Case randomCase(bool full) {
    const auto next=[] { return random32(randomState); };
    return {{std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next()),
             std::uint8_t(next()),std::uint8_t(next())},
            {std::uint16_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next())},
            {next(),next(),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next())},next(),next()%3,full};
}
}

extern "C" {
unsigned int m68k_read_memory_8(unsigned int address) {
    if(!permittedRead(address,1)) { failAccess("Invalid read",address,1); return 0; } return ram[address];
}
unsigned int m68k_read_memory_16(unsigned int address) {
    if(!permittedRead(address,2)) { failAccess("Invalid read",address,2); return 0; } return word(address);
}
unsigned int m68k_read_memory_32(unsigned int address) {
    if(!permittedRead(address,4)) { failAccess("Invalid read",address,4); return 0; } return longword(address);
}
void m68k_write_memory_8(unsigned int address,unsigned int value) {
    if(permittedWrite(address,1)) ram[address]=std::uint8_t(value);
}
void m68k_write_memory_16(unsigned int address,unsigned int value) {
    if(permittedWrite(address,2)) putWord(address,std::uint16_t(value));
}
void m68k_write_memory_32(unsigned int address,unsigned int value) {
    if(permittedWrite(address,4)) putLong(address,value);
}
}

int main(int argc,char** argv) try {
    if(argc<2 || (argc-2)%2) throw std::runtime_error("Usage: wheel_contact_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);
    if(payload.size()!=payloadSize) throw std::runtime_error("Expected original 408540-byte payload");
    std::copy(payload.begin(),payload.end(),ram.begin()+base);
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_set_instr_hook_callback(instructionHook);
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":"<<initialSeed<<",\"cpu_register_seed\":"<<initialCpuSeed<<",\"captures\":[";
    for(int argument=2;argument<argc;argument+=2) {
        const std::string label=argv[argument];
        if(label.empty() || label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[argument+1]);
        if(capture.size()!=ram.size()
           || !std::equal(payload.begin()+interpolation-base,payload.begin()+end-base,capture.begin()+interpolation)
           || !std::equal(payload.begin()+bounce-base,payload.begin()+bounceEnd-base,capture.begin()+bounce))
            throw std::runtime_error("Capture code identity mismatch");
        if(argument>2) std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<"\",\"wheels\":[";
        for(unsigned slot=0;slot<3;++slot) {
            std::copy(capture.begin(),capture.end(),ram.begin());
            const Case c{{word(0x1bc02),word(0x1bc04),word(0x1bc06),word(0x1bc08),ram[0x1bc4d],ram[0x1bc41]},
                         {word(lateral),ram[direction],ram[speed],ram[pitch]},
                         {longword(heights+slot*4),longword(scratch),ram[flag],ram[mode],ram[phase]},0,slot,true};
            const auto result=check(c);
            if(slot) std::cout<<',';
            std::cout<<"{\"slot\":"<<slot<<",\"old_height\":"<<c.state.height
                     <<",\"replayed_height\":"<<result.height<<",\"replayed_sample\":"<<result.sampledHeight
                     <<",\"stored_height_unchanged\":"<<(c.state.height==result.height?"true":"false")<<'}';
        }
        std::cout<<"]}";
    }
    const auto captureCases=totalCases;
    ram.fill(0); std::copy(payload.begin(),payload.end(),ram.begin()+base);
    auto c=randomCase(false); c.state.outOfBoundsFlag=0xff; c.inputs.speedHighByte=10;
    for(unsigned value=0;value<65536;++value) for(unsigned reverse:{0u,0x80u}) for(auto height:{0x2000u,0x10ffu}) {
        c.inputs.lateralPosition=std::uint16_t(value); c.inputs.reverseDirection=std::uint8_t(reverse);
        c.rawHeight=height; c.slot=value%3; check(c);
    }
    const auto lateralCases=totalCases-captureCases;
    const auto beforeSpeed=totalCases;
    c.inputs.lateralPosition=0x180; c.state.outOfBoundsFlag=0x75;
    for(unsigned velocity=0;velocity<256;++velocity) for(unsigned angle=0;angle<256;++angle)
        for(auto raw:{0x7fffffffu,0xffffffffu}) {
            c.inputs.speedHighByte=std::uint8_t(velocity); c.inputs.pitchHighByte=std::uint8_t(angle);
            c.rawHeight=raw; c.state.height=0xfffffffe; check(c);
        }
    const auto speedPitchCases=totalCases-beforeSpeed;
    const auto beforeFlags=totalCases;
    c.rawHeight=0x2000; c.inputs.speedHighByte=10;
    for(unsigned field=0;field<4;++field) for(unsigned value=0;value<256;++value) for(unsigned position:{0x180u,0xffe0u,0x400u}) {
        c.state.outOfBoundsFlag=0x80; c.inputs.reverseDirection=0; c.state.renderingOrderMode=0x53; c.state.bouncePhase=0x2d;
        c.inputs.lateralPosition=std::uint16_t(position);
        if(field==0) c.state.outOfBoundsFlag=std::uint8_t(value);
        if(field==1) c.inputs.reverseDirection=std::uint8_t(value);
        if(field==2) c.state.renderingOrderMode=std::uint8_t(value);
        if(field==3) c.state.bouncePhase=std::uint8_t(value);
        check(c);
    }
    const auto byteCases=totalCases-beforeFlags;
    const auto beforeBoundary=totalCases;
    constexpr std::array<std::uint32_t,19> boundaries{0,1,2,0xff,0x100,0xfff,0x1000,0x10ff,0x1100,0x1101,
        0x12ff,0x1300,0x7fffffff,0x80000000,0x80000100,0xffffefff,0xffffff00,0xfffffffe,0xffffffff};
    for(auto raw:boundaries) for(auto old:boundaries)
        for(unsigned position:{0u,0x14fu,0x150u,0x180u,0x1b0u,0x1b1u,0x8000u,0xffcfu,0xffd0u,0xffffu})
            for(unsigned setting=0;setting<3;++setting) {
                c.rawHeight=raw; c.state.height=old; c.state.outOfBoundsFlag=setting==0?0x33:0xb3;
                c.inputs.lateralPosition=std::uint16_t(position); c.inputs.reverseDirection=0x80;
                c.inputs.speedHighByte=setting==2?10:9; c.inputs.pitchHighByte=0x80; check(c);
            }
    const auto boundaryCases=totalCases-beforeBoundary;
    const auto beforeFractions=totalCases;
    for(unsigned left=0;left<256;++left) for(unsigned along=0;along<256;++along) {
        c=randomCase(true); c.track={0x18,0x48,0x50,0x20,std::uint8_t(left),std::uint8_t(along)};
        c.inputs.lateralPosition=std::uint16_t(along&1?0x180+left:0u-left); c.state.outOfBoundsFlag|=0x80;
        check(c);
    }
    const auto fractionCases=totalCases-beforeFractions;
    constexpr unsigned randomCases=200000;
    for(unsigned i=0;i<randomCases;++i) check(randomCase((i&1)!=0));
    for(auto count:paths) if(!count) throw std::runtime_error("Missing original branch coverage");
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"lateral_sweep_cases\":"<<lateralCases
             <<",\"speed_pitch_cases\":"<<speedPitchCases<<",\"flag_byte_cases\":"<<byteCases
             <<",\"boundary_cases\":"<<boundaryCases<<",\"fraction_cases\":"<<fractionCases
             <<",\"random_cases\":"<<randomCases<<",\"full_interpolation_contact_cases\":"<<fullCases
             <<",\"post_interpolation_contact_cases\":"<<directCases<<",\"total_cases\":"<<totalCases
             <<",\"mismatches\":0,\"instructions\":"<<totalInstructions
             <<",\"instruction_count_min\":"<<minimumInstructions<<",\"instruction_count_max\":"<<maximumInstructions
             <<",\"path_counts\":{\"average\":"<<paths[0]<<",\"replace\":"<<paths[1]
             <<",\"near_edge\":"<<paths[2]<<",\"clamp\":"<<paths[3]<<",\"no_edge\":"<<paths[4]<<"}}\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
