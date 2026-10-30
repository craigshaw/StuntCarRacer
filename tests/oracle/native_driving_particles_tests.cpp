// Pixel and ordered-event comparisons against the unmodified disk code.
// Musashi belongs only to this SCR_BUILD_ORACLE executable.
#include "presentation/native_driving_particles.h"
#include "driving_effects_fixture.h"
#include "audio_playback_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using Memory=scr::runtime::NativeMemory;
namespace f=scr::test::effects;
namespace audio=scr::test::audio;
using namespace scr::runtime;
Memory ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,stopPc=0xa0000,frame=0x6a594;
constexpr std::array<std::array<unsigned,2>,5> codeRanges{{
    {0x60c92,0x60f9c},{0x62574,0x625a2},{0x69fe8,0x6a166},
    {0x66348,0x6642a},{audio::entry,audio::end}}};
constexpr std::array<unsigned,8> patches{0x66405,0x66409,0x6640d,0x66411,0x6641b,0x6641d,0x66421,0x66425};
std::array<bool,0x80000> writable{};
scr::DrivingParticleDraws originalDraws;
std::vector<std::uint8_t> originalSounds;
unsigned pc{},instructions{},cases{},dustCases{},sparkCases{},groundCases{},rejected{};
std::uint64_t totalInstructions{},comparedPixels{},drawEvents{};
std::uint32_t randomState=0x50415254;
bool invalid{},returned{};
std::string failure;
bool in(unsigned a,unsigned first,unsigned end){return a>=first&&a<end;}
unsigned word(const Memory& m,unsigned a){return (unsigned(m.at(a))<<8)|m.at(a+1);}
unsigned longword(const Memory& m,unsigned a){return (word(m,a)<<16)|word(m,a+2);}
void word(Memory& m,unsigned a,unsigned v){m.at(a)=std::uint8_t(v>>8);m.at(a+1)=std::uint8_t(v);}
void longword(Memory& m,unsigned a,unsigned v){word(m,a,v>>16);word(m,a+2,v);}
unsigned next(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
void fail(const char* why,unsigned a){invalid=true;if(failure.empty())failure=std::string(why)+" address="+std::to_string(a)+" pc="+std::to_string(pc);}
void hook(unsigned a){
    pc=a;if(invalid){m68k_end_timeslice();return;}
    if(a==stopPc){returned=true;m68k_end_timeslice();return;}
    const auto admitted=std::any_of(codeRanges.begin(),codeRanges.end(),[&](auto r){return in(a,r[0],r[1]);});
    if((a&1)||!admitted||++instructions>2000000){fail("Unexpected PC/budget",a);m68k_end_timeslice();return;}
    if(a==audio::entry)originalSounds.push_back(std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D0)&7));
    // Both hook points are after all original clipping/rejection tests.
    if(a==0x60f5a||a==0x60ed6){
        const auto index=unsigned(ram[0x1bbe4])>>1;
        originalDraws.push_back({std::uint16_t(word(ram,0x1c380+index*2)),
            std::uint16_t(word(ram,0x1c3c0+index*2)),std::uint8_t(index),a==0x60f5a});
    }
}
std::uint8_t read(unsigned a){
    if(a<ram.size())return ram[a];
    if(in(a,stackBase,stackTop))return stack[a-stackBase];
    if(in(a,stopPc,stopPc+4))return (a&1)?0x71:0x4e;
    fail("Unexpected read",a);return 0;
}
void write(unsigned a,std::uint8_t value){
    if(in(a,stackBase,stackTop)){stack[a-stackBase]=value;return;}
    if(a>=ram.size()){fail("Unexpected write",a);return;}
    if(std::find(patches.begin(),patches.end(),a)!=patches.end()){
        if(!in(pc,0x66348,0x663f2)||((ram[a]^value)&~0x40u)){fail("Unexpected code patch",a);return;}
    }else if(!writable[a]){fail("Unexpected RAM store",a);return;}
    ram[a]=value;
}
void run(unsigned entry,unsigned index=0){
    invalid=returned=false;failure.clear();instructions=0;m68k_pulse_reset();
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(stopPc>>(24-8*i));
    m68k_set_reg(M68K_REG_D1,index*2);m68k_set_reg(M68K_REG_A4,0x1c380);
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(100000000);
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stackTop)
        throw std::runtime_error("Original particle execution failed: "+failure+" case="+std::to_string(cases));
    totalInstructions+=instructions;
}
void prepare(const Memory& base,unsigned variation){
    ram=base;longword(ram,0x6a584,frame);longword(ram,0x6a58c,frame);longword(ram,0x6a590,frame+0x284);
    for(unsigned i=0;i<64000;++i)ram[frame+i]=std::uint8_t(i*37+(i>>7)*13+variation*71);
    originalDraws.clear();originalSounds.clear();
}
std::vector<std::uint32_t> pixels(const RaceRenderAssets& assets){
    const auto original=scr::decodePlanarImage(ram.data()+frame,32000,320,200,4);
    std::vector<std::uint32_t> result(64000);
    for(unsigned i=0;i<64000;++i)result[i]=assets.palette[original.pixels[i]];
    return result;
}
void comparePixels(const RaceRenderAssets& assets,const std::vector<std::uint32_t>& native,const char* stage){
    const auto original=scr::decodePlanarImage(ram.data()+frame,32000,320,200,4);
    for(unsigned i=0;i<64000;++i){++comparedPixels;if(native[i]!=assets.palette[original.pixels[i]])
        throw std::runtime_error(std::string(stage)+" pixel mismatch case="+std::to_string(cases)+" x="+std::to_string(i%320)+" y="+std::to_string(i/320));}
    ++cases;
}
void compareEvents(const scr::DrivingParticleDraws& native){
    if(native.size()!=originalDraws.size())throw std::runtime_error("Particle draw event count mismatch case="+std::to_string(cases));
    for(unsigned i=0;i<native.size();++i){const auto& n=native[i];const auto& o=originalDraws[i];
        if(n.x!=o.x||n.y!=o.y||n.index!=o.index||n.dust!=o.dust)throw std::runtime_error("Ordered particle draw mismatch case="+std::to_string(cases)+" event="+std::to_string(i));}
    drawEvents+=native.size();
}
void direct(const Memory& base,const RaceRenderAssets& assets,const NativeDrivingParticleData& data,
        unsigned x,unsigned y,unsigned index,unsigned phase,bool dust){
    prepare(base,cases);ram[0x1bb9c]=dust?128:0;ram[0x1bbac]=std::uint8_t(phase);
    word(ram,0x1c380+index*2,x);word(ram,0x1c3c0+index*2,y);
    auto native=pixels(assets);scr::DrivingParticleDraws draws;
    if(x<256&&y>=1&&y<128&&(dust||x<254))draws.push_back({std::uint16_t(x),std::uint16_t(y),std::uint8_t(index),dust});
    const auto before=std::make_unique<Memory>(ram);
    composeNativeDrivingParticles(assets,data,draws,std::uint8_t(phase),native);
    if(ram!=*before)throw std::runtime_error("Particle presentation changed gameplay memory");
    run(0x60e88,index);compareEvents(draws);comparePixels(assets,native,dust?"dust":"spark");
    if(draws.empty()&&word(ram,0x1c3c0+index*2)!=210)throw std::runtime_error("Original particle rejection did not retire particle");
    if(dust)++dustCases;else ++sparkCases;
}
scr::DrivingEffectsState ground(const Memory& base,const RaceRenderAssets& assets,const NativeDrivingParticleData& data,
        scr::DrivingEffectsInputs inputs,scr::DrivingEffectsState state,unsigned phase){
    prepare(base,cases);f::writeInputs(ram,inputs);f::writeState(ram,state);ram[0x1bbac]=std::uint8_t(phase);
    auto expected=std::make_unique<Memory>(ram),withoutDraws=std::make_unique<Memory>(ram);
    auto native=pixels(assets);scr::DrivingParticleDraws draws;
    auto noDrawState=state;
    const auto sounds=scr::updateGroundAndEdgeEffects(inputs,state,&draws);
    const auto noDrawSounds=scr::updateGroundAndEdgeEffects(inputs,noDrawState);
    f::writeState(*expected,state);f::writeState(*withoutDraws,noDrawState);
    if(*expected!=*withoutDraws||sounds!=noDrawSounds)throw std::runtime_error("Collecting particle events changed gameplay/RNG");
    const auto afterUpdate=std::make_unique<Memory>(*expected);
    composeNativeDrivingParticles(assets,data,draws,std::uint8_t(phase),native);
    if(*expected!=*afterUpdate)throw std::runtime_error("Particle presentation changed updated gameplay state");
    if(!inputs.raceStartTimer&&(inputs.offTrackFlags&128))run(0x60c92);
    run(0x60cf8);
    compareEvents(draws);
    if(sounds!=originalSounds)throw std::runtime_error("Particle ordered audio request mismatch");
    for(auto a:f::outputBytes())if(ram[a]!=(*expected)[a])throw std::runtime_error("Particle gameplay/RNG mismatch case="+std::to_string(cases)+" address="+std::to_string(a));
    comparePixels(assets,native,"ground-stage");++groundCases;return state;
}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return read(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1)fail("Odd word read",a);return (unsigned(read(a))<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){write(a,std::uint8_t(v));}
void m68k_write_memory_16(unsigned a,unsigned v){
    if(a&1){fail("Odd word write",a);return;}
    if(in(a,0xdff000,0xdff200)){if(!in(pc,audio::entry,audio::end))fail("Unexpected hardware writer",a);return;}
    write(a,std::uint8_t(v>>8));write(a+1,std::uint8_t(v));
}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc!=2)throw std::invalid_argument("Usage: native_driving_particles_tests original-asset-pack");
    auto startup=startNativeRace(argv[1]);const auto& base=*startup.memory;
    const auto assets=decodeRaceRenderAssets(startup.payload.data(),startup.payload.size(),base.data(),base.size());
    const auto data=decodeNativeDrivingParticleData(base);
    for(auto range:codeRanges)if(!std::equal(startup.payload.begin()+range[0]-0xe700,startup.payload.begin()+range[1]-0xe700,base.begin()+range[0]))
        throw std::runtime_error("Original particle code changed during initialization");
    for(auto a:f::outputBytes())writable[a]=true;
    std::fill(writable.begin()+frame,writable.begin()+frame+64000,true);
    std::fill(writable.begin()+0x6a166,writable.begin()+0x6a16c,true);
    for(unsigned i=0;i<4;++i)writable[audio::channels+4*i]=writable[audio::channels+4*i+1]=true;
    writable[audio::guard]=writable[audio::guard+1]=true;
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    // All 16 animation phases and 32 particle indices; every shift and the
    // left/right clipping edges, including rows clipped at the bottom.
    for(unsigned phase=0;phase<16;++phase)for(unsigned index=0;index<32;++index)
        direct(base,assets,data,(index*7+phase*13)&255,1+(index*11+phase*17)%127,index,phase,true);
    for(unsigned phase=0;phase<16;++phase)for(unsigned x=0;x<32;++x)for(unsigned y:{1u,127u}){
        direct(base,assets,data,x,y,phase,phase,true);
        direct(base,assets,data,224+x,y,31-phase,phase,true);
    }
    for(unsigned x=0;x<254;++x)for(unsigned y:{1u,2u,63u,126u,127u})direct(base,assets,data,x,y,x%32,x&15,false);
    for(bool dust:{false,true})for(unsigned x:{0u,253u,254u,255u,256u,65535u})for(unsigned y:{0u,1u,127u,128u,65535u})
        direct(base,assets,data,x,y,17,7,dust);
    // Consecutive ticks exercise draw-before-movement, newly spawned draws,
    // overlap order, retiring particles, both dust limits, sparks and no motion.
    auto inputs=f::readInputs(base);auto initial=f::readState(base);
    for(unsigned mode=0;mode<6;++mode){auto state=initial;inputs.raceStartTimer=0;
        inputs.offTrackFlags=mode<2?128:0;inputs.networkMode=mode==1?128:0;
        inputs.wheelMovementActive=mode==4?0:1;inputs.speedMagnitude=mode==5?0:0x2000;state.bouncePhase=128;
        for(auto& p:state.debris)p={128,125,0,0};
        for(unsigned tick=0;tick<64;++tick)state=ground(base,assets,data,inputs,state,tick);
    }
    for(unsigned variation=0;variation<128;++variation){auto state=initial;inputs.raceStartTimer=std::uint8_t(variation&3);
        inputs.offTrackFlags=(variation&1)?128:0;inputs.networkMode=std::uint8_t(variation&2);
        inputs.wheelMovementActive=std::uint8_t((variation>>2)&1);inputs.speedMagnitude=std::uint16_t(next());
        state.bouncePhase=std::uint8_t(next());state.postWreck=std::uint8_t(next());state.randomSeed=next();state.randomTail=std::uint8_t(next());
        for(auto& p:state.debris)p={std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next())};
        ground(base,assets,data,inputs,state,variation);
    }
    for(unsigned invalidCase=0;invalidCase<5;++invalidCase){std::vector<std::uint32_t> native(64000);scr::DrivingParticleDraws draws{{128,64,0,false}};
        if(invalidCase==0)native.resize(1);if(invalidCase==1)draws[0].index=32;
        if(invalidCase==2)draws[0].x=256;if(invalidCase==3)draws[0].y=0;if(invalidCase==4)draws[0].x=254;
        try{composeNativeDrivingParticles(assets,data,draws,0,native);}catch(const std::invalid_argument&){++rejected;}
    }
    if(rejected!=5)throw std::runtime_error("Particle renderer admitted an invalid domain");
    std::cout<<"{\"cases\":"<<cases<<",\"dust_cases\":"<<dustCases<<",\"spark_cases\":"<<sparkCases<<",\"ground_cases\":"<<groundCases
        <<",\"ordered_draw_events\":"<<drawEvents<<",\"compared_pixels\":"<<comparedPixels<<",\"original_instructions\":"<<totalInstructions
        <<",\"invalid_domains_rejected\":"<<rejected<<",\"mismatches\":0,\"simulation_and_rng_compared\":true,\"draw_collection_preserves_state\":true,\"all_canvas_pixels_compared\":true}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
