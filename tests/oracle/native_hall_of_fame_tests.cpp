#include "native_hall_of_fame.h"
#include "native_race_initialization.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Memory=scr::viewer::NativeMemory;
Memory ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000,frame=0x6a594;
unsigned pc{},instructions{},cases{},vblanks{};
std::uint64_t totalInstructions{},comparedPixels{},comparedPaletteWords{};
bool invalid{},stopped{};
std::string failure;
bool in(unsigned a,unsigned begin,unsigned end){return a>=begin&&a<end;}
void fail(const char* reason,unsigned a){invalid=true;if(failure.empty())failure=std::string(reason)+" address="+std::to_string(a)+" PC="+std::to_string(pc);}
bool code(unsigned a){return in(a,0x5fbc6,0x5fde0)||in(a,0x6444e,0x64530)||in(a,0x1ba70,0x1bace)||
    in(a,0x593f8,0x59450)||in(a,0x594c6,0x5982e)||in(a,0x5ec76,0x5ec92)||
    in(a,0x5deb6,0x5decc)||in(a,0x5df00,0x5df08)||in(a,0x5ed40,0x5edba)||
    in(a,0x5edee,0x5ee04)||in(a,0x5ef2e,0x5ef42)||in(a,0x6770a,0x6775a);}
void hook(unsigned a){
    pc=a;
    if(invalid){m68k_end_timeslice();return;}
    if(a==0x5fdc8){stopped=true;m68k_end_timeslice();return;}
    // Supply the interrupt's two countdown effects at the original spin waits.
    // All decompression, palette and character instructions execute unchanged.
    if((a==0x64516&&ram[0x616d8])||(a==0x64524&&ram[0x616da])){
        if(ram[0x616d8])--ram[0x616d8];if(ram[0x616da])--ram[0x616da];++vblanks;
    }
    if((a&1)||!code(a)||++instructions>2000000){fail("Unexpected instruction",a);m68k_end_timeslice();}
}
std::uint8_t read(unsigned a){
    if(a<ram.size())return ram[a];
    if(in(a,stackBase,stackTop))return stack[a-stackBase];
    if(in(a,sentinel,sentinel+4))return (a&1)?0x71:0x4e;
    fail("Unexpected read",a);return 0;
}
void write(unsigned a,std::uint8_t v){
    if(in(a,stackBase,stackTop)){stack[a-stackBase]=v;return;}
    if(a<ram.size()){ram[a]=v;return;}
    fail("Unexpected write",a);
}
unsigned word(const Memory& m,unsigned a){return (unsigned(m.at(a))<<8)|m.at(a+1);}
void word(Memory& m,unsigned a,unsigned v){m.at(a)=std::uint8_t(v>>8);m.at(a+1)=std::uint8_t(v);}
void longword(Memory& m,unsigned a,unsigned v){word(m,a,v>>16);word(m,a+2,v);}
void run(){
    invalid=stopped=false;failure.clear();instructions=0;stack.fill(0);m68k_pulse_reset();
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_PC,0x5fbc6);m68k_execute(2000000000);
    if(invalid||!stopped)throw std::runtime_error("Original Hall of Fame failed: "+failure);
    totalInstructions+=instructions;
}
void compare(const scr::viewer::NativeMenuImage& image,unsigned variation){
    for(unsigned i=0;i<16;++i){
        const unsigned source=word(ram,0xe85a+i*2);
        auto channel=[](unsigned v){v&=7;return (2*v+(v?1:0))*17;};
        const unsigned expected=(channel(source>>8)<<16)|(channel(source>>4)<<8)|channel(source);
        ++comparedPaletteWords;
        if(image.palette[i]!=expected)throw std::runtime_error("Hall of Fame palette mismatch");
    }
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x){
        unsigned expected=0;for(unsigned p=0;p<4;++p)
            if(ram[frame+32000+p*8000+y*40+x/8]&(128u>>(x%8)))expected|=1u<<p;
        ++comparedPixels;
        if(image.indices[y*320+x]!=expected)throw std::runtime_error("Hall of Fame pixel mismatch variation="+std::to_string(variation)+
            " context="+std::to_string(ram[0x1c9d0])+" x="+std::to_string(x)+" y="+std::to_string(y)+
            " expected="+std::to_string(expected)+" actual="+std::to_string(image.indices[y*320+x]));
    }
    ++cases;
}
unsigned bcd(unsigned v){return ((v/10)<<4)|(v%10);}
void records(Memory& m,unsigned variation){
    static constexpr char names[][13]={"            ","Craig       ","ABCDEFGHIJKL","abcdefghijkl","A.0:/ 9Zz xY","0123456789  "};
    for(unsigned bank=0;bank<2;++bank)for(unsigned type=0;type<2;++type)for(unsigned track=0;track<8;++track){
        const unsigned address=(type?0x7a71a:0x7a61a)+bank*128+track*16;
        for(unsigned i=0;i<12;++i)m[address+i]=std::uint8_t(names[(variation+track+bank*3+type)%6][i]);
        unsigned mode=(variation+track+bank+type)%11;
        m[address+12]=std::uint8_t(mode==0?9:mode==1?0:mode-2);
        m[address+13]=std::uint8_t(mode==1?0:bcd((variation*13+track*7+type*23+bank)%60));
        m[address+14]=std::uint8_t(bcd((variation*19+track*9+type*37+bank)%100));
        m[address+15]=std::uint8_t(variation+bank*97+track*7+type*41);
    }
}
void exportPpm(const scr::viewer::NativeMenuImage& image,const char* path){
    std::ofstream output(path,std::ios::binary);if(!output)throw std::runtime_error("Cannot export Hall of Fame image");
    output<<"P6\n320 200\n255\n";
    for(auto index:image.indices)for(unsigned shift:{16u,8u,0u})output.put(char(image.palette[index]>>shift));
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return read(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1)fail("Odd read",a);return (unsigned(read(a))<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){write(a,std::uint8_t(v));}
void m68k_write_memory_16(unsigned a,unsigned v){if(a&1)fail("Odd write",a);write(a,std::uint8_t(v>>8));write(a+1,std::uint8_t(v));}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc<2||argc>3)throw std::invalid_argument("Usage: native_hall_of_fame_tests original-payload.bin [image.ppm]");
    std::ifstream file(argv[1],std::ios::binary);const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    if(payload.size()!=408540)throw std::invalid_argument("Wrong original payload size");
    auto base=std::make_unique<Memory>();std::copy(payload.begin(),payload.end(),base->begin()+0xe700);
    scr::viewer::initializeNativeAssets(*base);scr::viewer::initializeNativeGameData(*base);
    (*base)[0x1bbbc]=0x80;(*base)[0x5d724]=0x41;(*base)[0x1bb6d]=(*base)[0x1bb6e]=0;
    (*base)[0x59843]=(*base)[0x59844]=(*base)[0x5924a]=0;
    longword(*base,0x6a584,frame);longword(*base,0x6a588,frame+32000);longword(*base,0x6a58c,frame);
    std::copy_n(base->begin()+0x23c32,32,base->begin()+0xe83a);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    auto state=std::make_unique<Memory>();
    for(unsigned context:{0u,1u,2u,128u,255u})for(unsigned variation=0;variation<128;++variation){
        *state=*base;(*state)[0x1c9d0]=std::uint8_t(context);records(*state,variation);ram=*state;
        const auto image=scr::viewer::makeNativeHallOfFameImage(*state);
        run();compare(image,variation);
        if(argc==3&&context==0&&variation==3)exportPpm(image,argv[2]);
        auto unchanged=std::make_unique<Memory>(*base);(*unchanged)[0x1c9d0]=std::uint8_t(context);records(*unchanged,variation);
        if(*state!=*unchanged)throw std::runtime_error("Hall of Fame renderer modified gameplay memory");
    }
    std::cout<<"{\"mismatches\":0,\"cases\":"<<cases<<",\"compared_pixels\":"<<comparedPixels<<
        ",\"compared_palette_words\":"<<comparedPaletteWords<<",\"original_instructions\":"<<totalInstructions<<
        ",\"fade_vblanks\":"<<vblanks<<",\"captured_state_used\":false,\"original_code_unmodified\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
