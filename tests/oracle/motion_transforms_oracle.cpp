// Independent original-byte MC68000 execution for every motion-transform stage
// and shared multiplication helper. The native library has no CPU dependency.
#include "motion_transforms_fixture.h"
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
namespace fixture=scr::test::motion;
constexpr unsigned base=0xe700,payloadSize=408540,stack=0x7f000,sentinel=0x7e000;
constexpr unsigned negativeEntry=0x61338,positiveEntry=0x61340,multiplyEntry=0x61344,helperEnd=0x6135a;
constexpr std::uint32_t initialSeed=0x53435238,initialCpuSeed=0x36383036;
constexpr std::array<const char*,4> stageNames{"world","view","secondary","tertiary"};
constexpr std::array<unsigned,4> requiredStackBytes{24,12,36,20},requiredOutputBytes{4,6,6,6};
constexpr std::array<std::array<unsigned,3>,4> stageOutputs{{{0x1bd2c,0x1bd30,0},{0x1bd0e,0x1bd10,0x1bd12},
    {0x1bcf6,0x1bcf8,0x1bcfa},{0x1bd3a,0x1bd3c,0x1bd3e}}};
constexpr std::array<std::uint16_t,9> edges{0,1,0xffff,0x7fff,0x8000,0x8001,0x3fff,0x4000,0x7ffe};
std::array<std::uint8_t,0x80000> ram{},expectedRam{};
std::array<bool,0x80000> readable{},writable{};
std::uint32_t randomState=initialSeed,cpuState=initialCpuSeed;
std::uint64_t helperCases=0,totalInstructions=0,totalStageCases=0;
std::array<std::uint64_t,4> stageCases{};
std::array<std::uint64_t,3> helperEntryCases{};
std::array<std::uint64_t,36> selectedCoefficients{};
unsigned currentPc=0,instructions=0,outputBytes=0,stackBytes=0,minimumInstructions=1000,maximumInstructions=0;
int activeStage=-1;
bool executing=false,invalidAccess=false,returned=false;
std::string failure;

