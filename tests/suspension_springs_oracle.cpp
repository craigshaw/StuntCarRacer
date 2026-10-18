// Differential execution of original recovered bytes, with Musashi confined to
// this test executable. The external wrapper pins payload/code/table hashes.
#include "suspension_springs.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr unsigned load=0xe700, payloadSize=408540, entry=0x622dc, end=0x62424;
constexpr unsigned attenEntry=0x62424, attenEnd=0x62454, mulEntry=0x612a2, mulEnd=0x612be;
constexpr unsigned tableAddress=0x1eeca, stack=0x7f000, sentinel=0x7e000;
constexpr std::array<unsigned,6> wordOutputs{0x1bd40,0x1bd42,0x1bd44,0x1bd48,0x1bd4a,0x1bd4c};
constexpr std::array<unsigned,9> byteOutputs{0x1bd4e,0x1bd50,0x1bd52,0x1bb1a,0x1bb1b,0x1bb2b,0x1bb2c,0x1bb2d,0x1bbbb};
constexpr std::array<unsigned,6> scratchAddresses{0x1bb1a,0x1bb1b,0x1bb2b,0x1bb2c,0x1bb2d,0x1bbbb};
constexpr std::array<unsigned,4> callSites{0x62306,0x62334,0x6234a,0x6235c};
using Bytes=std::array<std::uint8_t,26>;
std::array<std::uint8_t,0x80000> ram{};
scr::SuspensionAttenuationTable attenuationTable{};
bool executing=false, returned=false, invalid=false, probe=false;
unsigned currentPc=0, instructions=0, byteWrites=0, wordWrites=0, stackBytes=0;
unsigned expectedStackMask=0, expectedReturn=0, callCount=0, probeAddress=0, probeReads=0;
std::uint32_t randomState=0x53505231, cpuState=0x36385350;
std::uint64_t cases=0, contractCases=0;

