#include "driving_effects_fixture.h"
#include "audio_playback_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
namespace f=scr::test::effects;namespace audio=scr::test::audio;
using Ram=scr_test::ChipRam;
Ram ram{},expected{};std::array<std::uint8_t,4096> stackRam{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
constexpr std::array<std::array<unsigned,2>,12> ranges{{
    {0x5b56c,0x5b646},{0x62574,0x625a2},{0x60c92,0x60f9c},{0x69f42,0x6a166},
    {0x5e778,0x5e848},{0x5dfb4,0x5e098},{0x66106,0x662aa},{0x66348,0x6642a},
    {0x5e848,0x5e8ac},{0x5e4ec,0x5e502},{0x6091a,0x60988},{0xf362,0xf3ee}}};
constexpr std::array<unsigned,8> patches{0x66405,0x66409,0x6640d,0x66411,0x6641b,0x6641d,0x66421,0x66425};
std::array<bool,0x80000> writable{};std::vector<scr::AudioRegisterWrite> busWrites;
std::vector<std::uint8_t> soundIds;
std::uint32_t rng=0x45464632;
unsigned pc=0,instructions=0,maximumInstructions=0,frameBase=0;bool invalid=false,returned=false,executing=false;
unsigned holesOld=0,holesNew=0,pixelCalls=0,dustCalls=0;std::uint64_t total=0,audioRequests=0,totalInstructions=0;
std::array<std::uint64_t,4> stages{};std::string failure;
std::uint32_t next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
void fail(const char* why,unsigned a){invalid=true;if(failure.empty())failure=std::string(why)+" address="+std::to_string(a)+" pc="+std::to_string(pc);}
bool in(unsigned a,unsigned first,unsigned end){return a>=first&&a<end;}
bool code(unsigned a){for(auto r:ranges)if(in(a,r[0],r[1]))return true;return false;}
void hook(unsigned a){pc=a;if(invalid){m68k_end_timeslice();return;}
    if(a==sentinel){returned=true;m68k_end_timeslice();return;}
    if((a&1)||!code(a)||++instructions>2000000){fail("Invalid PC/budget",a);m68k_end_timeslice();return;}
    if(a==audio::entry)soundIds.push_back(std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D0)&7));
    if(a==0x6091a)++holesOld;if(a==0x60922)++holesNew;if(a==0x663f2)++pixelCalls;if(a==0x60f5a)++dustCalls;
}
std::uint8_t readByte(unsigned a){
    if(a<ram.size())return ram[a];if(in(a,stackBase,stackTop))return stackRam[a-stackBase];
    if(in(a,sentinel,sentinel+4))return (a&1)?0x71:0x4e;
    fail("Unexpected read",a);return 0;
}
void writeByte(unsigned a,std::uint8_t v){
    if(in(a,stackBase,stackTop)){stackRam[a-stackBase]=v;return;}
    if(a>=ram.size()){fail("Unexpected write",a);return;}
    if(std::find(patches.begin(),patches.end(),a)!=patches.end()){
        if(pc<0x66348||pc>=0x663f2||((ram[a]^v)&~0x40u)){fail("Unexpected code patch",a);return;}ram[a]=v;return;
    }
    if(!writable[a]&&!in(a,frameBase,frameBase+0xfa00)){fail("Unexpected RAM store",a);return;}ram[a]=v;
}
void mark(unsigned a,unsigned n){std::fill(writable.begin()+a,writable.begin()+a+n,true);}
void initializeMasks(){
    for(auto a:f::outputBytes())writable[a]=true;
    mark(0x6a166,6);mark(0x6a16c,0x340);mark(0x6a58c,4);
    for(unsigned i=0;i<4;++i)mark(audio::channels+4*i,2);mark(audio::guard,2);
}
void validateDrawState(const Ram& r){
    const auto base=f::damageBuffer(r);if(base!=0x6a594)throw std::invalid_argument("Only the captured framebuffer layout is admitted");
    if(f::readLong(r,0x6a58c)!=base||f::readLong(r,0x6a590)!=base+0x284)throw std::invalid_argument("Unrecognized captured render pointers");
    if(r[0x1c9cf]>10||r[0x662aa]<2||r[0x662aa]>6)throw std::invalid_argument("Damage UI values outside captured domain");
    for(unsigned id=0x17;id<=0x24;++id){
        const unsigned params=0x6a16c+id*16+4,source=f::readLong(r,0x6a4ac+id*4);
        const unsigned width=f::readWord(r,params)+1,height=f::readWord(r,params+2)+1;
        // The loaded sprite resources are relocated below the e700 code
        // payload (captured hole/dust data starts at93c0), so their valid
        // address domain is Chip RAM, not the executable's load interval.
        if(width>32||height>128||source<0x1000||source>=0x6a594||std::uint64_t(source)+std::uint64_t(width)*height*10>0x6a594)
            throw std::invalid_argument("Sprite source or dimensions outside captured domain id="+std::to_string(id)+" source="+std::to_string(source));
    }
}
void run(unsigned entry,bool reset){
    if(reset){executing=false;m68k_pulse_reset();for(unsigned reg=0;reg<15;++reg)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+reg),next());
        m68k_set_reg(M68K_REG_D1,next()&0xffff00ffu);m68k_set_reg(M68K_REG_D2,next()&0xffff00ffu);m68k_set_reg(M68K_REG_SR,0x2700|(next()&31));}
    for(unsigned k=0;k<4;++k)stackRam[4092+k]=std::uint8_t(sentinel>>(24-8*k));
    m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_PC,entry);invalid=returned=false;instructions=0;failure.clear();executing=true;
    m68k_execute(100000000);executing=false;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stackTop)throw std::runtime_error("Effects execution failure "+failure+" case="+std::to_string(total)+" pc="+std::to_string(pc));
    maximumInstructions=std::max(maximumInstructions,instructions);totalInstructions+=instructions;
}
scr::DrivingEffectsState check(unsigned stage,const scr::DrivingEffectsInputs& inputs,scr::DrivingEffectsState state){
    state.damagePixelTouched.fill(false);f::writeInputs(ram,inputs);f::writeState(ram,state);
    const auto initialAudio=audio::readState(ram.data());auto data=audio::readData(ram.data());
    expected=ram;frameBase=f::damageBuffer(ram);soundIds.clear();busWrites.clear();holesOld=holesNew=pixelCalls=dustCalls=0;
    std::vector<std::uint8_t> requests;scr::DrivingVisualEvents visual{};
    if(stage==0){run(0x5b56c,true);scr::updateEnginePitch(inputs,state);}
    else if(stage==1){
        if(!inputs.raceStartTimer&&(inputs.offTrackFlags&0x80)){run(0x60c92,true);run(0x60cf8,false);}else run(0x60cf8,true);
        requests=scr::updateGroundAndEdgeEffects(inputs,state);
    }else if(stage==2){scr::TrigTable trig{};for(unsigned i=0;i<trig.size();++i)trig[i]=f::readWord(ram,0x1ca42+2*i);
        run(0x5e778,true);scr::updateWheelVisuals(inputs,trig,state);
    }else{run(0x5dfb4,true);requests=scr::updateDamageFeedback(inputs,state,visual);}
    f::writeState(expected,state);
    auto playback=initialAudio;data.samples[1].period=state.edgeSamplePeriod;data.samples[6].period=state.groundSamplePeriod;
    data.samples[4].volume=std::uint16_t((data.samples[4].volume&0xff00u)|state.creakVolume);
    std::vector<scr::AudioRegisterWrite> expectedBus;
    for(auto id:requests){const auto writes=scr::requestSample(id,data,playback);expectedBus.insert(expectedBus.end(),writes.begin(),writes.end());}
    audio::writeState(expected.data(),playback);
    for(auto a:f::outputBytes())if(ram[a]!=expected[a]){
        std::cerr<<"Effects mismatch case="<<total<<" stage="<<stage<<" address="<<std::hex<<a<<std::dec<<" original="<<unsigned(ram[a])<<" native="<<unsigned(expected[a])<<'\n';
        throw std::runtime_error("Driving effects state mismatch");}
    const auto actualAudio=audio::readState(ram.data());
    if(actualAudio.completedInterrupts!=playback.completedInterrupts||actualAudio.dmaEnableGuard!=playback.dmaEnableGuard||soundIds!=requests||expectedBus.size()!=busWrites.size())throw std::runtime_error("Driving audio mismatch");
    for(unsigned i=0;i<busWrites.size();++i)if(busWrites[i].address!=expectedBus[i].address||busWrites[i].value!=expectedBus[i].value)throw std::runtime_error("Driving ordered Paula mismatch");
    if(stage==3){
        if(bool(holesOld)!=visual.redrawExistingHole||bool(holesNew)!=visual.drawNewHole)throw std::runtime_error("Hole visual event mismatch");
        // Hole sprites lie outside this eight-row feedback strip. Compare the
        // actual colors of every explicitly drawn pixel in both framebuffers.
        for(unsigned pixel=0;pixel<state.damagePixelTouched.size();++pixel)if(state.damagePixelTouched[pixel])
            for(unsigned offset:{0u,0x7d00u})for(unsigned plane=0;plane<4;++plane){
                const auto a=f::damagePixelAddress(frameBase+offset,pixel%256,pixel/256,plane),mask=0x80u>>(pixel%8);
                if((ram[a]&mask)!=(expected[a]&mask))throw std::runtime_error("Damage pixel color mismatch");}
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<256;++x)
            if(f::occupied(ram,frameBase,x,y)!=state.damagePixels[y*256+x])throw std::runtime_error("Damage occupancy mismatch case="+std::to_string(total)+
                " x="+std::to_string(x)+" y="+std::to_string(y)+" original="+std::to_string(f::occupied(ram,frameBase,x,y))+
                " native="+std::to_string(state.damagePixels[y*256+x])+" oldHole="+std::to_string(holesOld)+" newHole="+std::to_string(holesNew)+
                " damage="+std::to_string(state.carDamage)+" target="+std::to_string(state.targetDamage));
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<256;++x){const unsigned pixel=y*256+x;
            if(f::pixelColor(ram,frameBase,x,y)!=state.damagePixelColors[pixel]||
               f::pixelColor(ram,frameBase+0x7d00,x,y)!=state.damageOtherPixelColors[pixel])
                throw std::runtime_error("Full damage canvas color mismatch case="+std::to_string(total)+" pixel="+std::to_string(pixel));}
    }
    audioRequests+=requests.size();++total;++stages[stage];return state;
}
std::vector<std::uint8_t> readFile(const char* p){std::ifstream s(p,std::ios::binary);if(!s)throw std::runtime_error("Cannot read input");return {std::istreambuf_iterator<char>(s),std::istreambuf_iterator<char>()};}
}
extern "C"{
unsigned int m68k_read_memory_8(unsigned a){return readByte(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1){fail("Odd word read",a);return 0;}return (unsigned(readByte(a))<<8)|readByte(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){writeByte(a,std::uint8_t(v));}
void m68k_write_memory_16(unsigned a,unsigned v){
    if(a&1){fail("Odd word write",a);return;}
    if(a>=0xdff000&&a<0xdff200){if(pc<audio::entry||pc>=audio::end){fail("Unexpected hardware writer",a);return;}busWrites.push_back({a,std::uint16_t(v)});return;}
    writeByte(a,std::uint8_t(v>>8));writeByte(a+1,std::uint8_t(v));
}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc<4||(argc-2)%2)throw std::runtime_error("Usage: driving_effects_oracle payload.bin label chipram.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Payload size mismatch");
    initializeMasks();m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"seed\":1162233394,\"captures\":[";
    for(int arg=2;arg<argc;arg+=2){
        const auto capture=readFile(argv[arg+1]);if(capture.size()!=ram.size())throw std::runtime_error("Capture size mismatch");std::copy(capture.begin(),capture.end(),ram.begin());
        validateDrawState(ram);
        for(auto range:ranges)for(unsigned a=range[0];a<range[1];++a){const bool patch=std::find(patches.begin(),patches.end(),a)!=patches.end();
            if(((ram[a]^payload[a-0xe700])&(patch?0xbfu:0xffu))!=0)throw std::runtime_error("Captured effect code mismatch");}
        for(unsigned stage=0;stage<4;++stage){std::copy(capture.begin(),capture.end(),ram.begin());check(stage,f::readInputs(ram),f::readState(ram));}
        if(arg>2)std::cout<<',';std::cout<<"{\"label\":\""<<argv[arg]<<"\",\"stages\":4}";
    }
    const Ram capture=ram;auto inputs=f::readInputs(ram);auto state=f::readState(ram);
    const auto captureCases=total;
    for(unsigned value=0;value<65536;++value){inputs.localForwardSpeed=std::uint16_t(value);inputs.wheelMovementActive=1;check(0,inputs,state);}
    for(unsigned value=0;value<65536;++value){inputs.enginePitchAccumulator=std::uint16_t(value);inputs.wheelMovementActive=std::uint8_t(value&1);inputs.inputFlags=std::uint8_t(value);check(0,inputs,state);}
    ram=capture;inputs=f::readInputs(ram);state=f::readState(ram);
    for(unsigned value=0;value<65536;++value){inputs.wheelTravel={std::uint16_t(value),std::uint16_t(~value)};state.bouncePhase=std::uint8_t(value);state.wheelBaseHeight=std::uint8_t(value>>8);check(2,inputs,state);}
    ram=capture;inputs=f::readInputs(ram);state=f::readState(ram);inputs.raceStartTimer=0;
    for(unsigned speed=0;speed<256;++speed)for(unsigned mode=0;mode<4;++mode){
        inputs.speedMagnitude=std::uint16_t(speed<<8);inputs.offTrackFlags=std::uint8_t((mode&1)?0x80:0);
        inputs.wheelMovementActive=std::uint8_t(mode&2);inputs.networkMode=std::uint8_t(speed&1);state.bouncePhase=0x80;check(1,inputs,state);}
    for(unsigned n=0;n<400;++n){ram=capture;inputs=f::readInputs(ram);state=f::readState(ram);inputs.raceStartTimer=0;
        inputs.speedMagnitude=std::uint16_t(next());inputs.offTrackFlags=std::uint8_t((n&1)?0x80:0);inputs.wheelMovementActive=1;state.bouncePhase=0x80;
        state.randomSeed=next();state.randomTail=std::uint8_t(next());for(auto& p:state.debris)p={std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next())};check(1,inputs,state);}
    for(unsigned damage=0;damage<256;++damage)for(unsigned cooldown:{0u,1u,0x45u,0xffu}){
        ram=capture;inputs=f::readInputs(ram);state=f::readState(ram);state.accumulatedForce.fill(std::uint8_t(damage));state.damageActive=1;
        state.majorImpactCooldown=std::uint8_t(cooldown);state.holePosition=std::uint8_t(damage%11);inputs.maxCompression=std::uint16_t(damage<<8);check(3,inputs,state);}
    for(unsigned sequence=0;sequence<4;++sequence){ram=capture;inputs=f::readInputs(ram);state=f::readState(ram);
        inputs.raceStartTimer=0;inputs.wheelMovementActive=1;inputs.offTrackFlags=std::uint8_t((sequence&1)?0x80:0);state.bouncePhase=0x80;
        for(unsigned tick=0;tick<64;++tick){
            inputs.speedMagnitude=std::uint16_t((1+tick%40)<<8);state=check(0,inputs,state);state=check(1,inputs,state);
            state=check(2,inputs,state);state.damageActive=1;state.accumulatedForce.fill(std::uint8_t(tick));inputs.maxCompression=0x500;
            state=check(3,inputs,state);
        }
    }
    unsigned rejected=0;for(unsigned kind=0;kind<3;++kind){ram=capture;if(kind==0)f::writeLong(ram,0x6a584,0xffff0000);if(kind==1)ram[0x1c9cf]=0xff;if(kind==2)f::writeLong(ram,0x6a4ac+0x17*4,0);
        try{validateDrawState(ram);}catch(const std::invalid_argument&){++rejected;}}
    if(rejected!=3)throw std::runtime_error("Invalid draw domain accepted");
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"total_cases\":"<<total<<",\"mismatches\":0,\"audio_requests\":"<<audioRequests
        <<",\"invalid_domains_rejected\":"<<rejected<<",\"maximum_instructions\":"<<maximumInstructions<<",\"total_instructions\":"<<totalInstructions
        <<",\"stages\":["<<stages[0]<<','<<stages[1]<<','<<stages[2]<<','<<stages[3]<<"],\"renderer_helpers_executed\":true,\"ordered_paula_writes_compared\":true,\"entry_d1_d2_high_bytes\":0}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
