#include "presentation/native_opening_render.h"
#include "menus/native_opening_flow.h"
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
using Memory=scr::runtime::NativeMemory;
Memory ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000,frame=0x6a594;
unsigned stop{},pc{},instructions{},cases{};
std::uint64_t totalInstructions{},comparedPixels{};
bool invalid{},stopped{};
void word(Memory& m,unsigned a,unsigned v){m.at(a)=std::uint8_t(v>>8);m.at(a+1)=std::uint8_t(v);}
void longword(Memory& m,unsigned a,unsigned v){word(m,a,v>>16);word(m,a+2,v);}
std::uint8_t readByte(unsigned a){
    if(a<ram.size())return ram[a];
    if(a>=stackBase&&a<stackTop)return stack[a-stackBase];
    if(a>=sentinel&&a<sentinel+4)return (a&1)?0x71:0x4e;
    invalid=true;return 0;
}
void writeByte(unsigned a,std::uint8_t v){
    if(a>=stackBase&&a<stackTop){stack[a-stackBase]=v;return;}
    if(a<ram.size()){ram[a]=v;return;}invalid=true;
}
void hook(unsigned a){
    pc=a;if(a==stop){stopped=true;m68k_end_timeslice();return;}
    if((a&1)||a<0xe700||a>=0x724dc||++instructions>2000000){invalid=true;m68k_end_timeslice();}
}
void run(unsigned entry,unsigned boundary=sentinel,unsigned d0=0,unsigned d1=0,unsigned d2=0){
    stop=boundary;invalid=stopped=false;instructions=0;m68k_pulse_reset();stack.fill(0);
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_D0,d0);m68k_set_reg(M68K_REG_D1,d1);m68k_set_reg(M68K_REG_D2,d2);
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(2000000000);
    if(invalid||!stopped)throw std::runtime_error("Original opening renderer failed PC="+std::to_string(pc));
    totalInstructions+=instructions;
}
void compare(const scr::runtime::NativeMenuImage& image,const std::string& label,bool both=true){
    for(unsigned i=0;i<16;++i){
        const unsigned source=(unsigned(ram[0x23c32+2*i])<<8)|ram[0x23c33+2*i];
        auto channel=[](unsigned v){v&=7;return (2*v+(v?1:0))*17;};
        const unsigned expected=(channel(source>>8)<<16)|(channel(source>>4)<<8)|channel(source);
        if(image.palette[i]!=expected)throw std::runtime_error(label+" original menu palette mismatch");
    }
    const unsigned active=(unsigned(ram[0x6a58c])<<24)|(unsigned(ram[0x6a58d])<<16)|(unsigned(ram[0x6a58e])<<8)|ram[0x6a58f];
    for(unsigned buffer=0;buffer<(both?2u:1u);++buffer)for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x){
        unsigned expected=0;for(unsigned p=0;p<4;++p)
            if(ram[(both?frame+buffer*32000:active)+p*8000+y*40+x/8]&(128u>>(x%8)))expected|=1u<<p;
        ++comparedPixels;
        if(image.indices[y*320+x]!=expected)throw std::runtime_error(label+" mismatch buffer="+std::to_string(buffer)+
            " x="+std::to_string(x)+" y="+std::to_string(y)+" expected="+std::to_string(expected)+" actual="+std::to_string(image.indices[y*320+x]));
    }
    ++cases;
}
void exportPpm(const scr::runtime::NativeMenuImage& image,const char* path){
    std::ofstream output(path,std::ios::binary);output<<"P6\n320 200\n255\n";
    for(auto index:image.indices){const auto colour=image.palette[index];
        for(unsigned shift:{16u,8u,0u})output.put(char(colour>>shift));}
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return readByte(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1)invalid=true;return (unsigned(readByte(a))<<8)|readByte(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){writeByte(a,std::uint8_t(v));}
void m68k_write_memory_16(unsigned a,unsigned v){if(a&1)invalid=true;writeByte(a,std::uint8_t(v>>8));writeByte(a+1,std::uint8_t(v));}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc!=2)throw std::invalid_argument("Usage: native_opening_render_tests original-payload.bin");
    std::ifstream file(argv[1],std::ios::binary);
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    if(payload.size()!=408540)throw std::invalid_argument("Wrong original payload size");
    auto base=std::make_unique<Memory>();std::copy(payload.begin(),payload.end(),base->begin()+0xe700);
    scr::runtime::initializeNativeAssets(*base);
    (*base)[0x1bbbc]=0x80;(*base)[0x5d724]=0x41;(*base)[0x1c9ce]=3;(*base)[0x1c9d0]=0;
    (*base)[0x1bb6d]=(*base)[0x1bb6e]=0;(*base)[0x5924a]=0;(*base)[0x64c18]=2;
    longword(*base,0x6a584,frame);longword(*base,0x6a588,frame+32000);longword(*base,0x6a58c,frame);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned offset:{0x10u,0x14u})for(unsigned division=0;division<4;++division){
        const unsigned count=offset==0x10?2:1;
        for(unsigned selection=0;selection<=count;++selection){
            ram=*base;ram[0x5eb75]=0x80;ram[0x1c9ce]=std::uint8_t(division);
            run(0x64570,0x645ac);run(0x5b840,0x5b962,selection,offset,count);
            scr::runtime::NativeMenuRenderState menu;
            menu.menuOffset=offset;menu.count=count;menu.selected=selection;menu.division=division;menu.menuStrings=true;
            compare(scr::runtime::makeNativeMenuImage(*base,menu),"Opening choice",false);
            ram[0x1bbca]=0x10;run(0x5b87e,0x5b962);menu.confirmed=true;
            compare(scr::runtime::makeNativeMenuImage(*base,menu),"Confirmed opening choice",false);
        }
    }
    const unsigned menuCases=cases;
    for(unsigned division=0;division<4;++division){
        ram=*base;ram[0x1c9ce]=std::uint8_t(division);run(0x64570,0x645ac);
        run(0x5b648,0x5b694);
        scr::runtime::NativeOpeningNameScreen image(ram);compare(image.image(),"Initial name frame");
        run(0x5b694,0x5b6f6);image.beginAttempt();compare(image.image(),"Initial input arrow",false);
        for(unsigned ch=32;ch<127;++ch){
            run(0x594c6,sentinel,ch);image.drawCharacter(ch);compare(image.image(),"Typed glyph "+std::to_string(ch),false);
            run(0x594c6,sentinel,127);image.drawCharacter(127);compare(image.image(),"Erased glyph "+std::to_string(ch),false);
        }
        for(unsigned retry=0;retry<3;++retry){
            run(0x64c0e);run(0x5b694,0x5b6f6);image.beginAttempt();compare(image.image(),"Blank retry",false);
        }
        for(unsigned i=0;i<12;++i){
            const unsigned ch="Craig.9/ abcZ"[i];run(0x594c6,sentinel,ch);image.drawCharacter(ch);compare(image.image(),"Full name",false);
        }
        if(division==3){
            run(0x5b648,0x5b694);scr::runtime::NativeOpeningNameScreen clean(ram);
            run(0x5b694,0x5b6f6);clean.beginAttempt();
            for(unsigned ch:std::string("Craig")){run(0x594c6,sentinel,ch);clean.drawCharacter(ch);compare(clean.image(),"Fresh typed name",false);}
            exportPpm(clean.image(),"analysis/opening-render/name.ppm");
        }
    }
    const unsigned nameCases=cases-menuCases;
    auto state=std::make_unique<Memory>(*base);
    for(unsigned additional=1;additional<=7;++additional)for(unsigned variation=0;variation<16;++variation){
        *state=*base;(*state)[0x5eb76]=std::uint8_t(additional);(*state)[0x5eb75]=0;
        (*state)[0x1c9ce]=std::uint8_t(variation&3);
        (*state)[0x1bbcb]=(*state)[0x5eb7a]=(*state)[0x64530]=(*state)[0x57c67]=0;
        for(unsigned i=0;i<12;++i){(*state)[0x1c9c2+i]=std::uint8_t(i);
            (*state)[0x1c9f6+i]=std::uint8_t(variation<4?0:((i+variation)*7)%100);
            (*state)[0x1c9de+i]=std::uint8_t(variation<4?0:(i+variation)%25);
            (*state)[0x1c9ea+i]=std::uint8_t(variation<4?0:(i*3+variation)%25);
            if(variation&8)for(unsigned j=0;j<12;++j)(*state)[0x1ecab+16*i+j]=std::uint8_t("AZaz/9 .0:Cx"[(i+j)%12]);
        }
        ram=*state;run(0x64570,0x645ac);run(0x646e8,0x64828);
        scr::runtime::prepareNativeOpeningLocalStandings(*state);
        auto image=scr::runtime::makeNativeOpeningStandingsImage(*state);compare(image,"Local table "+std::to_string(additional+1),false);
        if(additional==7&&variation==0)exportPpm(image,"analysis/opening-render/standings.ppm");
    }
    std::cout<<"{\"mismatches\":0,\"cases\":"<<cases<<",\"compared_pixels\":"<<comparedPixels<<
        ",\"menu_cases\":"<<menuCases<<",\"name_cases\":"<<nameCases<<",\"standings_cases\":"<<cases-menuCases-nameCases<<
        ",\"original_instructions\":"<<totalInstructions<<",\"captured_state_used\":false,\"original_code_unmodified\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