std::uint32_t next(std::uint32_t& state) {
    state^=state<<13; state^=state>>17; state^=state<<5; return state;
}
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
bool contains(unsigned a,unsigned n,unsigned begin,unsigned finish) {
    return a>=begin && a<finish && n<=finish-a;
}
std::uint16_t word(unsigned a) { return std::uint16_t((unsigned(ram[a])<<8)|ram[a+1]); }
std::uint32_t longword(unsigned a) { return (std::uint32_t(word(a))<<16)|word(a+2); }
void putWord(unsigned a,std::uint16_t v) { ram[a]=std::uint8_t(v>>8); ram[a+1]=std::uint8_t(v); }
void putLong(unsigned a,std::uint32_t v) { putWord(a,std::uint16_t(v>>16)); putWord(a+2,std::uint16_t(v)); }
Bytes stateBytes() {
    Bytes bytes{};
    std::copy_n(ram.begin()+0x1bd40,20,bytes.begin());
    for(unsigned i=0;i<6;++i) bytes[20+i]=ram[scratchAddresses[i]];
    return bytes;
}
void putState(const Bytes& b) {
    std::copy_n(b.begin(),20,ram.begin()+0x1bd40);
    for(unsigned i=0;i<6;++i) ram[scratchAddresses[i]]=b[20+i];
}
Bytes native(const scr::SuspensionSpringInputs& in,const Bytes& before) {
    const auto w=[&](unsigned i) { return std::uint16_t((unsigned(before[i])<<8)|before[i+1]); };
    scr::SuspensionSpringState state{};
    state.rollAngle=w(0); state.pitchAngle=w(2); state.yawAngle=w(4); state.retainedWord46=w(6);
    state.rollDisplacement=w(8); state.pitchReferenceZero=w(10); state.pitchDisplacement=w(12);
    state.rollComponent={before[14],before[15]}; state.pitchComponent={before[16],before[17]};
    state.yawComponent={before[18],before[19]};
    state.scratch={before[20],before[21],before[22],before[23],before[24],before[25]};
    scr::calculateSuspensionSpringForces(in,attenuationTable,state);
    Bytes after{};
    const auto put=[&](unsigned i,std::uint16_t v) { after[i]=std::uint8_t(v>>8); after[i+1]=std::uint8_t(v); };
    put(0,state.rollAngle); put(2,state.pitchAngle); put(4,state.yawAngle); put(6,state.retainedWord46);
    put(8,state.rollDisplacement); put(10,state.pitchReferenceZero); put(12,state.pitchDisplacement);
    after[14]=state.rollComponent.value; after[15]=state.rollComponent.retainedLowByte;
    after[16]=state.pitchComponent.value; after[17]=state.pitchComponent.retainedLowByte;
    after[18]=state.yawComponent.value; after[19]=state.yawComponent.retainedLowByte;
    after[20]=state.scratch.multiplier; after[21]=state.scratch.productLow;
    after[22]=state.scratch.rawDisplacement; after[23]=state.scratch.savedAttenuation;
    after[24]=state.scratch.attenuation; after[25]=state.scratch.signByte;
    return after;
}
bool validRead(unsigned a,unsigned n) {
    if(!contains(a,n,0,unsigned(ram.size())) || (n>1 && (a&1))) return false;
    if(!executing) return true;
    if(contains(a,n,sentinel,sentinel+8) || contains(a,n,stack-4,stack+4)) return true;
    if(contains(a,n,attenEntry,attenEnd+4)) return true;
    if(probe) return (a==probeAddress && n==1);
    if(contains(a,n,entry,end+4) || contains(a,n,mulEntry,mulEnd+4)) return true;
    if(contains(a,n,0x1bcb0,0x1bcbc) || contains(a,n,0x1bd38,0x1bd3a)) return true;
    if(contains(a,n,0x1bd40,0x1bd54) || contains(a,n,tableAddress,tableAddress+128)) return true;
    return n==1 && std::find(scratchAddresses.begin(),scratchAddresses.end(),a)!=scratchAddresses.end();
}
void hook(unsigned pc) {
    currentPc=pc;
    if(pc==sentinel) { returned=true; m68k_end_timeslice(); return; }
    const bool allowed=contains(pc,2,attenEntry,attenEnd)
        || (!probe && (contains(pc,2,entry,end)||contains(pc,2,mulEntry,mulEnd)));
    if(!allowed || ++instructions>128) { invalid=true; m68k_end_timeslice(); return; }
    if(!probe && std::find(callSites.begin(),callSites.end(),pc)!=callSites.end()) {
        if(expectedStackMask || m68k_get_reg(nullptr,M68K_REG_SP)!=stack) invalid=true;
        expectedStackMask=15; expectedReturn=pc+6; ++callCount;
    }
}
void prepare(unsigned start) {
    executing=false;
    putLong(0,stack); putLong(4,start); putLong(stack,sentinel);
    putWord(sentinel,0x4e72); putWord(sentinel+2,0x2700);
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r) m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),next(cpuState));
    // Normal-game contract: retain random upper word/low byte, zero bits8..15.
    m68k_set_reg(M68K_REG_D1,next(cpuState)&0xffff00ffu);
    m68k_set_reg(M68K_REG_SR,0x2700|(next(cpuState)&31));
    m68k_set_reg(M68K_REG_SP,stack); m68k_set_reg(M68K_REG_PC,start);
    instructions=byteWrites=wordWrites=stackBytes=expectedStackMask=callCount=probeReads=0;
    invalid=returned=false;
}
void run() {
    executing=true; m68k_execute(10000); executing=false;
    if(invalid || !returned || m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4 || expectedStackMask)
        throw std::runtime_error("Original execution/access/return contract failed at PC "+std::to_string(currentPc));
}
Bytes check(const scr::SuspensionSpringInputs& in,const Bytes& before) {
    putLong(0x1bcb0,in.frontLeftTravel); putLong(0x1bcb4,in.frontRightTravel);
    putLong(0x1bcb8,in.rearTravel); putWord(0x1bd38,in.averageVelocity); putState(before);
    probe=false; prepare(entry); run();
    if(wordWrites!=6 || byteWrites!=17 || stackBytes!=16 || callCount!=4)
        throw std::runtime_error("Original write/call count changed");
    const auto expected=stateBytes(), actual=native(in,before);
    if(expected!=actual) {
        std::cerr<<"Mismatch case "<<cases<<" inputs "<<in.frontLeftTravel<<' '<<in.frontRightTravel<<' '
                 <<in.rearTravel<<' '<<in.averageVelocity<<"\n";
        for(unsigned i=0;i<expected.size();++i) if(expected[i]!=actual[i])
            std::cerr<<"state byte "<<i<<" expected "<<unsigned(expected[i])<<" actual "<<unsigned(actual[i])<<"\n";
        throw std::runtime_error("Suspension spring differential mismatch");
    }
    ++cases;
    return actual;
}
void check(const scr::SuspensionSpringInputs& in) {
    Bytes before{}; for(auto& byte:before) byte=std::uint8_t(next(cpuState)); check(in,before);
}
void probeD1Contract() {
    // A standalone helper with nonzero D1 high byte really reads another page.
    // This is an execution probe of the documented precondition, not a claim
    // that the native API supports arbitrary surrounding memory as a table.
    constexpr std::array<unsigned,5> values{0,255,256,0x8000,0xffff};
    for(unsigned high=0;high<256;++high) for(auto value:values) {
        unsigned raw=value;
        if(raw&0x8000) raw=(0u-raw)&65535;
        raw=(!(raw&0x8000)&&raw>=256) ? 255 : (raw&255);
        const unsigned index=(high<<8)|(raw>>1);
        const auto signedIndex=index<32768 ? int(index) : int(index)-65536;
        probeAddress=unsigned(int(tableAddress)+signedIndex);
        const auto saved=ram[probeAddress]; ram[probeAddress]=0xa6;
        probe=true; prepare(attenEntry);
        m68k_set_reg(M68K_REG_D0,0xa55a0000u|value);
        m68k_set_reg(M68K_REG_D1,0x5aa50000u|(high<<8)|0x5a);
        run();
        if(probeReads!=1 || byteWrites!=2 || wordWrites || stackBytes || callCount
           || ram[0x1bb2b]!=raw || ram[0x1bb2d]!=0xa6
           || (m68k_get_reg(nullptr,M68K_REG_D1)&65535)!=index)
            throw std::runtime_error("Standalone attenuation D1-index contract probe failed");
        ram[probeAddress]=saved; ++contractCases;
    }
    probe=false;
}
void printBytes(const Bytes& bytes) {
    std::cout<<'['; for(unsigned i=0;i<bytes.size();++i) { if(i) std::cout<<','; std::cout<<unsigned(bytes[i]); } std::cout<<']';
}
} // namespace

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) {
    if(!validRead(a,1)) { invalid=true; return 0; }
    if(executing && probe && a==probeAddress) ++probeReads;
    return ram[a];
}
unsigned int m68k_read_memory_16(unsigned int a) {
    if(!validRead(a,2)) { invalid=true; return 0; }
    return word(a);
}
unsigned int m68k_read_memory_32(unsigned int a) {
    return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);
}
void m68k_write_memory_8(unsigned int a,unsigned int v) {
    const bool addressOk=probe ? (a==0x1bb2b||a==0x1bb2d)
        : std::find(byteOutputs.begin(),byteOutputs.end(),a)!=byteOutputs.end();
    if(!executing || !addressOk) { invalid=true; return; }
    ++byteWrites; ram[a]=std::uint8_t(v);
}
void m68k_write_memory_16(unsigned int a,unsigned int v) {
    if(executing && !probe && contains(a,2,stack-4,stack) && !(a&1)) {
        const unsigned shift=8*(4-(a-(stack-4))-2), mask=3u<<(a-(stack-4));
        if((expectedStackMask&mask)!=mask || std::uint16_t(v)!=std::uint16_t(expectedReturn>>shift)) { invalid=true; return; }
        expectedStackMask&=~mask; stackBytes+=2; putWord(a,std::uint16_t(v)); return;
    }
    if(!executing || probe || std::find(wordOutputs.begin(),wordOutputs.end(),a)==wordOutputs.end()) { invalid=true; return; }
    ++wordWrites; putWord(a,std::uint16_t(v));
}
void m68k_write_memory_32(unsigned int a,unsigned int v) {
    if(!executing || probe || a!=stack-4 || expectedStackMask!=15 || v!=expectedReturn) { invalid=true; return; }
    expectedStackMask=0; stackBytes+=4; putLong(a,v);
}
}