std::uint32_t random32(std::uint32_t& state) {state^=state<<13;state^=state>>17;state^=state<<5;return state;}
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
std::uint32_t readLong(unsigned a) {return (std::uint32_t(fixture::readWord(ram.data(),a))<<16)|fixture::readWord(ram.data(),a+2);}
void writeLong(unsigned a,std::uint32_t v) {fixture::writeWord(ram.data(),a,std::uint16_t(v>>16));fixture::writeWord(ram.data(),a+2,std::uint16_t(v));}
bool contains(unsigned a,unsigned n,unsigned start,unsigned end) {return a>=start && a<end && n<=end-a;}
void fail(const char* description,unsigned address,unsigned size) {
    invalidAccess=true;if(!failure.empty())return;
    std::ostringstream s;s<<description<<" address=0x"<<std::hex<<address<<" size="<<std::dec<<size<<" pc=0x"<<std::hex<<currentPc;failure=s.str();
}
void allow(unsigned a,unsigned n) {for(unsigned i=0;i<n;++i)readable[a+i]=true;}
void initializePermissions() {
    allow(negativeEntry,helperEnd-negativeEntry+4);allow(stack-4,8);allow(sentinel,8);
    for(unsigned stage=0;stage<4;++stage)allow(fixture::entries[stage],fixture::ends[stage]-fixture::entries[stage]+4);
    allow(fixture::coefficientAddress,72);allow(fixture::selectorAddress,22);
    for(auto a:fixture::inputWords)allow(a,2);
    for(auto a:fixture::outputWords)allow(a,2);
    allow(0x1bd2e,2);
}
bool permittedRead(unsigned a,unsigned n) {
    if(!contains(a,n,0,unsigned(ram.size())) || (n>1 && (a&1)))return false;
    if(!executing)return true;
    for(unsigned j=0;j<n;++j)if(!readable[a+j])return false;
    return true;
}
bool permittedWrite(unsigned a,unsigned n) {
    if(!executing || !contains(a,n,0,unsigned(ram.size())) || (n>1 && (a&1))) {fail("Invalid write",a,n);return false;}
    if(contains(a,n,stack-4,stack)) {
        if(activeStage<0 || (n!=2 && n!=4) || fixture::readWord(ram.data(),currentPc)!=0x4eb9) {fail("Unexpected call-stack write",a,n);return false;}
        stackBytes+=n;return true;
    }
    if(n!=2 || activeStage<0) {fail("Non-word or helper state write",a,n);return false;}
    for(unsigned j=0;j<n;++j)if(!writable[a+j]) {fail("Write outside active-stage outputs",a,n);return false;}
    outputBytes+=n;return true;
}
void instructionHook(unsigned pc) {
    currentPc=pc;if(invalidAccess){m68k_end_timeslice();return;}
    if(pc==sentinel){returned=true;m68k_end_timeslice();return;}
    const bool inHelper=contains(pc,2,negativeEntry,helperEnd);
    const bool inStage=activeStage>=0 && contains(pc,2,fixture::entries[unsigned(activeStage)],fixture::ends[unsigned(activeStage)]);
    if((!inHelper && !inStage) || (pc&1) || ++instructions>300) {fail("Instruction outside stage/helper or budget",pc,2);m68k_end_timeslice();return;}
    if(pc==multiplyEntry) {
        const auto index=m68k_get_reg(nullptr,M68K_REG_D1)&0xffff;
        if(index>=36){fail("Invalid coefficient selector",index,2);return;}
        ++selectedCoefficients[index];
    }
}
std::uint32_t original(unsigned entry,std::uint16_t input=0,unsigned coefficientIndex=0) {
    writeLong(0,stack);writeLong(4,entry);writeLong(stack,sentinel);
    fixture::writeWord(ram.data(),sentinel,0x4e72);fixture::writeWord(ram.data(),sentinel+2,0x2700);
    executing=false;m68k_pulse_reset();
    std::uint32_t originalD1=0;
    for(unsigned r=0;r<15;++r) {
        auto v=random32(cpuState);
        if(r==0 && activeStage<0)v=(v&0xffff0000)|input;
        if(r==1) {
            if(activeStage<0)v=(v&0xffff0000)|coefficientIndex;
            else if(activeStage!=1)v&=0xffff00ff; // view sets its own entire word
            originalD1=v;
        }
        m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),v);
    }
    m68k_set_reg(M68K_REG_SR,0x2700|(random32(cpuState)&31));m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);
    instructions=outputBytes=stackBytes=0;invalidAccess=returned=false;failure.clear();currentPc=entry;
    executing=true;m68k_execute(20000);executing=false;
    const unsigned requiredBytes=activeStage<0 ? 0 : requiredOutputBytes[unsigned(activeStage)];
    const unsigned requiredStack=activeStage<0 ? 0 : requiredStackBytes[unsigned(activeStage)];
    if(invalidAccess || !returned || outputBytes!=requiredBytes || stackBytes!=requiredStack
       || m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4) {
        std::ostringstream s;s<<"Motion oracle contract failure at helper="<<helperCases<<" stage="<<totalStageCases
            <<" active="<<activeStage<<": "<<failure<<" returned="<<returned<<" outputBytes="<<outputBytes<<" stackBytes="<<stackBytes;
        throw std::runtime_error(s.str());
    }
    const auto d1=m68k_get_reg(nullptr,M68K_REG_D1);
    if((activeStage<0 && d1!=originalD1) || (activeStage>=0 && (d1&0xff00)))
        throw std::runtime_error("Original coefficient-index register convention not preserved");
    totalInstructions+=instructions;minimumInstructions=std::min(minimumInstructions,instructions);maximumInstructions=std::max(maximumInstructions,instructions);
    return m68k_get_reg(nullptr,M68K_REG_D0);
}
void checkHelper(unsigned helper,std::uint16_t value,std::uint16_t coefficient,unsigned index) {
    activeStage=-1;fixture::writeWord(ram.data(),fixture::coefficientAddress+index*2,coefficient);
    const unsigned entry=helper==0 ? multiplyEntry : helper==1 ? negativeEntry : positiveEntry;
    const auto originalWord=std::uint16_t(original(entry,value,index));
    const auto native=scr::multiplyMatrixWord(helper==0 ? value : helper==1 ? 0xfec3 : 0x13d,coefficient);
    if(originalWord!=native) {
        std::ostringstream s;s<<"Matrix product mismatch helper="<<helper<<" case="<<helperCases<<" value="<<value
            <<" coefficient="<<coefficient<<" original="<<originalWord<<" native="<<native;throw std::runtime_error(s.str());
    }
    ++helperCases;++helperEntryCases[helper];
}
void nativeStage(unsigned stage,const scr::MotionTransformInputs& input,const scr::TransformMatrices& matrix,
                 const scr::MotionTransformSelectors& selectors,scr::MotionTransformState& state) {
    switch(stage) {
    case 0:scr::transformWorldCoordinates(input,matrix,selectors,state);break;
    case 1:scr::calculateViewAngles(matrix,state);break;
    case 2:scr::calculateSecondaryCoordinates(input,matrix,selectors,state);break;
    case 3:scr::calculateTertiaryCoordinates(input,matrix,selectors,state);break;
    default:throw std::runtime_error("Invalid stage");
    }
}
scr::MotionTransformState checkStage(unsigned stage,const scr::MotionTransformInputs& input,const scr::TransformMatrices& matrix,
                                     const scr::MotionTransformSelectors& selectors,const scr::MotionTransformState& initial) {
    fixture::writeInputs(ram.data(),input);fixture::writeMatrices(ram.data(),matrix);fixture::writeSelectors(ram.data(),selectors);
    fixture::writeState(ram.data(),initial);
    auto expected=initial;nativeStage(stage,input,matrix,selectors,expected);fixture::writeState(expectedRam.data(),expected);
    for(auto address:fixture::outputWords){writable[address]=writable[address+1]=false;}
    for(auto address:stageOutputs[stage])if(address)writable[address]=writable[address+1]=true;
    activeStage=int(stage);(void)original(fixture::entries[stage]);
    for(const auto& range:fixture::observedRanges)for(unsigned a=range[0];a<range[1];++a)if(ram[a]!=expectedRam[a]) {
        std::ostringstream s;s<<"Motion "<<stageNames[stage]<<" mismatch case="<<totalStageCases<<" address=0x"<<std::hex<<a
            <<" original="<<unsigned(ram[a])<<" native="<<unsigned(expectedRam[a]);throw std::runtime_error(s.str());
    }
    ++stageCases[stage];++totalStageCases;return expected;
}
scr::MotionTransformInputs randomInputs() {
    scr::MotionTransformInputs i;
    for(auto* fields:{&i.worldSpeed,&i.adjustedAngles,&i.rotationSpeed})for(auto& v:*fields)v=std::uint16_t(random32(randomState));
    return i;
}
scr::TransformMatrices randomMatrices() {scr::TransformMatrices m;for(auto& v:m.words)v=std::uint16_t(random32(randomState));return m;}
scr::MotionTransformState randomOutputs() {
    scr::MotionTransformState s;
    for(auto* fields:{&s.localVelocity,&s.worldAcceleration,&s.transformedAngularVelocity})for(auto& v:*fields)v=std::uint16_t(random32(randomState));
    s.baseTargetRoll=std::uint16_t(random32(randomState));s.baseRollAngle=std::uint16_t(random32(randomState));s.additionalYawOffset=std::uint16_t(random32(randomState));return s;
}
void printWords(const std::array<std::uint16_t,3>& a) {std::cout<<'['<<a[0]<<','<<a[1]<<','<<a[2]<<']';}
std::array<std::uint16_t,3> stageWords(unsigned stage,const scr::MotionTransformState& s) {
    switch(stage){case 0:return s.localVelocity;case 1:return {s.baseTargetRoll,s.baseRollAngle,s.additionalYawOffset};case 2:return s.worldAcceleration;default:return s.transformedAngularVelocity;}
}
} // namespace

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) {if(!permittedRead(a,1)){fail("Invalid read",a,1);return 0;}return ram[a];}
unsigned int m68k_read_memory_16(unsigned int a) {if(!permittedRead(a,2)){fail("Invalid read",a,2);return 0;}return fixture::readWord(ram.data(),a);}
unsigned int m68k_read_memory_32(unsigned int a) {if(!permittedRead(a,4)){fail("Invalid read",a,4);return 0;}return readLong(a);}
void m68k_write_memory_8(unsigned int a,unsigned int v) {if(permittedWrite(a,1))ram[a]=std::uint8_t(v);}
void m68k_write_memory_16(unsigned int a,unsigned int v) {if(permittedWrite(a,2))fixture::writeWord(ram.data(),a,std::uint16_t(v));}
void m68k_write_memory_32(unsigned int a,unsigned int v) {if(permittedWrite(a,4))writeLong(a,v);}
}

