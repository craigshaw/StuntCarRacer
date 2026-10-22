// Test-only differential execution of recovered 68000 bytes. No arithmetic
// re-encoding of those bytes is used as the oracle. Musashi is not linked into
// the native game library. The wrapper pins the supplied payload/table hashes.
#include "transform_matrices.h"
#include "wheel_coordinates.h"
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
constexpr unsigned loadAddress=0xe700, payloadSize=408540;
constexpr unsigned producerBegin=0x61368, producerEnd=0x6158c;
constexpr unsigned trigBegin=0x64d08, cosineEntry=0x64d10, trigEnd=0x64d66;
constexpr unsigned wheelBegin=0x618ce, wheelEnd=0x61950;
constexpr unsigned matrixAddress=0x1c230, tableAddress=0x1ca42;
constexpr unsigned stack=0x7f000, sentinel=0x7e000;
constexpr std::uint32_t initialSeed=0x53435232, initialCpuSeed=0x36383030;
constexpr std::array<unsigned,4> angleAddresses{0x1bce4,0x1bce6,0x1bce8,0x1bd5a};
constexpr std::array<unsigned,6> wheelOutputs{0x1bd02,0x1bd04,0x1bd06,0x1bd08,0x1bd0a,0x1bd0c};
constexpr std::array<unsigned,6> holes{0x26,0x32,0x36,0x3a,0x3c,0x40};
constexpr std::array<unsigned,8> callSites{0x61374,0x61394,0x613bc,0x613d0,0x613e8,0x613f8,0x61410,0x61420};
using AngleWords=std::array<std::uint16_t,4>;
using MatrixWords=std::array<std::uint16_t,36>;
using WheelWords=std::array<std::uint16_t,6>;
enum class Phase { trig, producer, wheel };
std::array<std::uint8_t,0x80000> ram{};
scr::TrigTable trigTable{};
Phase phase=Phase::trig;
bool executing=false, returned=false, invalidAccess=false;
unsigned currentPc=0, instructions=0, matrixWrites=0, wheelWrites=0, stackBytes=0, jsrCalls=0;
unsigned instructionMaximum=0, producerInstructionMaximum=0, trigInstructionMaximum=0;
std::array<bool,36> matrixWritten{};
std::array<bool,6> wheelWritten{};
std::uint32_t randomState=initialSeed, cpuRandomState=initialCpuSeed;
std::uint64_t trigCases=0, matrixCases=0, chainedWheelCases=0, totalInstructions=0;
std::string accessFailure;
struct ExpectedStackWrite { unsigned address=0, size=0, mask=0; std::uint32_t value=0; } stackWrite;