int main(int argc,char** argv) try {
    if(argc<2 || (argc-2)%2) throw std::runtime_error("Usage: suspension_springs_oracle payload.bin [label chip-ram.bin]...");
    const auto payload=readFile(argv[1]);
    if(payload.size()!=payloadSize) throw std::runtime_error("Unexpected original payload size");
    std::copy(payload.begin(),payload.end(),ram.begin()+load);
    std::copy_n(ram.begin()+tableAddress,128,attenuationTable.begin());
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":1397772849,\"captures\":[";
    for(int a=2;a<argc;a+=2) {
        const std::string label=argv[a];
        if(label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[a+1]);
        if(capture.size()!=ram.size()) throw std::runtime_error("Capture must be 512KiB chip RAM");
        for(auto range:std::array<std::array<unsigned,2>,4>{{{entry,end},{attenEntry,attenEnd},{mulEntry,mulEnd},{tableAddress,tableAddress+128}}})
            if(!std::equal(capture.begin()+range[0],capture.begin()+range[1],payload.begin()+range[0]-load))
                throw std::runtime_error("Capture original code/table mismatch");
        std::copy(capture.begin(),capture.end(),ram.begin());
        const scr::SuspensionSpringInputs in{longword(0x1bcb0),longword(0x1bcb4),longword(0x1bcb8),word(0x1bd38)};
        const auto before=stateBytes(), result=check(in,before);
        if(a>2) std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<"\",\"input_words\":["<<in.frontLeftTravel<<','<<in.frontRightTravel<<','<<in.rearTravel<<','<<in.averageVelocity
                 <<"],\"replayed_state_bytes\":"; printBytes(result);
        std::cout<<",\"stored_state_bytes\":"; printBytes(before);
        std::cout<<",\"stored_state_agrees\":"<<(before==result?"true":"false")<<'}';
    }
    const auto captures=cases;
    probeD1Contract();
    for(unsigned field=0;field<7;++field) for(unsigned value=0;value<65536;++value) {
        std::array<std::uint32_t,3> travel{0x80010001,0x7fffffff,0xffffffe1};
        std::uint16_t velocity=0x8001;
        if(field<6) {
            const unsigned shift=(field%2)*16;
            travel[field/2]=(travel[field/2]&~(65535u<<shift))|(std::uint32_t(value)<<shift);
        } else velocity=std::uint16_t(value);
        check({travel[0],travel[1],travel[2],velocity});
    }
    const auto sweeps=cases-captures;
    for(unsigned value=0;value<65536;++value) {
        check({value<<4,value<<4,0,0x8001}); // Every first attenuation word.
        check({value<<3,0,0,0x7fff});        // Every second attenuation word.
    }
    constexpr std::array<std::uint32_t,13> edges{0,1,7,8,15,16,0x7fffffff,0x80000000,0x80000001,0xffffffff,0xfffffff8,0x40000,0x80000};
    constexpr std::array<std::uint16_t,7> velocities{0,1,0x7fff,0x8000,0x8001,0xffff,0x100};
    for(auto l:edges) for(auto r:edges) for(auto rear:edges) for(auto v:velocities) check({l,r,rear,v});
    for(unsigned n=0;n<150000;++n) {
        const auto l=next(randomState), r=next(randomState), rear=next(randomState);
        const auto v=std::uint16_t(next(randomState)); check({l,r,rear,v});
    }
    std::cout<<"],\"entry_D1_high_byte\":0,\"standalone_D1_index_probe_cases\":"<<contractCases
             <<",\"single_input_halfword_sweep_cases\":"<<sweeps
             <<",\"full_displacement_word_sweep_cases\":131072,\"boundary_cases\":15379,\"random_cases\":150000"
             <<",\"total_cases\":"<<cases<<",\"mismatches\":0,\"writes_confined\":true,\"stack_checked\":true}\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<"\n"<<error.what()<<'\n'; return 1; }
