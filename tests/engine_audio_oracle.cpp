#include "engine_audio_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
namespace f=scr::test::engineAudio;namespace audio=scr::test::audio;
std::array<std::uint8_t,0x80000> ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
std::vector<scr::AudioRegisterWrite> writes;
unsigned phase=0,pc=0,instructions=0,maximumInstructions=0;
std::uint16_t dmaReadback{};
bool invalid=false,returned=false,executing=false;
std::string failure;
std::uint64_t cases=0,totalInstructions=0,generationCases=0,vblankCases=0,interruptCases=0;
std::uint64_t exitCases=0,startupCases=0,completionCases=0;
std::array<std::uint64_t,8> selectedLevels{};
std::uint32_t rng=0x454e474e;
std::uint32_t random32(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
bool in(unsigned a,unsigned first,unsigned end){return a>=first&&a<end;}
void fail(const char* why,unsigned a){invalid=true;if(failure.empty())failure=std::string(why)+" address="+std::to_string(a)+" pc="+std::to_string(pc);}
bool code(unsigned a){return in(a,f::generationEntry,f::generationEnd)||in(a,f::interruptEntry,f::interruptEnd)||in(a,f::vblankEntry,f::vblankEnd);}
void hook(unsigned a){pc=a;if(invalid){m68k_end_timeslice();return;}
    if(a==sentinel){returned=true;m68k_end_timeslice();return;}
    if((a&1)||!code(a)||++instructions>20000){fail("Unexpected PC/budget",a);m68k_end_timeslice();}}
std::uint8_t readByte(unsigned a){
    if(a<ram.size())return ram[a];if(in(a,stackBase,stackTop))return stack[a-stackBase];
    if(in(a,sentinel,sentinel+4))return (a&1)?0x71:0x4e;
    fail("Unexpected read",a);return 0;
}
bool allowedWrite(unsigned a){
    if(in(a,stackBase,stackTop))return true;
    if(phase==0)return in(a,0xf474,0xf4b4)||in(a,0x1aa4a,0x1b6ca)||in(a,0x1b73a,0x1b742);
    if(phase==1)return in(a,0x1bc34,0x1bc36)||in(a,f::periodAddress,f::periodAddress+2);
    for(unsigned i=1;i<4;++i)if(in(a,audio::channels+4*i,audio::channels+4*i+2))return true;
    return false;
}
void writeByte(unsigned a,std::uint8_t v){
    if(!executing||!allowedWrite(a)){fail("Unexpected write",a);return;}
    if(in(a,stackBase,stackTop)){stack[a-stackBase]=v;return;}
    if(a>=ram.size()){fail("Out-of-range write",a);return;}ram[a]=v;
}
void run(unsigned entry,unsigned activePhase){
    phase=activePhase;executing=false;m68k_pulse_reset();
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),random32());
    m68k_set_reg(M68K_REG_SR,0x2700|(random32()&31));
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_PC,entry);
    invalid=returned=false;failure.clear();instructions=0;writes.clear();executing=true;m68k_execute(10000000);executing=false;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stackTop)
        throw std::runtime_error("Audio CPU execution failure "+failure+" case="+std::to_string(cases));
    maximumInstructions=std::max(maximumInstructions,instructions);totalInstructions+=instructions;
}
void compareWrites(const std::vector<scr::AudioRegisterWrite>& expected){
    if(writes.size()!=expected.size())throw std::runtime_error("Audio bus write count mismatch case="+std::to_string(cases));
    for(unsigned i=0;i<writes.size();++i)if(writes[i].address!=expected[i].address||writes[i].value!=expected[i].value)
        throw std::runtime_error("Audio ordered bus mismatch case="+std::to_string(cases)+" write="+std::to_string(i));
}
scr::EngineAudioData checkGeneration(){
    // Poison derived memory and metadata so accidental retained capture bytes
    // cannot satisfy the comparison.
    std::fill(ram.begin()+0x1aa4a,ram.begin()+0x1b6ca,0xa5);
    std::fill(ram.begin()+0xf474,ram.begin()+0xf4b4,0x5a);
    const auto expected=scr::generateEngineSamples(ram.data()+scr::originalEngineSampleAddress,scr::originalEngineSampleBytes);
    run(f::generationEntry,0);compareWrites({});
    const auto actual=f::readData(ram.data());
    for(unsigned i=0;i<actual.levels.size();++i)if(actual.levels[i].address!=expected.levels[i].address||actual.levels[i].wordLength!=expected.levels[i].wordLength)
        throw std::runtime_error("Engine sample level metadata mismatch");
    if(!std::equal(expected.generatedSamples.begin(),expected.generatedSamples.end(),ram.begin()+scr::generatedEngineSampleAddress))
        throw std::runtime_error("Engine decimated sample bytes mismatch");
    if(audio::longword(ram.data(),0x1b73a)!=scr::originalEngineSampleAddress||audio::longword(ram.data(),0x1b73e)!=scr::originalEngineSampleBytes)
        throw std::runtime_error("Engine initial sample descriptor mismatch");
    for(unsigned i=scr::generatedEngineSampleAddress+unsigned(expected.generatedSamples.size());i<0x1b6ca;++i)
        if(ram[i]!=0xa5)throw std::runtime_error("Engine generator changed unwritten work area");
    ++cases;++generationCases;return expected;
}
void checkVblank(const scr::EngineAudioData& data,const scr::EngineAudioInputs& inputs,scr::EngineAudioState state){
    f::writeData(ram.data(),data);f::writeInputs(ram.data(),inputs);f::writeState(ram.data(),state);
    const auto expected=scr::updateEngineAudio(data,inputs,state);
    run(f::vblankEntry,1);compareWrites(expected);
    const auto actual=f::readState(ram.data());
    if(actual.pitchAccumulator!=state.pitchAccumulator||actual.savedPeriod!=state.savedPeriod)
        throw std::runtime_error("Engine VBlank state mismatch case="+std::to_string(cases));
    if(expected.size()==3)++exitCases;
    else for(unsigned level=0;level<data.levels.size();++level)
        if(expected[1].value==std::uint16_t(data.levels[level].address>>16)&&expected[2].value==std::uint16_t(data.levels[level].address)){++selectedLevels[level];break;}
    ++cases;++vblankCases;
}
void checkInterrupt(unsigned channel,std::uint16_t dma,const std::array<std::uint16_t,4>& masks,scr::AudioPlaybackState state,std::uint16_t period){
    auto data=audio::readData(ram.data());data.channelMasks=masks;audio::writeData(ram.data(),data);audio::writeState(ram.data(),state);
    audio::putWord(ram.data(),f::periodAddress,period);dmaReadback=dma;
    const auto expected=scr::completeAudioChannel(channel,dma,masks,state,period);
    const unsigned entries[4]={0xf2cc,0xf2d8,0xf2e4,0xf2f0};run(entries[channel],2);compareWrites(expected);
    const auto actual=audio::readState(ram.data());
    if(actual.completedInterrupts!=state.completedInterrupts||actual.dmaEnableGuard!=state.dmaEnableGuard)
        throw std::runtime_error("Audio interrupt state mismatch case="+std::to_string(cases));
    if(channel&&(dma&masks[channel])){if(state.completedInterrupts[channel]==1)++startupCases;if(expected.size()==2)++completionCases;}
    ++cases;++interruptCases;
}
std::vector<std::uint8_t> readFile(const char* path){std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read fixture");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return readByte(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1){fail("Odd word read",a);return 0;}if(a==0xdff002)return dmaReadback;return (unsigned(readByte(a))<<8)|readByte(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){writeByte(a,std::uint8_t(v));}
void m68k_write_memory_16(unsigned a,unsigned v){
    if(a&1){fail("Odd word write",a);return;}
    if(in(a,0xdff000,0xdff200)){writes.push_back({a,std::uint16_t(v)});return;}
    writeByte(a,std::uint8_t(v>>8));writeByte(a+1,std::uint8_t(v));
}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc<2||(argc-2)%2)throw std::runtime_error("Usage: engine_audio_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Payload size mismatch");
    std::copy(payload.begin(),payload.end(),ram.begin()+0xe700);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    auto data=checkGeneration();
    for(int arg=2;arg<argc;arg+=2){
        const auto capture=readFile(argv[arg+1]);if(capture.size()!=ram.size())throw std::runtime_error("Capture size mismatch");
        for(const auto range:std::array<std::array<unsigned,2>,3>{{{f::generationEntry,f::generationEnd},{f::interruptEntry,f::interruptEnd},{f::vblankEntry,f::vblankEnd}}})
            if(!std::equal(payload.begin()+range[0]-0xe700,payload.begin()+range[1]-0xe700,capture.begin()+range[0]))throw std::runtime_error("Captured audio code mismatch");
        std::copy(capture.begin(),capture.end(),ram.begin());
        const auto capturedData=f::readData(ram.data());const auto capturedInputs=f::readInputs(ram.data());const auto capturedState=f::readState(ram.data());
        checkVblank(capturedData,capturedInputs,capturedState);
        const auto sampleData=audio::readData(ram.data());const auto playback=audio::readState(ram.data());
        for(unsigned channel=0;channel<4;++channel)checkInterrupt(channel,0x20f,sampleData.channelMasks,playback,capturedState.savedPeriod);
        data=checkGeneration();
    }
    for(unsigned variant=0;variant<24;++variant){
        for(unsigned i=0;i<scr::originalEngineSampleBytes;++i)ram[scr::originalEngineSampleAddress+i]=std::uint8_t(variant<8?(variant&1?i:1u<<(variant/2)):random32());
        data=checkGeneration();
    }
    for(unsigned value=0;value<65536;++value){
        scr::EngineAudioInputs inputs{0,0x820f,std::uint8_t(value),0};
        checkVblank(data,inputs,{std::uint16_t(value),std::uint16_t(~value)});
        inputs.pitchDelta=std::uint16_t(value);inputs.gameExit=std::uint8_t(value&1);
        for(auto accumulator:{0u,0x7fffu,0xffffu})checkVblank(data,inputs,{std::uint16_t(accumulator),std::uint16_t(value)});
    }
    const std::array<std::uint16_t,4> masks{{1,2,4,8}};
    for(unsigned value=0;value<65536;++value){
        scr::AudioPlaybackState state;state.completedInterrupts.fill(std::uint16_t(value));state.dmaEnableGuard=std::uint16_t(value);
        checkInterrupt(0,0,masks,state,std::uint16_t(value));
        for(unsigned channel=1;channel<4;++channel)checkInterrupt(channel,0x20f,masks,state,std::uint16_t(value));
        checkInterrupt(1,std::uint16_t(value),masks,state,0x99);
    }
    for(unsigned n=0;n<10000;++n){
        scr::EngineAudioInputs inputs{std::uint16_t(random32()),std::uint16_t(random32()),std::uint8_t(random32()),std::uint8_t(random32())};
        checkVblank(data,inputs,{std::uint16_t(random32()),std::uint16_t(random32())});
        std::array<std::uint16_t,4> arbitraryMasks{};for(auto& mask:arbitraryMasks)mask=std::uint16_t(random32());
        scr::AudioPlaybackState state;for(auto& count:state.completedInterrupts)count=std::uint16_t(random32());state.dmaEnableGuard=std::uint16_t(random32());
        checkInterrupt(random32()%4,std::uint16_t(random32()),arbitraryMasks,state,std::uint16_t(random32()));
    }
    // Continuous six-VBlank gameplay cadence proves pitch accumulator carry
    // across a changing throttle/deceleration sequence without touching RAM.
    scr::EngineAudioState sequenceState{};
    for(unsigned tick=0;tick<1463;++tick){
        const scr::EngineAudioInputs inputs{std::uint16_t(tick%140<100?0x100:0xff00),0x820f,0,0};
        for(unsigned vblank=0;vblank<6;++vblank){checkVblank(data,inputs,sequenceState);(void)scr::updateEngineAudio(data,inputs,sequenceState);}
    }
    unsigned rejected=0;try{(void)scr::generateEngineSamples(nullptr,3172);}catch(const std::invalid_argument&){++rejected;}
    try{(void)scr::generateEngineSamples(ram.data(),3171);}catch(const std::invalid_argument&){++rejected;}
    scr::AudioPlaybackState state;try{(void)scr::completeAudioChannel(4,0,masks,state,0);}catch(const std::out_of_range&){++rejected;}
    if(rejected!=3)throw std::runtime_error("Invalid audio input accepted");
    std::cout<<"{\"total_cases\":"<<cases<<",\"mismatches\":0,\"generation_cases\":"<<generationCases<<",\"vblank_cases\":"<<vblankCases
        <<",\"interrupt_cases\":"<<interruptCases<<",\"exit_cases\":"<<exitCases<<",\"startup_interrupt_cases\":"<<startupCases
        <<",\"completion_interrupt_cases\":"<<completionCases<<",\"generated_bytes\":"<<data.generatedSamples.size()<<",\"selected_levels\":[";
    for(unsigned i=0;i<selectedLevels.size();++i){if(i)std::cout<<',';std::cout<<selectedLevels[i];}
    std::cout<<"],\"maximum_instructions\":"<<maximumInstructions<<",\"total_instructions\":"<<totalInstructions
        <<",\"invalid_inputs_rejected\":3,\"ordered_paula_writes_compared\":true,\"waveform_timing_verified\":false}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