std::uint32_t nextRandom(std::uint32_t& state) {
    state^=state<<13; state^=state>>17; state^=state<<5; return state;
}
std::uint16_t randomWord() { return std::uint16_t(nextRandom(randomState)); }
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
bool contains(unsigned address,unsigned size,unsigned begin,unsigned end) {
    return address>=begin && address<end && size<=end-address;
}
std::uint16_t word(unsigned address) {
    return std::uint16_t((unsigned(ram[address])<<8)|ram[address+1]);
}
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
    if(!executing) return true; // Reset vectors and test setup are outside execution.
    if(contains(address,size,sentinel,sentinel+8) || contains(address,size,stack-6,stack+4)) return true;
    if(phase==Phase::wheel)
        return contains(address,size,wheelBegin,wheelEnd+4)
            || contains(address,size,matrixAddress,matrixAddress+72)
            || contains(address,size,wheelOutputs.front(),wheelOutputs.back()+2);
    if(contains(address,size,trigBegin,trigEnd+4)
       || contains(address,size,tableAddress,tableAddress+unsigned(trigTable.size()*2))) return true;
    if(phase==Phase::producer) {
        if(contains(address,size,producerBegin,producerEnd+4)
           || contains(address,size,matrixAddress,matrixAddress+72)) return true;
        for(auto a:angleAddresses) if(contains(address,size,a,a+2)) return true;
    }
    return false;
}
bool permittedWrite(unsigned address,unsigned size,unsigned value) {
    if(!executing || !contains(address,size,0,unsigned(ram.size())) || (address&1)) {
        failAccess("Invalid write",address,size); return false;
    }
    if(phase==Phase::producer && size==2
       && contains(address,size,matrixAddress,matrixAddress+72)) {
        const auto offset=address-matrixAddress;
        if(std::find(holes.begin(),holes.end(),offset)!=holes.end()) {
            failAccess("Write to preserved matrix hole",address,size); return false;
        }
        ++matrixWrites; matrixWritten[offset/2]=true; return true;
    }
    if(phase==Phase::wheel && size==2) {
        const auto found=std::find(wheelOutputs.begin(),wheelOutputs.end(),address);
        if(found!=wheelOutputs.end()) {
            ++wheelWrites; wheelWritten[std::size_t(found-wheelOutputs.begin())]=true; return true;
        }
    }
    // Only the eight decoded JSR instructions and one MOVE.W D0,-(SP) may
    // write the stack. Check exact destination, bytes and one-time coverage.
    if(phase==Phase::producer && stackWrite.size
       && contains(address,size,stackWrite.address,stackWrite.address+stackWrite.size)) {
        for(unsigned byte=0;byte<size;++byte) {
            const unsigned index=address+byte-stackWrite.address;
            const unsigned expected=(stackWrite.value>>(8*(stackWrite.size-1-index)))&255;
            const unsigned actual=(value>>(8*(size-1-byte)))&255;
            if(expected!=actual || (stackWrite.mask&(1u<<index))) {
                failAccess("Wrong or repeated stack byte",address,size); return false;
            }
            stackWrite.mask|=1u<<index;
        }
        stackBytes+=size; return true;
    }
    failAccess("Write outside phase outputs or justified stack",address,size); return false;
}
void instructionHook(unsigned pc) {
    if(stackWrite.size && stackWrite.mask!=((1u<<stackWrite.size)-1))
        failAccess("Incomplete expected stack write",stackWrite.address,stackWrite.size);
    stackWrite={}; currentPc=pc;
    if(invalidAccess) { m68k_end_timeslice(); return; }
    if(pc==sentinel) { returned=true; m68k_end_timeslice(); return; }
    const bool allowed=phase==Phase::wheel ? pc>=wheelBegin && pc<wheelEnd
        : (pc>=trigBegin && pc<trigEnd) || (phase==Phase::producer && pc>=producerBegin && pc<producerEnd);
    const unsigned limit=phase==Phase::producer ? 1000u : phase==Phase::trig ? 60u : 29u;
    if(!allowed || (pc&1) || ++instructions>limit) {
        failAccess("Instruction outside phase ranges/budget",pc,2); m68k_end_timeslice(); return;
    }
    if(phase==Phase::producer && std::find(callSites.begin(),callSites.end(),pc)!=callSites.end()) {
        const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);
        const auto target=longword(pc+2);
        if(word(pc)!=0x4eb9 || (target!=trigBegin && target!=cosineEntry)
           || (sp!=stack && sp!=stack-2)) {
            failAccess("Unexpected nested call contract",pc,6); m68k_end_timeslice(); return;
        }
        stackWrite={sp-4,4,0,pc+6}; ++jsrCalls;
    } else if(phase==Phase::producer && pc==0x613ba) {
        if(word(pc)!=0x3f00 || m68k_get_reg(nullptr,M68K_REG_SP)!=stack) {
            failAccess("Unexpected temporary angle push",pc,2); m68k_end_timeslice(); return;
        }
        stackWrite={stack-2,2,0,m68k_get_reg(nullptr,M68K_REG_D0)&0xffff};
    }
}
void executeOriginal(Phase nextPhase,unsigned entry,std::uint16_t input=0) {
    executing=false; phase=nextPhase;
    putLong(0,stack); putLong(4,entry); putLong(stack,sentinel);
    putWord(sentinel,0x4e72); putWord(sentinel+2,0x2700);
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r)
        m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),nextRandom(cpuRandomState));
    m68k_set_reg(M68K_REG_SR,0x2700|(nextRandom(cpuRandomState)&31));
    m68k_set_reg(M68K_REG_SP,stack); m68k_set_reg(M68K_REG_PC,entry);
    if(nextPhase==Phase::trig)
        m68k_set_reg(M68K_REG_D0,(m68k_get_reg(nullptr,M68K_REG_D0)&0xffff0000)|input);
    instructions=matrixWrites=wheelWrites=stackBytes=jsrCalls=0;
    matrixWritten.fill(false); wheelWritten.fill(false); stackWrite={};
    invalidAccess=returned=false; accessFailure.clear(); currentPc=entry; executing=true;
    m68k_execute(nextPhase==Phase::producer ? 30000 : 3000);
    executing=false;
    if(invalidAccess || !returned || m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4) {
        std::ostringstream message;
        message<<"Oracle execution/access/return contract failed: "<<accessFailure
               <<" phase="<<int(nextPhase)<<" returned="<<returned<<" instructions="<<instructions;
        throw std::runtime_error(message.str());
    }
    if(nextPhase==Phase::producer
       && (matrixWrites!=55 || std::count(matrixWritten.begin(),matrixWritten.end(),true)!=30
           || stackBytes!=34 || jsrCalls!=8))
        throw std::runtime_error("Producer write-count/call-stack contract failed: matrix writes="
            +std::to_string(matrixWrites)+", stack bytes="+std::to_string(stackBytes)+", calls="+std::to_string(jsrCalls));
    if(nextPhase==Phase::wheel && (instructions!=29 || wheelWrites!=12
       || std::count(wheelWritten.begin(),wheelWritten.end(),true)!=6))
        throw std::runtime_error("Chained wheel instruction/write contract failed");
    instructionMaximum=std::max(instructionMaximum,instructions);
    if(nextPhase==Phase::producer) producerInstructionMaximum=std::max(producerInstructionMaximum,instructions);
    if(nextPhase==Phase::trig) trigInstructionMaximum=std::max(trigInstructionMaximum,instructions);
    totalInstructions+=instructions;
}
template<std::size_t N> void printWords(const std::array<std::uint16_t,N>& values) {
    std::cout<<'[';
    for(std::size_t i=0;i<N;++i) { if(i) std::cout<<','; std::cout<<unsigned(values[i]); }
    std::cout<<']';
}
template<std::size_t N> std::array<std::uint16_t,N> readWords(const std::array<unsigned,N>& addresses) {
    std::array<std::uint16_t,N> values{};
    for(std::size_t i=0;i<N;++i) values[i]=word(addresses[i]);
    return values;
}
MatrixWords readMatrix() {
    MatrixWords values{};
    for(std::size_t i=0;i<values.size();++i) values[i]=word(matrixAddress+unsigned(i*2));
    return values;
}
WheelWords nativeWheelWords(const scr::TransformMatrices& matrix) {
    const auto wheel=scr::calculateWheelCoordinates(matrix.wheelInputs());
    return {std::uint16_t(wheel.frontLeft.x),std::uint16_t(wheel.frontRight.x),std::uint16_t(wheel.rearCenter.x),
            std::uint16_t(wheel.frontLeft.y),std::uint16_t(wheel.frontRight.y),std::uint16_t(wheel.rearCenter.y)};
}
struct Replayed { MatrixWords matrix; WheelWords wheels; };
Replayed checkMatrix(const AngleWords& angles,const MatrixWords* suppliedOld=nullptr) {
    MatrixWords old{};
    if(suppliedOld) old=*suppliedOld;
    else for(auto& value:old) value=randomWord();
    for(std::size_t i=0;i<old.size();++i) putWord(matrixAddress+unsigned(i*2),old[i]);
    for(std::size_t i=0;i<angles.size();++i) putWord(angleAddresses[i],angles[i]);
    for(auto address:wheelOutputs) putWord(address,randomWord());
    executeOriginal(Phase::producer,producerBegin);
    const auto expected=readMatrix();
    scr::TransformMatrices actual; actual.words=old;
    scr::updateTransformMatrices({angles[0],angles[1],angles[2],angles[3]},trigTable,actual);
    if(expected!=actual.words) {
        std::cerr<<"Matrix mismatch at case "<<matrixCases<<" angles:";
        for(auto value:angles) std::cerr<<' '<<unsigned(value);
        for(std::size_t i=0;i<old.size();++i) if(expected[i]!=actual.words[i])
            std::cerr<<"\n offset "<<i*2<<" original="<<expected[i]<<" native="<<actual.words[i]<<" old="<<old[i];
        throw std::runtime_error("Full36-word matrix differential mismatch");
    }
    for(auto offset:holes) if(expected[offset/2]!=old[offset/2])
        throw std::runtime_error("Original failed to preserve a matrix hole");
    ++matrixCases;
    // The original wheel routine consumes the matrix that Musashi just wrote;
    // the native wheel routine consumes only the independently produced C++ one.
    executeOriginal(Phase::wheel,wheelBegin);
    const auto expectedWheel=readWords(wheelOutputs),actualWheel=nativeWheelWords(actual);
    if(expectedWheel!=actualWheel) throw std::runtime_error("Chained wheel differential mismatch at matrix case "+std::to_string(matrixCases));
    ++chainedWheelCases;
    return {actual.words,actualWheel};
}
void checkTrig(std::uint16_t angle,bool cosine) {
    executeOriginal(Phase::trig,cosine?cosineEntry:trigBegin,angle);
    const auto expected=std::uint16_t(m68k_get_reg(nullptr,M68K_REG_D0));
    const auto actual=cosine?scr::gameCosine(angle,trigTable):scr::gameSine(angle,trigTable);
    if(actual!=expected) {
        std::cerr<<"Trig mismatch angle="<<angle<<" cosine="<<cosine
                 <<" original="<<expected<<" native="<<actual;
        throw std::runtime_error("Trig differential mismatch");
    }
    ++trigCases;
}
bool matchesRange(const std::vector<std::uint8_t>& payload,const std::vector<std::uint8_t>& capture,unsigned begin,unsigned end) {
    return std::equal(payload.begin()+begin-loadAddress,payload.begin()+end-loadAddress,capture.begin()+begin);
}
}

