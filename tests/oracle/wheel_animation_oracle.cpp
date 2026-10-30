#include "wheel_animation_fixture.h"
#include "presentation/native_wheel_sprites.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace f=scr::test::wheelAnimation;
std::array<std::uint8_t,0x80000> ram{},expected{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned pc{},instructions{},maximumInstructions{},activeEntry{};
bool invalid{},returned{},executing{};
std::string failure;
std::uint32_t rng=0x57484545;
std::uint64_t cases{},totalInstructions{},prefixCases{},graphicsCases{},rotationCases{},updatedCases{},carryCases{};
std::uint64_t positionCases{},pixelCases{},pixelsCompared{};
std::array<std::uint64_t,12> selectedSprites{};
std::vector<unsigned> selectedIds;
struct PointerWrite {unsigned address,value;};
std::vector<PointerWrite> pointerWrites;
bool in(unsigned a,unsigned first,unsigned end){return a>=first&&a<end;}
std::uint32_t random32(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
void fail(const std::string& why,unsigned a) {
    invalid=true;if(failure.empty())failure=why+" address="+std::to_string(a)+" pc="+std::to_string(pc);
}
bool code(unsigned a) {
    return in(a,f::vblankEntry,f::vblankEnd)||in(a,f::graphicsEntry,f::graphicsEnd)||
        in(a,f::rotationEntry,f::rotationEnd)||in(a,f::spriteEntry,0x69f40);
}
void hook(unsigned a) {
    pc=a;
    if(invalid){m68k_end_timeslice();return;}
    if(a==sentinel||(activeEntry==f::vblankEntry&&a==f::vblankEnd)) {
        // Musashi finishes one fetch after an instruction-hook stop request.
        // Redirect that fetch to the external NOP sentinel at the declared
        // audio boundary; no original instruction or payload byte is patched.
        if(a==f::vblankEnd)m68k_set_reg(M68K_REG_PC,sentinel);
        returned=true;m68k_end_timeslice();return;
    }
    if(a==f::spriteEntry)selectedIds.push_back(m68k_get_reg(nullptr,M68K_REG_D0)&255);
    if((a&1)||!code(a)||++instructions>1024) {fail("Unexpected PC or instruction budget",a);m68k_end_timeslice();}
}
std::uint8_t readByte(unsigned a) {
    if(a<ram.size())return ram[a];
    if(in(a,stackBase,stackTop))return stack[a-stackBase];
    if(in(a,sentinel,sentinel+4))return (a&1)?0x71:0x4e;
    fail("Unexpected read",a);return 0;
}
bool writable(unsigned a) {
    if(activeEntry==0x69f04)for(unsigned id=37;id<49;++id) {
        const auto pointer=f::longword(ram.data(),0x6a4ac+4*id);
        if(a==pointer||a==pointer+2)return true;
    }
    return in(a,stackBase,stackTop)||a==0x1bbe3||a==0x1bbdb||a==0x616d8||a==0x616da||
        in(a,0xe7f0,0xe7f2)||in(a,0xe7f4,0xe7f6)||in(a,0xe7f8,0xe7fa)||in(a,0xe7fc,0xe7fe);
}
void writeByte(unsigned a,std::uint8_t value) {
    if(!executing||!writable(a)){fail("Unexpected write",a);return;}
    if(in(a,stackBase,stackTop)){stack[a-stackBase]=value;return;}
    ram[a]=value;
}
void run(unsigned entry) {
    activeEntry=entry;executing=false;m68k_pulse_reset();
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),random32());
    m68k_set_reg(M68K_REG_SR,0x2700|(random32()&31));
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_PC,entry);
    invalid=returned=false;failure.clear();instructions=0;pointerWrites.clear();selectedIds.clear();
    executing=true;m68k_execute(100000);executing=false;
    const unsigned endSp=entry==f::vblankEntry?stackTop-4:stackTop;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=endSp)
        throw std::runtime_error("Wheel CPU execution failure "+failure+" case="+std::to_string(cases));
    maximumInstructions=std::max(maximumInstructions,instructions);totalInstructions+=instructions;
}
void compareRam() {
    if(ram!=expected) {
        const auto a=std::mismatch(ram.begin(),ram.end(),expected.begin()).first-ram.begin();
        throw std::runtime_error("Wheel whole-RAM mismatch case="+std::to_string(cases)+" address="+std::to_string(a));
    }
}
void check(unsigned entry) {
    const auto data=f::readData(ram.data());const auto inputs=f::readInputs(ram.data());
    auto state=f::readState(ram.data());const auto accumulator=state.accumulator;
    expected=ram;scr::WheelAnimationSelection selection;
    if(entry==f::vblankEntry){selection=scr::updateWheelVblank(data,inputs,state);++prefixCases;}
    else if(entry==f::graphicsEntry){selection=scr::updateWheelGraphics(data,inputs,state);++graphicsCases;}
    else {scr::advanceWheelRotationFrame(inputs.localForwardVelocity,state);++rotationCases;}
    f::writeState(expected.data(),state);run(entry);compareRam();
    if(selection.updated) {
        ++updatedCases;if(unsigned(accumulator)+inputs.speedHigh>255)++carryCases;
        if(selectedIds.size()!=2||selectedIds[0]!=selection.spriteIds[0]||selectedIds[1]!=selection.spriteIds[1])
            throw std::runtime_error("Wheel graphic ID mismatch");
        if(pointerWrites.size()!=4)throw std::runtime_error("Wheel copper pointer write count mismatch");
        for(unsigned wheel=0;wheel<2;++wheel) {
            ++selectedSprites[selection.spriteIds[wheel]-37];
            const unsigned address=0xe7f0+8*wheel,pointer=state.spritePointers[wheel];
            if(pointerWrites[2*wheel].address!=address+4||pointerWrites[2*wheel].value!=(pointer&65535)||
               pointerWrites[2*wheel+1].address!=address||pointerWrites[2*wheel+1].value!=(pointer>>16))
                throw std::runtime_error("Wheel ordered copper pointer writes mismatch");
        }
    } else if(!pointerWrites.empty()||!selectedIds.empty())throw std::runtime_error("Frozen wheel changed a sprite pointer");
    ++cases;
}
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Cannot read wheel fixture");
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
void checkOriginalCode(const std::vector<std::uint8_t>& payload) {
    for(auto range:std::array<std::array<unsigned,2>,4>{{{f::rotationEntry,f::rotationEnd},
        {f::graphicsEntry,f::graphicsEnd},{f::spriteEntry,0x69f40},{f::vblankEntry,f::vblankEnd}}})
        if(!std::equal(payload.begin()+range[0]-0xe700,payload.begin()+range[1]-0xe700,ram.begin()+range[0]))
            throw std::runtime_error("Original wheel code differs from supplied payload");
}
void setCase(std::uint8_t speed,std::uint8_t accumulator,std::uint8_t phase,std::uint16_t velocity,
        std::uint16_t right=126,std::uint16_t left=126) {
    f::writeInputs(ram.data(),{0x80,0,speed,velocity,{{right,left}}});
    scr::WheelAnimationState state{accumulator,phase,{{random32(),random32()}},std::uint8_t(random32()),std::uint8_t(random32())};
    f::writeState(ram.data(),state);
}
void initializeSyntheticSprites() {
    for(unsigned colour=0;colour<4;++colour)f::putWord(ram.data(),0x69eb4+2*colour,std::uint16_t(random32()&0xfff));
    f::putWord(ram.data(),0x69ec4,60);ram[0x69f41]=0x80;
    for(unsigned i=0;i<12;++i) {
        const unsigned id=37+i,address=0x2000+i*80;
        f::putLong(ram.data(),0x6a4ac+4*id,address);
        f::putWord(ram.data(),0x6a16c+16*id+4,0);f::putWord(ram.data(),0x6a16c+16*id+6,15);
        f::putWord(ram.data(),0x6a16c+16*id+10,119);
        f::putWord(ram.data(),address,std::uint16_t(0xb300|((i%6<3)?200:80)));
        f::putWord(ram.data(),address+2,0xc200);
        for(unsigned row=0;row<16;++row){f::putWord(ram.data(),address+4+4*row,std::uint16_t(random32()));
            f::putWord(ram.data(),address+6+4*row,std::uint16_t(random32()));}
        f::putLong(ram.data(),address+68,0);
    }
}
void checkSpritePositions(const scr::runtime::NativeWheelSprites& sprites) {
    ram[0x69f41]=0x80;
    const auto predicted=scr::runtime::captureNativeWheelSpriteFrame(sprites,ram.data(),ram.size());expected=ram;
    for(unsigned i=0;i<12;++i) {
        const auto address=sprites.images[i].address;
        expected[address]=std::uint8_t(predicted.placements[i].y+f::word(ram.data(),0x69ec4));
        expected[address+2]=std::uint8_t(expected[address]+predicted.placements[i].rows);
    }
    run(0x69f04);compareRam();
    ram[0x69f41]=0;
    const auto actual=scr::runtime::captureNativeWheelSpriteFrame(sprites,ram.data(),ram.size());
    for(unsigned i=0;i<12;++i)if(predicted.placements[i].x!=actual.placements[i].x||
        predicted.placements[i].y!=actual.placements[i].y||predicted.placements[i].rows!=actual.placements[i].rows)
            throw std::runtime_error("Wheel original sprite-position prediction mismatch");
    ++cases;++positionCases;
}
void checkSpritePixels(const scr::runtime::NativeWheelSprites& sprites) {
    auto frame=scr::runtime::captureNativeWheelSpriteFrame(sprites,ram.data(),ram.size());
    for(unsigned variant=0;variant<144;++variant) {
        scr::WheelAnimationState state;state.spritePointers={{sprites.images[variant/12].address,sprites.images[variant%12].address}};
        std::vector<std::uint32_t> actual(320*200),reference(320*200);
        for(unsigned i=0;i<actual.size();++i)actual[i]=reference[i]=(i*0x17093u)&0xffffff;
        scr::runtime::composeNativeWheelSprites(sprites,frame,state,actual);
        for(unsigned n=2;n>0;--n) {
            const auto pointer=state.spritePointers[n-1];const auto position=f::word(ram.data(),pointer),control=f::word(ram.data(),pointer+2);
            const unsigned start=(position>>8)|((control&4)<<6),end=(control>>8)|((control&2)<<7);
            for(unsigned row=0;row<((end-start)&511);++row)for(unsigned bit=0;bit<16;++bit) {
                const int x=int(((position&255)<<1)|(control&1))-128+int(bit),y=int(start)+int(row)-f::word(ram.data(),0x69ec4);
                if(x<0||x>=320||y<0||y>=200)continue;
                unsigned index=0;for(unsigned plane=0;plane<2;++plane)
                    index|=((f::word(ram.data(),pointer+4+4*row+2*plane)>>(15-bit))&1)<<plane;
                if(!index)continue;const auto colour=f::word(ram.data(),0x69eb4+2*index);
                reference[unsigned(y)*320+unsigned(x)]=(((colour>>8)&15)*17u<<16)|(((colour>>4)&15)*17u<<8)|((colour&15)*17u);
            }
        }
        if(actual!=reference)throw std::runtime_error("Wheel sprite pixel or priority mismatch");
        ++pixelCases;pixelsCompared+=actual.size();
    }
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return readByte(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1){fail("Odd word read",a);return 0;}return (unsigned(readByte(a))<<8)|readByte(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){writeByte(a,std::uint8_t(v));}
void m68k_write_memory_16(unsigned a,unsigned v) {
    if(a&1){fail("Odd word write",a);return;}
    if(in(a,0xe7f0,0xe800))pointerWrites.push_back({a,v&65535});
    writeByte(a,std::uint8_t(v>>8));writeByte(a+1,std::uint8_t(v));
}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try {
    if(argc<2||(argc-2)%2)throw std::runtime_error("Usage: wheel_animation_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Original payload size mismatch");
    std::copy(payload.begin(),payload.end(),ram.begin()+0xe700);checkOriginalCode(payload);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(int arg=2;arg<argc;arg+=2) {
        const auto capture=readFile(argv[arg+1]);if(capture.size()!=ram.size())throw std::runtime_error("Wheel seed size mismatch");
        std::copy(capture.begin(),capture.end(),ram.begin());checkOriginalCode(payload);
        check(f::vblankEntry);check(f::graphicsEntry);
        const auto sprites=scr::runtime::decodeNativeWheelSprites(ram.data(),ram.size());
        checkSpritePositions(sprites);checkSpritePixels(sprites);
    }
    // Synthetic pointers are numbers only. This routine never reads sprite
    // images, and writing arbitrary pointer words covers both copper halves.
    for(unsigned i=0;i<12;++i)f::putLong(ram.data(),0x6a4ac+4*(37+i),random32());
    for(unsigned phase=0;phase<3;++phase)for(unsigned reverse=0;reverse<2;++reverse)
        for(unsigned speed=0;speed<256;++speed)for(unsigned accumulator=0;accumulator<256;++accumulator) {
            setCase(std::uint8_t(speed),std::uint8_t(accumulator),std::uint8_t(phase),reverse?0xffff:0,
                (accumulator&1)?125:126,(speed&1)?0x8000:0x7fff);
            check(f::graphicsEntry);
        }
    for(unsigned display=0;display<256;++display)for(unsigned disabled=0;disabled<256;++disabled) {
        setCase(255,255,std::uint8_t(display%3),disabled&1?0x8000:0x7fff);
        ram[0x5d724]=std::uint8_t(display);ram[0x1bbb9]=std::uint8_t(disabled);
        ram[0x616d8]=std::uint8_t(display);ram[0x616da]=std::uint8_t(disabled);
        check(f::vblankEntry);
    }
    for(unsigned height=0;height<65536;++height) {
        setCase(0,0,std::uint8_t(height%3),std::uint16_t(height),std::uint16_t(height),std::uint16_t(~height));
        check(f::graphicsEntry);
    }
    for(unsigned phase=0;phase<256;++phase)for(auto velocity:{0u,0x7fffu,0x8000u,0xffffu}) {
        setCase(0,0,std::uint8_t(phase),std::uint16_t(velocity));check(f::rotationEntry);
    }
    // Evolving 50 Hz sequence spans acceleration, reverse, airborne coast,
    // display freezes, fade waits and race exits without resetting the phase.
    setCase(0,0,0,0);
    for(unsigned tick=0;tick<4096;++tick)for(unsigned vblank=0;vblank<6;++vblank) {
        const auto speed=std::uint8_t(tick<512?tick/2:tick<1024?255-(tick-512)/2:tick%256);
        auto inputs=f::readInputs(ram.data());inputs.speedHigh=speed;
        inputs.localForwardVelocity=tick&0x200?0xffff:0;
        inputs.disabled=tick%157<7?0x80:0;inputs.displayUpdate=tick%173<9?0x41:0x80;
        inputs.height={{std::uint16_t(101+tick%51),std::uint16_t(101+(tick+13)%51)}};
        f::writeInputs(ram.data(),inputs);check(f::vblankEntry);
    }
    initializeSyntheticSprites();const auto sprites=scr::runtime::decodeNativeWheelSprites(ram.data(),ram.size());
    for(unsigned variant=0;variant<4096;++variant) {
        for(unsigned id=37;id<49;++id) {
            f::putWord(ram.data(),0x6a16c+16*id+10,std::uint16_t(101+random32()%35));
            const auto address=f::longword(ram.data(),0x6a4ac+4*id);
            ram[address+1]=std::uint8_t(random32());ram[address+3]=std::uint8_t(random32()&1);
        }
        checkSpritePositions(sprites);
        if(variant%64==0)checkSpritePixels(sprites);
    }
    unsigned rejected=0;try{scr::WheelAnimationState invalidState;invalidState.phase=3;
        (void)scr::updateWheelGraphics({}, {},invalidState);}catch(const std::invalid_argument&){++rejected;}
    if(rejected!=1)throw std::runtime_error("Invalid wheel animation phase accepted");
    std::cout<<"{\"cases\":"<<cases<<",\"mismatches\":0,\"whole_ram_bytes_compared\":"<<cases*ram.size()
        <<",\"graphics_cases\":"<<graphicsCases<<",\"vblank_prefix_cases\":"<<prefixCases
        <<",\"rotation_cases\":"<<rotationCases<<",\"wheel_update_cases\":"<<updatedCases<<",\"carry_cases\":"<<carryCases
        <<",\"sprite_position_cases\":"<<positionCases<<",\"sprite_pixel_cases\":"<<pixelCases<<",\"pixels_compared\":"<<pixelsCompared
        <<",\"selected_sprites\":[";
    for(unsigned i=0;i<12;++i){if(i)std::cout<<',';std::cout<<selectedSprites[i];}
    std::cout<<"],\"total_instructions\":"<<totalInstructions<<",\"maximum_instructions\":"<<maximumInstructions
        <<",\"ordered_pointer_writes_compared\":true,\"original_code_untouched\":true,\"invalid_inputs_rejected\":1}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
