// Full original local processPlayerInput, including the real primary-font HUD.
// HUD pixels/text state are excluded from native gameplay equivalence. No call
// is patched, skipped or replaced; network mode is explicitly unsupported.
#include "player_controls_fixture.h"
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
namespace f=scr::test::controls;
using Ram=std::array<std::uint8_t,0x80000>;
Ram ram{},nativeRam{},baseline{};
constexpr unsigned stack=0x7f000,sentinel=0x7e000;
std::uint32_t rng=0x504c4159,registerRng=0x36384354;
std::uint64_t cases=0,hudCases=0,joystickCases=0;
bool executing=false,invalid=false,returned=false,prefix=false;
unsigned pcNow=0,instructions=0,maximumInstructions=0,joyReads=0,portReads=0,directionReads=0,directionWrites=0,hudCalls=0;
scr::ControllerHardware hardware{};
scr::PlayerControlsEffects originalEffects{};
unsigned framebufferBase=0,renderBase=0;
std::uint32_t next(std::uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
std::uint8_t byte(){return static_cast<std::uint8_t>(next(rng));}
bool inside(unsigned a,unsigned n,unsigned b,unsigned e){return a>=b&&a<e&&n<=e-a;}
std::vector<std::uint8_t> readFile(const char* name){std::ifstream in(name,std::ios::binary);if(!in)throw std::runtime_error(std::string("Cannot read ")+name);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void bad(){invalid=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();}
void hook(unsigned pc){
    pcNow=pc;
    if(invalid){bad();return;}
    if(pc==sentinel){returned=true;m68k_end_timeslice();return;}
    if(pc==0x60846){
        if(prefix){returned=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;}
        ++hudCalls;originalEffects.fuelHudRedraw=true;originalEffects.displayedFuel=ram[0x1ca20];
    }
    bool allowed=prefix?inside(pc,2,0x60824,0x60846):inside(pc,2,0x594c6,0x59806);
    if(!prefix)for(auto r:f::codeRanges)allowed|=inside(pc,2,r.begin,r.end);
    if(!allowed||(pc&1)||++instructions>6000)bad();
}
bool validRead(unsigned a,unsigned n){return inside(a,n,0,unsigned(ram.size()))&&(n==1||!(a&1));}
bool allowedWrite(unsigned a,unsigned n){
    if(!executing||!validRead(a,n))return false;
    if(inside(a,n,stack-128,stack))return true;
    if(prefix)return n==1&&a==0x1ca20;
    if(n==1&&(std::find(f::byteOutputs.begin(),f::byteOutputs.end(),a)!=f::byteOutputs.end()||a==0x1bd2a||a==0x1bd2b))return true;
    if(n==2&&a==0x1bd2a)return true;
    if(n==4&&a==0x625a2)return true;
    if(n==1&&(a==0x1bb6d||a==0x1bb6e||inside(a,1,0x5983e,0x59845)))return true;
    return inside(pcNow,2,0x594c6,0x59806)&&n==2
        &&(inside(a,n,framebufferBase,framebufferBase+64000)||inside(a,n,renderBase,renderBase+32000));
}
void original(bool fuelPrefix,std::uint8_t delta=0){
    prefix=fuelPrefix;executing=false;
    if(ram[0x57c3c])throw std::runtime_error("Network transport is outside this local control oracle");
    if(ram[0x5924a])throw std::runtime_error("Oracle HUD requires original primary-font mode");
    framebufferBase=f::longword(ram.data(),0x6a584);renderBase=f::longword(ram.data(),0x6a58c);
    if(framebufferBase<0x68000||!inside(framebufferBase,64000,0,0x80000)||renderBase<0x68000||!inside(renderBase,32000,0,0x80000))
        throw std::runtime_error("Invalid captured framebuffer mapping");
    f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,prefix?0x60824:f::entry);f::putLong(ram.data(),stack,sentinel);
    f::putWord(ram.data(),sentinel,0x4e72);f::putWord(ram.data(),sentinel+2,0x2700);m68k_pulse_reset();
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),next(registerRng));
    m68k_set_reg(M68K_REG_D1,next(registerRng)&0xffff00ffu);m68k_set_reg(M68K_REG_D2,next(registerRng)&0xffff00ffu);
    if(prefix)m68k_set_reg(M68K_REG_D0,(next(registerRng)&0xffffff00u)|delta);
    m68k_set_reg(M68K_REG_SR,0x2700|(next(registerRng)&31));m68k_set_reg(M68K_REG_SP,stack);
    m68k_set_reg(M68K_REG_PC,prefix?0x60824:f::entry);
    invalid=returned=false;instructions=joyReads=portReads=directionReads=directionWrites=hudCalls=0;originalEffects={};
    executing=true;m68k_execute(200000);executing=false;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=(prefix?stack:stack+4)||hudCalls>1
       ||joyReads!=directionWrites||portReads!=directionWrites||directionReads!=directionWrites||directionWrites>1)
        throw std::runtime_error("Original local controls execution/access contract failed PC="+std::to_string(pcNow));
    maximumInstructions=std::max(maximumInstructions,instructions);
}
void check(const scr::PlayerControlsInputs& input,scr::PlayerControlsState* persistent=nullptr){
    f::writeInputs(ram.data(),input);hardware=input.hardware;
    for(auto r:f::observedRanges)std::copy(ram.begin()+r.begin,ram.begin()+r.end,nativeRam.begin()+r.begin);
    auto local=f::readState(ram.data());auto& state=persistent?*persistent:local;
    const auto effects=scr::processLocalPlayerInput(input,f::readKeys(ram.data()),state);
    f::writeState(nativeRam.data(),state);original(false);
    for(auto r:f::observedRanges)for(unsigned a=r.begin;a<r.end;++a)if(ram[a]!=nativeRam[a]){
        std::cerr<<"Control mismatch case="<<cases<<" address0x"<<std::hex<<a<<std::dec<<" original="<<unsigned(ram[a])<<" native="<<unsigned(nativeRam[a])<<'\n';
        throw std::runtime_error("Local controls state mismatch");}
    if(effects.joystickRead!=originalEffects.joystickRead||(effects.joystickRead&&effects.ciaDataDirection!=originalEffects.ciaDataDirection)
       ||effects.fuelHudRedraw!=originalEffects.fuelHudRedraw||(effects.fuelHudRedraw&&effects.displayedFuel!=originalEffects.displayedFuel))
        throw std::runtime_error("Control hardware/HUD event mismatch");
    ++cases;hudCases+=hudCalls;joystickCases+=directionWrites;
}
void fuelCheck(std::uint8_t delta,std::uint8_t fuel,std::uint8_t maximum){
    ram[0x1ca20]=fuel;ram[0x1bb95]=maximum;const auto expected=scr::adjustBoostFuel(delta,fuel,maximum);
    original(true,delta);if(ram[0x1ca20]!=expected)throw std::runtime_error("BCD prefix mismatch");++cases;
}
scr::ControllerHardware joystick(unsigned controls,std::uint8_t direction=0xff){
    const unsigned left=(controls>>2)&1,right=(controls>>3)&1,up=controls&1,down=(controls>>1)&1;
    return {static_cast<std::uint16_t>((left<<9)|((up^left)<<8)|(right<<1)|(down^right)),
            static_cast<std::uint8_t>(controls&16?0x7f:0xff),direction};
}
scr::PlayerControlsInputs baseInput(){
    auto in=f::readInputs(baseline.data(),joystick(0));in.keyboard.fill(0);in.localVelocityZ=0x1000;
    in.wheelMovementActive=0x80;in.raceStartTimer=in.postWreckStateFlag=0;in.frameThrottleFlag=0x80;
    in.displayUpdateFlag=0x80;in.maxBoostFuel=0x99;return in;
}
void randomState(){
    for(auto r:f::observedRanges)for(unsigned a=r.begin;a<r.end;++a)ram[a]=byte();
}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){
    if(a==0xbfe001){++portReads;return hardware.ciaPortA;}
    if(a==0xbfe201){++directionReads;return hardware.ciaDataDirection;}
    if(!validRead(a,1)){invalid=true;return 0;}return ram[a];
}
unsigned int m68k_read_memory_16(unsigned int a){
    if(a==0xdff00c){++joyReads;return hardware.joy1dat;}
    if(!validRead(a,2)){invalid=true;return 0;}return f::word(ram.data(),a);
}
unsigned int m68k_read_memory_32(unsigned int a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){
    if(executing&&a==0xbfe201){++directionWrites;hardware.ciaDataDirection=std::uint8_t(v);originalEffects.joystickRead=true;originalEffects.ciaDataDirection=std::uint8_t(v);return;}
    if(!allowedWrite(a,1)){invalid=true;return;}ram[a]=std::uint8_t(v);
}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(!allowedWrite(a,2)){invalid=true;return;}f::putWord(ram.data(),a,std::uint16_t(v));}
void m68k_write_memory_32(unsigned int a,unsigned int v){if(!allowedWrite(a,4)){invalid=true;return;}f::putLong(ram.data(),a,v);}
}
int main(int argc,char** argv)try{
    if(argc<4||(argc-2)%2)throw std::runtime_error("Usage: player_controls_oracle payload.bin label chipram.bin [...]");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Invalid payload size");
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"seed\":"<<rng<<",\"captures\":[";
    for(int a=2;a<argc;a+=2){
        const std::string label=argv[a];if(label.empty()||label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)throw std::runtime_error("Invalid label");
        const auto capture=readFile(argv[a+1]);if(capture.size()!=ram.size())throw std::runtime_error("Expected512KiB capture");
        for(auto r:f::codeRanges)if(!std::equal(payload.begin()+r.begin-0xe700,payload.begin()+r.end-0xe700,capture.begin()+r.begin))throw std::runtime_error("Captured control code differs");
        if(!std::equal(payload.begin()+0x594c6-0xe700,payload.begin()+0x59806-0xe700,capture.begin()+0x594c6))throw std::runtime_error("Captured renderer differs");
        std::copy(capture.begin(),capture.end(),ram.begin());baseline=ram;check(f::readInputs(ram.data(),joystick(0)));
        if(a>2)std::cout<<',';std::cout<<"{\"label\":\""<<label<<"\",\"hardware\":\"explicit neutral joystick; captured keyboard\"}";
    }
    const auto captureCases=cases;ram=baseline;
    for(unsigned joy=0;joy<65536;++joy)for(unsigned fire=0;fire<2;++fire){
        randomState();auto in=baseInput();in.hardware={std::uint16_t(joy),std::uint8_t(fire?0x7f:0xff),std::uint8_t(joy)};check(in);}
    for(unsigned value=0;value<65536;++value){
        randomState();auto in=baseInput();in.hardware=joystick(1);in.localVelocityZ=std::uint16_t(value);check(in);
        randomState();in.localVelocityZ=0;in.gasInputIntensity=std::uint8_t(value);in.gasInputAccumulator=std::uint8_t(value>>8);check(in);}
    const auto sweepCases=cases-captureCases;
    const auto keys=f::readKeys(ram.data());
    for(unsigned mask=0;mask<32;++mask)for(unsigned joy=0;joy<32;++joy)for(unsigned display=0;display<2;++display){
        randomState();auto in=baseInput();for(unsigned k=0;k<5;++k)if(mask&(1u<<k))in.keyboard[keys[k]]=0xb3;
        in.hardware=joystick(joy);in.displayUpdateFlag=display?0x80:0;check(in);}
    for(unsigned field=0;field<11;++field)for(unsigned value=0;value<256;++value)for(unsigned variant=0;variant<2;++variant){
        randomState();auto in=baseInput();in.hardware=joystick(variant?17:0);in.frameThrottleFlag=variant?0:0x80;
        auto state=f::readState(ram.data());state.boostFuel=0x34;state.fuelTimer=1;state.accelerationState=0x80;
        const auto v=std::uint8_t(value);
        switch(field){case 0:in.wheelMovementActive=v;break;case 1:in.raceStartTimer=v;break;case 2:in.postWreckStateFlag=v;break;
        case 3:in.gasInputIntensity=v;break;case 4:in.gasInputAccumulator=v;break;case 5:in.fuelConsumptionRate=v;break;
        case 6:in.frameThrottleFlag=v;break;case 7:in.maxBoostFuel=v;break;case 8:state.accelerationState=v;break;
        case 9:state.fuelTimer=v;break;default:state.boostFuel=v;break;}
        f::writeState(ram.data(),state);check(in);}
    const auto boundaryCases=cases-captureCases-sweepCases;
    for(unsigned value=0;value<256;++value)for(unsigned other=0;other<256;++other){fuelCheck(std::uint8_t(other),std::uint8_t(value),0xff);fuelCheck(0x99,std::uint8_t(value),std::uint8_t(other));}
    const auto bcdCases=cases-captureCases-sweepCases-boundaryCases;
    for(unsigned n=0;n<10000;++n){
        randomState();auto in=baseInput();for(auto& key:in.keyboard)key=byte();
        for(auto key:keys)if(byte()&1)in.keyboard[key]=0xb3;
        in.hardware={std::uint16_t(next(rng)),byte(),byte()};in.localVelocityZ=std::uint16_t(next(rng));
        in.wheelMovementActive=byte();in.raceStartTimer=byte();in.postWreckStateFlag=byte();in.displayUpdateFlag=byte();
        in.gasInputIntensity=byte();in.gasInputAccumulator=byte();in.fuelConsumptionRate=byte();in.frameThrottleFlag=byte();in.maxBoostFuel=byte();
        if(n%4==0){in.keyboard.fill(0);in.hardware=joystick(17);in.raceStartTimer=in.postWreckStateFlag=in.frameThrottleFlag=0;ram[0x1bb3d]=0;ram[0x1bba8]=0x80;}
        check(in);}
    const auto randomCases=cases-captureCases-sweepCases-boundaryCases-bcdCases;
    ram=baseline;auto nativeState=f::readState(ram.data());auto in=baseInput();in.frameThrottleFlag=0;in.fuelConsumptionRate=2;
    for(unsigned n=0;n<4096;++n){in.hardware=joystick(n%64<48?17:0);in.localVelocityZ=std::uint16_t(n*7);check(in,&nativeState);}
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"word_sweep_cases\":"<<sweepCases<<",\"boundary_cases\":"<<boundaryCases
             <<",\"bcd_prefix_cases\":"<<bcdCases<<",\"random_cases\":"<<randomCases<<",\"independent_native_sequence_cases\":4096"
             <<",\"total_cases\":"<<cases<<",\"mismatches\":0,\"hud_redraws_with_original_renderer_executed\":"<<hudCases
             <<",\"joystick_reads\":"<<joystickCases<<",\"maximum_instructions\":"<<maximumInstructions
             <<",\"network_supported\":false,\"graphics_and_text_outputs_excluded\":true,\"original_code_unmodified\":true}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<'\n'<<e.what()<<'\n';return 1;}