int main(int argc,char** argv) try {
    if(argc<2 || (argc-2)%2)throw std::runtime_error("Usage: motion_transforms_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);if(payload.size()!=payloadSize)throw std::runtime_error("Expected original 408540-byte payload");
    std::copy(payload.begin(),payload.end(),ram.begin()+base);initializePermissions();
    const auto originalSelectors=fixture::readSelectors(ram.data());
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(instructionHook);
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":"<<initialSeed<<",\"cpu_register_seed\":"<<initialCpuSeed<<",\"captures\":[";
    for(int argument=2;argument<argc;argument+=2) {
        const std::string label=argv[argument];
        if(label.empty() || label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)throw std::runtime_error("Invalid capture label");
        const auto bytes=readFile(argv[argument+1]);if(bytes.size()!=ram.size())throw std::runtime_error("Capture must contain 512KiB RAM");
        if(!std::equal(payload.begin()+negativeEntry-base,payload.begin()+helperEnd-base,bytes.begin()+negativeEntry)
           || !std::equal(payload.begin()+fixture::entries.front()-base,payload.begin()+fixture::ends.back()-base,bytes.begin()+fixture::entries.front())
           || !std::equal(originalSelectors.begin(),originalSelectors.end(),bytes.begin()+fixture::selectorAddress))
            throw std::runtime_error("Capture original code/selectors differ from payload");
        std::copy(bytes.begin(),bytes.end(),ram.begin());const auto in=fixture::readInputs(ram.data());const auto matrix=fixture::readMatrices(ram.data());const auto stored=fixture::readState(ram.data());
        if(argument>2)std::cout<<',';std::cout<<"{\"label\":\""<<label<<"\",\"stages\":[";
        for(unsigned stage=0;stage<4;++stage) {
            const auto result=checkStage(stage,in,matrix,originalSelectors,stored);
            if(stage)std::cout<<',';std::cout<<"{\"name\":\""<<stageNames[stage]<<"\",\"stored_words\":";printWords(stageWords(stage,stored));
            std::cout<<",\"replayed_words\":";printWords(stageWords(stage,result));
            std::cout<<",\"stored_snapshot_agrees\":"<<(stageWords(stage,stored)==stageWords(stage,result)?"true":"false")<<'}';
        }
        std::cout<<"]}";
    }
    const auto captureCases=totalStageCases;
    // Every input word with nine boundary coefficients. Signed extrema include
    // -32768 * -32768, whose doubled product wraps the Q15 word to 0x8000.
    for(auto coefficient:edges)for(unsigned value=0;value<65536;++value)
        checkHelper(0,std::uint16_t(value),coefficient,value%36);
    const auto helperSweepCases=helperCases;
    for(unsigned value=0;value<65536;++value)for(unsigned helper:{1u,2u})
        checkHelper(helper,std::uint16_t(random32(randomState)),std::uint16_t(value),value%36);
    constexpr unsigned randomHelperCases=100000;
    for(unsigned i=0;i<randomHelperCases;++i)checkHelper(0,std::uint16_t(random32(randomState)),std::uint16_t(random32(randomState)),random32(randomState)%36);

    // Each scalar input of each stage receives every word pattern. Other
    // inputs/coefficient words remain nontrivial and selector table is original.
    const auto beforeSweep=totalStageCases;
    for(unsigned stage=0;stage<4;++stage)for(unsigned field=0;field<3;++field) {
        auto in=randomInputs();auto matrix=randomMatrices();const auto state=randomOutputs();
        for(unsigned value=0;value<65536;++value) {
            if(stage==0)in.worldSpeed[field]=std::uint16_t(value);
            else if(stage==1)matrix.words[std::array<unsigned,3>{14,15,4}[field]]=std::uint16_t(value);
            else if(stage==2)in.adjustedAngles[field]=std::uint16_t(value);
            else in.rotationSpeed[field]=std::uint16_t(value);
            (void)checkStage(stage,in,matrix,originalSelectors,state);
        }
    }
    const auto stageSweepCases=totalStageCases-beforeSweep;
    const auto beforeBoundaries=totalStageCases;
    for(auto a:edges)for(auto b:edges)for(auto c:edges)for(unsigned pattern=0;pattern<3;++pattern) {
        auto in=randomInputs();auto matrix=randomMatrices();const auto state=randomOutputs();
        in.worldSpeed=in.adjustedAngles=in.rotationSpeed={a,b,c};
        for(unsigned i=0;i<36;++i)matrix.words[i]=std::array<std::uint16_t,3>{a,b,c}[(i+pattern)%3];
        for(unsigned stage=0;stage<4;++stage)(void)checkStage(stage,in,matrix,originalSelectors,state);
    }
    const auto boundaryCases=totalStageCases-beforeBoundaries;
    const auto beforeSelectors=totalStageCases;
    for(unsigned slot=0;slot<22;++slot)for(unsigned index=0;index<36;++index) {
        auto selectors=originalSelectors;selectors[slot]=std::uint8_t(index);
        const auto in=randomInputs();const auto matrix=randomMatrices();const auto state=randomOutputs();
        for(unsigned stage=0;stage<4;++stage)(void)checkStage(stage,in,matrix,selectors,state);
    }
    const auto selectorCases=totalStageCases-beforeSelectors;
    constexpr unsigned randomSets=100000;
    for(unsigned i=0;i<randomSets;++i) {
        const auto in=randomInputs();const auto matrix=randomMatrices();const auto state=randomOutputs();auto selectors=originalSelectors;
        if(i&1)for(auto& selector:selectors)selector=std::uint8_t(random32(randomState)%36);
        for(unsigned stage=0;stage<4;++stage)(void)checkStage(stage,in,matrix,selectors,state);
    }
    unsigned rejectedSelectors=0;
    for(unsigned stage:{0u,2u,3u})for(unsigned index=36;index<256;++index) {
        auto selectors=originalSelectors;selectors[stage==0 ? 0 : stage==2 ? 9 : 18]=std::uint8_t(index);
        auto state=randomOutputs();bool rejected=false;
        try{nativeStage(stage,randomInputs(),randomMatrices(),selectors,state);}catch(const std::out_of_range&){rejected=true;++rejectedSelectors;}
        if(!rejected)throw std::runtime_error("Out-of-block coefficient selector accepted");
    }
    if(std::find(selectedCoefficients.begin(),selectedCoefficients.end(),std::uint64_t(0))!=selectedCoefficients.end())throw std::runtime_error("Uncovered matrix coefficient index");
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"helper_input_word_sweep_cases\":"<<helperSweepCases
             <<",\"signed_constant_coefficient_sweep_cases\":131072,\"random_helper_cases\":"<<randomHelperCases
             <<",\"helper_cases\":"<<helperCases<<",\"stage_word_sweep_cases\":"<<stageSweepCases
             <<",\"boundary_cases\":"<<boundaryCases<<",\"selector_cases\":"<<selectorCases<<",\"random_stage_cases\":"<<randomSets*4
             <<",\"stage_cases\":"<<totalStageCases<<",\"rejected_invalid_selectors\":"<<rejectedSelectors
             <<",\"total_cases\":"<<helperCases+totalStageCases<<",\"mismatches\":0,\"stage_counts\":{";
    for(unsigned stage=0;stage<4;++stage){if(stage)std::cout<<',';std::cout<<'"'<<stageNames[stage]<<"\":"<<stageCases[stage];}
    std::cout<<"},\"helper_entry_counts\":["<<helperEntryCases[0]<<','<<helperEntryCases[1]<<','<<helperEntryCases[2]
             <<"],\"all_36_coefficient_slots_covered\":true,\"reads_confined_to_documented_state_code_tables_and_stack\":true"
             <<",\"writes_confined_to_active_stage_outputs_and_call_stack\":true,\"local_velocity_y_retained\":true"
             <<",\"original_d1_word_index_contract_preserved\":true,\"maximum_stack_depth_bytes\":4"
             <<",\"minimum_instructions\":"<<minimumInstructions<<",\"maximum_instructions\":"<<maximumInstructions
             <<",\"total_instructions\":"<<totalInstructions<<",\"final_input_prng_state\":"<<randomState<<",\"final_cpu_prng_state\":"<<cpuState<<"}\n";
    return 0;
} catch(const std::exception& e){std::cerr<<"\n"<<e.what()<<'\n';return 1;}
