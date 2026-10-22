#include "native_multiplayer_render.h"
#include "native_race_initialization.h"
#include "native_opening_flow.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace scr::viewer;
NativeMemory original{},baseline{},native{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000,frame=0x6a594;
unsigned pc{},boundary{},instructions{},cases{},matchupCases{},tableCases{};
std::uint64_t totalInstructions{},comparedPixels{};
std::uint64_t comparedPaletteWords{};
std::filesystem::path outputDirectory;
bool stopped{},invalid{};
unsigned read(unsigned a){
    if(a<original.size())return original[a];if(a>=stackBase&&a<stackTop)return stack[a-stackBase];
    if(a>=sentinel&&a<sentinel+4)return (a&1)?0x71:0x4e;invalid=true;return 0;
}
void write(unsigned a,unsigned v){
    if(a<original.size()){original[a]=std::uint8_t(v);return;}
    if(a>=stackBase&&a<stackTop){stack[a-stackBase]=std::uint8_t(v);return;}invalid=true;
}
void longword(NativeMemory& m,unsigned a,unsigned v){for(unsigned i=0;i<4;++i)m.at(a+i)=std::uint8_t(v>>(24-i*8));}
void hook(unsigned a){
    pc=a;if(a==boundary){stopped=true;m68k_end_timeslice();return;}
    if(++instructions>2000000||a<0xe700||a>=0x724dc||(a&1)){invalid=true;m68k_end_timeslice();}
}
void cpu(unsigned entry,unsigned stop=sentinel,unsigned d0=0,unsigned d1=0,unsigned d2=0){
    invalid=stopped=false;instructions=0;boundary=stop;stack.fill(0);m68k_pulse_reset();
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_D0,d0);m68k_set_reg(M68K_REG_D1,d1);m68k_set_reg(M68K_REG_D2,d2);
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(2000000000);
    if(invalid||!stopped)throw std::runtime_error("Original multiplayer screen failed PC="+std::to_string(pc));totalInstructions+=instructions;
}
void compare(const NativeMenuImage& image,const std::string& stage){
    for(unsigned i=0;i<16;++i){
        const unsigned colour=(unsigned(original[0x23c32+2*i])<<8)|original[0x23c33+2*i];
        auto channel=[](unsigned value){value&=7;return (2*value+(value?1:0))*17;};
        const unsigned expected=(channel(colour>>8)<<16)|(channel(colour>>4)<<8)|channel(colour);
        if(image.palette[i]!=expected)throw std::runtime_error(stage+" palette mismatch");++comparedPaletteWords;
    }
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x){
        unsigned expected=0;for(unsigned p=0;p<4;++p)if(original[frame+p*8000+y*40+x/8]&(128u>>(x%8)))expected|=1u<<p;
        ++comparedPixels;if(image.indices[y*320+x]!=expected)throw std::runtime_error(stage+" pixel mismatch x="+std::to_string(x)+" y="+std::to_string(y)+
            " expected="+std::to_string(expected)+" actual="+std::to_string(image.indices[y*320+x]));
    }++cases;
}
unsigned bcd(unsigned v){return ((v/10)<<4)|(v%10);}
void exportImage(const NativeMenuImage& image,const std::string& name){
    if(outputDirectory.empty())return;
    std::ofstream output(outputDirectory/(name+".ppm"),std::ios::binary);output<<"P6\n320 200\n255\n";
    for(auto index:image.indices)for(unsigned shift:{16u,8u,0u})output.put(char(image.palette[index]>>shift));
    if(!output)throw std::runtime_error("Cannot export multiplayer screen");
}
void state(unsigned additional,unsigned variation,unsigned context,unsigned offset){
    native=baseline;native[0x5eb76]=std::uint8_t(additional);native[0x1c9d0]=std::uint8_t(context);native[0x1bb6e]=std::uint8_t(offset);
    native[0x1c9ce]=native[0x1bb5f]=std::uint8_t(variation&3);native[0x5eb7b]=std::uint8_t(variation%(2*(additional+1)));
    native[0x5eb78]=std::uint8_t(additional+1);native[0x5eb77]=std::uint8_t(variation&1);native[0x1ca35]=std::uint8_t(variation%(additional+1));
    native[0x57c67]=std::uint8_t(variation%4);native[0x1ca33]=std::uint8_t(variation%8);
    native[0x1bc5b]=std::uint8_t(11-additional);native[0x1bc59]=12;
    native[0x1ca27]=std::uint8_t(11-variation%(additional+1));native[0x5eb79]=native[0x1ca27];
    native[0x1ca28]=std::uint8_t(variation&1);native[0x1ca25]=variation&2?native[0x1ca27]:native[0x1ca28];
    native[0x1ca26]=variation&4?native[0x1ca27]:native[0x1ca28];
    native[0x59140]=1;native[0x59141]=2;native[0x1ca24]=variation&1?native[0x1ca27]:0;
    native[0x1ca23]=variation&2?std::uint8_t(11-(variation+1)%(additional+1)):0;
    for(unsigned id=0;id<12;++id){
        native[0x1ecaa+16*id]=' ';
        for(unsigned i=0;i<12;++i)native[0x1ecab+16*id+i]=std::uint8_t(variation&8?"AZaz /9.0:Cx"[(id+i)%12]:(i<((variation+id)%12)?'A'+(id+i)%26:' '));
        native[0x1ca0e+id]=std::uint8_t(id);native[0x1c9f6+id]=std::uint8_t((id*3+variation)%32);
        native[0x1c9de+id]=std::uint8_t((id+variation)%20);native[0x1c9ea+id]=std::uint8_t((id*2+variation)%20);
        native[0x1ca02+id]=std::uint8_t(2*native[0x1c9de+id]+native[0x1c9ea+id]);
    }
    for(unsigned id=0;id<24;++id){native[0x1c908+id]=std::uint8_t((id+variation)%10);native[0x1c920+id]=std::uint8_t(bcd((id*7+variation*13)%60));
        native[0x1c938+id]=std::uint8_t(bcd((id*17+variation*19)%100));}
    for(unsigned record=0;record<16;++record)for(unsigned table:{0x7a61au,0x7a71au}){
        native[table+record*16+12]=std::uint8_t((record+variation)%10);
        native[table+record*16+13]=std::uint8_t(bcd((record*7+variation)%60));
        native[table+record*16+14]=std::uint8_t(bcd((record*9+variation)%100));
    }
}
void matchups(){
    for(unsigned additional=1;additional<=7;++additional)for(unsigned variation=0;variation<16;++variation)
    for(unsigned context=0;context<2;++context)for(unsigned flags:{0u,0x40u,0x80u,0xc0u,1u,0x41u,0x81u,0xc1u})
    for(auto screen:{NativeMultiplayerScreen::Matchup,NativeMultiplayerScreen::RoundResults}){
        state(additional,variation,context,0);native[0x1bbb1]=std::uint8_t(flags);original=native;
        cpu(0x64570,0x645ac);cpu(screen==NativeMultiplayerScreen::RoundResults?0x6465c:0x64664,0x64828);
        native=original;const auto unchanged=std::make_unique<NativeMemory>(native);
        const auto image=makeNativeMultiplayerImage(native,screen);
        compare(image,"Matchup count="+std::to_string(additional+1)+" variation="+std::to_string(variation)+
            " screen="+std::to_string(unsigned(screen))+" flags="+std::to_string(flags));
        if(additional==7&&variation==8&&context==1&&!flags)exportImage(image,"matchup-"+std::to_string(unsigned(screen)));
        if(native!=*unchanged)throw std::runtime_error("Multiplayer matchup changed gameplay state");
        applyNativeMultiplayerScreenState(native,screen);
        if(native[0x1bb6e]!=original[0x1bb6e])throw std::runtime_error("Multiplayer matchup final offset mismatch");++matchupCases;
    }
}
void tables(){
    for(unsigned additional=1;additional<=7;++additional)for(unsigned variation=0;variation<16;++variation)
    for(unsigned context=0;context<2;++context)for(unsigned offset:{0u,4u})
    for(auto screen:{NativeMultiplayerScreen::Standings,NativeMultiplayerScreen::RaceResults,NativeMultiplayerScreen::TrackResults}){
        state(additional,variation,context,offset);native[0x1bbcb]=screen==NativeMultiplayerScreen::Standings?0:128;
        native[0x5eb7a]=screen==NativeMultiplayerScreen::TrackResults?128:0;
        original=native;cpu(0x64570,0x645ac);cpu(0x646e8,0x64828);
        // State preparation owns sorting and the once-per-track bonus awards.
        // Render from the original's prepared values without repeating effects.
        native=original;native[0x1bb6e]=std::uint8_t(offset);
        const auto unchanged=std::make_unique<NativeMemory>(native);
        const auto image=makeNativeMultiplayerImage(native,screen,~0u,~0u,offset);
        compare(image,"Table count="+std::to_string(additional+1)+" variation="+std::to_string(variation)+
            " screen="+std::to_string(unsigned(screen))+" offset="+std::to_string(offset));
        if(variation==7&&context==1&&!offset)exportImage(image,"table-"+std::to_string(unsigned(screen))+"-players-"+std::to_string(additional+1));
        if(native!=*unchanged)throw std::runtime_error("Multiplayer renderer changed gameplay state");
        applyNativeMultiplayerScreenState(native,screen,offset);
        if(native[0x1bb6e]!=original[0x1bb6e])throw std::runtime_error("Multiplayer final text offset mismatch");++tableCases;
    }
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return read(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1)invalid=true;return (read(a)<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){write(a,v);}
void m68k_write_memory_16(unsigned a,unsigned v){if(a&1)invalid=true;write(a,v>>8);write(a+1,v);}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc<2||argc>3)throw std::invalid_argument("Usage: native_multiplayer_render_tests original-payload.bin [image-directory]");
    if(argc==3){outputDirectory=argv[2];std::filesystem::create_directories(outputDirectory);}
    std::ifstream file(argv[1],std::ios::binary);const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    if(payload.size()!=408540)throw std::invalid_argument("Wrong original payload size");std::copy(payload.begin(),payload.end(),baseline.begin()+0xe700);
    initializeNativeAssets(baseline);initializeNativeGameData(baseline);initializeNativeOpeningSession(baseline);
    baseline[0x1bbbc]=128;baseline[0x5d724]=0x41;baseline[0x64530]=baseline[0x5eb75]=baseline[0x57c3c]=baseline[0x5924a]=0;
    baseline[0x59843]=baseline[0x59844]=baseline[0x1bb6d]=baseline[0x1bb6e]=0;baseline[0x64c18]=2;
    longword(baseline,0x6a584,frame);longword(baseline,0x6a588,frame+32000);longword(baseline,0x6a58c,frame);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);tables();matchups();
    std::cout<<"{\"mismatches\":0,\"pixel_cases\":"<<cases<<",\"compared_pixels\":"<<comparedPixels<<
        ",\"compared_palette_words\":"<<comparedPaletteWords<<
        ",\"table_cases\":"<<tableCases<<",\"matchup_cases\":"<<matchupCases<<",\"original_instructions\":"<<totalInstructions<<
        ",\"original_code_unmodified\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