extern "C" {
unsigned int m68k_read_memory_8(unsigned int address) {
    if(!permittedRead(address,1)) { failAccess("Invalid read",address,1); return 0; }
    return ram[address];
}
unsigned int m68k_read_memory_16(unsigned int address) {
    if(!permittedRead(address,2)) { failAccess("Invalid read",address,2); return 0; }
    return word(address);
}
unsigned int m68k_read_memory_32(unsigned int address) {
    if(!permittedRead(address,4)) { failAccess("Invalid read",address,4); return 0; }
    return longword(address);
}
void m68k_write_memory_8(unsigned int address,unsigned int) { failAccess("Byte writes are not part of these routines",address,1); }
void m68k_write_memory_16(unsigned int address,unsigned int value) {
    if(permittedWrite(address,2,value)) putWord(address,std::uint16_t(value));
}
void m68k_write_memory_32(unsigned int address,unsigned int value) {
    if(permittedWrite(address,4,value)) putLong(address,value);
}
}

int main(int argc,char** argv) try {
    if(argc<2 || (argc-2)%2) throw std::runtime_error("Usage: transform_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);
    if(payload.size()!=payloadSize) throw std::runtime_error("Expected original408540-byte recovered payload");
    std::copy(payload.begin(),payload.end(),ram.begin()+loadAddress);
    for(std::size_t i=0;i<trigTable.size();++i) trigTable[i]=word(tableAddress+unsigned(i*2));
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_set_instr_hook_callback(instructionHook);
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":"<<initialSeed
             <<",\"cpu_register_seed\":"<<initialCpuSeed<<",\"captures\":[";
    for(int argument=2;argument<argc;argument+=2) {
        const std::string label=argv[argument];
        if(label.empty() || label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[argument+1]);
        if(capture.size()!=ram.size()
           || !matchesRange(payload,capture,producerBegin,producerEnd)
           || !matchesRange(payload,capture,trigBegin,trigEnd)
           || !matchesRange(payload,capture,wheelBegin,wheelEnd)
           || !matchesRange(payload,capture,tableAddress,tableAddress+unsigned(trigTable.size()*2)))
            throw std::runtime_error("Capture size or original producer/trig/wheel/table identity mismatch");
        std::copy(capture.begin(),capture.end(),ram.begin());
        const auto angles=readWords(angleAddresses);
        const auto storedWheels=readWords(wheelOutputs);
        const auto storedMatrix=readMatrix();
        // Preserve captured holes for a meaningful comparison to stored state.
        // Computed fields are replayed from current angles and can legitimately
        // differ from the older stored matrix/wheels at this snapshot instant.
        const auto replay=checkMatrix(angles,&storedMatrix);
        if(argument>2) std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<"\",\"input_angles\":"; printWords(angles);
        std::cout<<",\"replayed_matrix_words\":"; printWords(replay.matrix);
        std::cout<<",\"stored_matrix_words\":"; printWords(storedMatrix);
        std::cout<<",\"stored_matrix_agrees\":"<<(storedMatrix==replay.matrix?"true":"false");
        std::cout<<",\"replayed_wheel_words\":"; printWords(replay.wheels);
        std::cout<<",\"stored_wheel_words\":"; printWords(storedWheels);
        std::cout<<",\"stored_wheel_agrees\":"<<(storedWheels==replay.wheels?"true":"false")<<'}';
        // Separately exercise these captured inputs with unrelated old words.
        checkMatrix(angles);
    }
    const auto captureReplayCases=matrixCases;
    ram.fill(0); std::copy(payload.begin(),payload.end(),ram.begin()+loadAddress);
    for(unsigned angle=0;angle<65536;++angle) {
        checkTrig(std::uint16_t(angle),false); checkTrig(std::uint16_t(angle),true);
    }
    constexpr AngleWords fixed{0x8123,0x3fe1,0xc041,0x7fdf};
    for(std::size_t field=0;field<4;++field) for(unsigned value=0;value<65536;++value) {
        auto angles=fixed; angles[field]=std::uint16_t(value); checkMatrix(angles);
    }
    const auto sweepCases=matrixCases-captureReplayCases;
    const auto beforeBoundary=matrixCases;
    // Cross-product of all quadrant choices, with coupled perturbations on both
    // sides of quadrant/64-unit table boundaries and wrapped yaw-heading deltas.
    constexpr std::array<int,15> perturb{-65,-64,-63,-33,-32,-31,-1,0,1,31,32,33,63,64,65};
    for(unsigned p=0;p<4;++p) for(unsigned y=0;y<4;++y)
    for(unsigned r=0;r<4;++r) for(unsigned h=0;h<4;++h) for(int delta:perturb)
        checkMatrix({std::uint16_t(int(p*0x4000)+delta),std::uint16_t(int(y*0x4000)-delta),
                     std::uint16_t(int(r*0x4000)+2*delta),std::uint16_t(int(h*0x4000)+delta)});
    // Every table-cell boundary throughout the full turn, plus adjacent words,
    // enters each matrix angle and a combined multi-angle scenario.
    for(unsigned cell=0;cell<1024;++cell) for(int delta:{-1,0,1}) {
        const auto value=std::uint16_t(int(cell*64)+delta);
        for(std::size_t field=0;field<4;++field) {
            auto angles=fixed; angles[field]=value; checkMatrix(angles);
        }
        checkMatrix({value,std::uint16_t(value+0x4000),std::uint16_t(~value),std::uint16_t(value-0x8000)});
    }
    const auto boundaryCases=matrixCases-beforeBoundary;
    constexpr unsigned randomCases=100000;
    for(unsigned i=0;i<randomCases;++i) {
        AngleWords angles{}; for(auto& angle:angles) angle=randomWord(); checkMatrix(angles);
    }
    std::cout<<"],\"trig_cases\":"<<trigCases<<",\"matrix_cases\":"<<matrixCases
             <<",\"chained_wheel_cases\":"<<chainedWheelCases
             <<",\"capture_replay_cases\":"<<captureReplayCases
             <<",\"single_angle_sweep_cases\":"<<sweepCases<<",\"boundary_cases\":"<<boundaryCases
             <<",\"random_cases\":"<<randomCases<<",\"mismatches\":0"
             <<",\"all36_matrix_words_compared\":true,\"six_holes_preserved\":true"
             <<",\"matrix_written_slots\":30,\"matrix_word_writes_per_case\":55"
             <<",\"nested_calls_per_matrix_case\":8,\"stack_bytes_written_per_matrix_case\":34"
             <<",\"writes_confined_to_phase_outputs_and_verified_stack\":true"
             <<",\"reads_confined_to_phase_inputs_code_table_and_stack\":true"
             <<",\"max_instructions_per_matrix_case\":"<<producerInstructionMaximum
             <<",\"max_instructions_per_trig_case\":"<<trigInstructionMaximum
             <<",\"max_instructions_any_case\":"<<instructionMaximum
             <<",\"wheel_instructions_per_case\":29,\"total_instructions\":"<<totalInstructions
             <<",\"final_input_prng_state\":"<<randomState<<",\"final_cpu_prng_state\":"<<cpuRandomState<<"}\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<"\n"<<error.what()<<'\n'; return 1; }
