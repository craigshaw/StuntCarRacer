#include "native_player_name.h"
#include "native_cockpit_instruments.h"
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
using namespace scr::viewer;
NativeMemory original{},baseline{},native{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned pc{},instructions{},boundary{},scannerCases{},textCases{},sequenceCases{};
std::uint64_t totalInstructions{},comparedBytes{};
bool stopped{},invalid{},loopBoundary{};
std::vector<std::uint8_t> originalGlyphs;
struct ScanState {std::array<std::uint8_t,128> keys{};std::uint8_t controls{};};
std::vector<ScanState> script;
unsigned scanIndex{};
bool scripted{};
unsigned word(const NativeMemory& m,unsigned a){return (unsigned(m.at(a))<<8)|m.at(a+1);}
void putLong(NativeMemory& m,unsigned a,unsigned v){for(unsigned n=0;n<4;++n)m.at(a+n)=std::uint8_t(v>>(24-n*8));}
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
unsigned read(unsigned a){
    if(a<original.size())return original[a];
    if(a>=stackBase&&a<stackTop)return stack[a-stackBase];
    if(a>=sentinel&&a<sentinel+4)return (a&1)?0x71:0x4e;
    if(a==0xdff00c||a==0xdff00d)return 0;
    if(a==0xbfe001||a==0xbfe201)return 255;
    invalid=true;return 0;
}
void write(unsigned a,unsigned v){
    if(a<original.size()){original[a]=std::uint8_t(v);return;}
    if(a>=stackBase&&a<stackTop){stack[a-stackBase]=std::uint8_t(v);return;}
    if(a==0xbfe201)return;
    invalid=true;
}
void hook(unsigned a){
    pc=a;
    if(scripted&&a==0x59862){
        if(scanIndex>=script.size()){invalid=true;m68k_end_timeslice();return;}
        const auto& scan=script[scanIndex++];
        std::copy(scan.keys.begin(),scan.keys.end(),original.begin()+0xead6);
        original[0x1b88e]=std::uint8_t(~scan.controls);
    }
    if(a==0x594c6)originalGlyphs.push_back(std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D0)));
    if(a==boundary||(loopBoundary&&a==0x5b744)||a==0x5b836){stopped=true;m68k_end_timeslice();return;}
    if(++instructions>3000000||a<0xe700||a>=0x724dc||(a&1)){invalid=true;m68k_end_timeslice();}
}
void cpu(unsigned entry,unsigned d0=0,unsigned d1=0,unsigned d2=0){
    invalid=stopped=false;instructions=0;originalGlyphs.clear();stack.fill(0);m68k_pulse_reset();
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_D0,d0);m68k_set_reg(M68K_REG_D1,d1);m68k_set_reg(M68K_REG_D2,d2);
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(2000000000);
    if(invalid||!stopped)throw std::runtime_error("Original name routine failed PC="+std::to_string(pc));
    totalInstructions+=instructions;
}
void compareByte(unsigned a,const char* context){
    ++comparedBytes;
    if(original[a]!=native[a])throw std::runtime_error(std::string(context)+" byte mismatch address="+
        std::to_string(a)+" original="+std::to_string(original[a])+" native="+std::to_string(native[a]));
}
void compareScanner(std::uint8_t controls){
    original[0x1b88e]=std::uint8_t(~controls);native=original;
    boundary=sentinel;loopBoundary=false;cpu(0x59862);
    const auto result=scanNativeNameInput(native,controls);
    const bool expected=(m68k_get_reg(nullptr,M68K_REG_SR)&1)==0;
    require(result.available==expected,"Input availability/carry mismatch");
    if(expected)require(result.code==std::uint16_t(m68k_get_reg(nullptr,M68K_REG_D0)),"Input encoded raw/character mismatch");
    for(unsigned a=0xead6;a<0xeb56;++a)compareByte(a,"scan");compareByte(0x1b88e,"scan");
    ++scannerCases;
}
void scanner(){
    for(unsigned shifted=0;shifted<5;++shifted)for(unsigned key=0;key<128;++key)for(unsigned controls=0;controls<32;++controls){
        original=baseline;std::fill_n(original.begin()+0xead6,128,std::uint8_t{});
        original[0xead6+key]=0xb3;
        if(shifted)original[0xead6+((shifted&1)?0x60:0x61)]=shifted<3?0xb3:1;
        compareScanner(std::uint8_t(controls));
    }
    std::uint32_t random=0xb560a31;
    for(unsigned variation=0;variation<4096;++variation){
        original=baseline;
        for(unsigned key=0;key<128;++key){random=random*1664525+1013904223;original[0xead6+key]=(random&7)?std::uint8_t(random>>24):0xb3;}
        if(variation&1){putLong(original,0x1b890,0x1b898);putLong(original,0x1b894,0x1b950);}
        compareScanner(std::uint8_t(variation&31));
    }
}
void text(){
    for(unsigned offset:{0xb0u,0xa0u,0xc0u})for(unsigned flag:{0u,128u})for(unsigned position:{0u,1u,11u,12u})
        for(unsigned raw=0;raw<256;++raw)for(unsigned source:{0u,0x4500u}){
            original=baseline;original[0x1bbd5]=std::uint8_t(offset);original[0x5b83c]=12;
            original[0x5b83d]=0x37;original[0x62bb5]=std::uint8_t(flag);
            for(unsigned i=0;i<256;++i)original[0x1ecab+i]=std::uint8_t(i*53+raw);
            native=original;NativeTextInputState state{std::uint8_t(offset),12,std::uint8_t(position),false};
            const auto result=applyNativeTextInputCode(native,state,std::uint16_t(source|raw));
            loopBoundary=true;boundary=sentinel;cpu(0x5b752,source|raw,position,std::uint8_t(offset+position));
            for(unsigned i=0;i<256;++i)compareByte(0x1ecab+i,"text");
            compareByte(0x5b83d,"direction");
            require(state.position==std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D1)),"Text position mismatch");
            require(result.complete==(pc==0x5b836),"Text completion mismatch");
            require(result.drewCharacter==!originalGlyphs.empty(),"Text glyph event mismatch");
            if(result.drewCharacter)require(originalGlyphs.size()==1&&originalGlyphs.front()==result.character,"Text glyph value mismatch");
            ++textCases;
        }
    // Raw directional codes and fire have no printable low byte. Ordinary
    // player-name entry ignores directions/Escape; generic directional input
    // accepts them only when the original caller sets its signed flag.
    for(unsigned code:{0x4500u,0x4c00u,0x4d00u,0x4f00u,0x4e00u,0x440du})for(unsigned flag:{0u,128u}){
        original=baseline;original[0x1bbd5]=0xb0;original[0x5b83c]=12;original[0x5b83d]=0;original[0x62bb5]=std::uint8_t(flag);
        native=original;NativeTextInputState state{0xb0,12,3,false};
        const auto result=applyNativeTextInputCode(native,state,std::uint16_t(code));
        loopBoundary=true;boundary=sentinel;cpu(0x5b752,code,3,0xb3);
        for(unsigned i=0;i<256;++i)compareByte(0x1ecab+i,"direction text");compareByte(0x5b83d,"direction");
        require(result.complete==(pc==0x5b836),"Directional finish mismatch");++textCases;
    }
}
ScanState key(unsigned raw,bool shifted=false){ScanState state{};state.keys[raw]=0xb3;if(shifted)state.keys[0x60]=0xb3;return state;}
void sequences(){
    const std::vector<std::vector<ScanState>> cases{
        {{},key(0x20),key(0x21,true),key(0x40),key(0x33),key(0x41),key(0x44)},
        {key(0x44),{},key(0x40),key(0x40),key(0x44)},
        {{},key(0x41),key(0x45),key(0x4c),key(0x20),key(0x44)},
        {{},key(0x20),key(0x20),key(0x20),key(0x20),key(0x20),key(0x20),key(0x20),key(0x20),key(0x20),key(0x20),key(0x20),key(0x20),key(0x21),key(0x41),key(0x22,true),key(0x44)},
        {{},key(0x20),ScanState{{},1},ScanState{{},2},ScanState{{},4},ScanState{{},8},ScanState{{},16}},
        {ScanState{{},16},ScanState{{},16},{},key(0x20),ScanState{{},16}}
    };
    for(unsigned player=0;player<8;++player)for(const auto& scans:cases){
        original=baseline;original[0x5eb76]=std::uint8_t(player);original[0x62bb5]=0;
        native=original;NativePlayerNameEntry entry(native);auto update=entry.begin();require(update.beganAttempt,"Missing initial attempt");
        std::vector<std::uint8_t> glyphs{'>'};
        for(const auto& scan:scans){
            std::copy(scan.keys.begin(),scan.keys.end(),native.begin()+0xead6);update=entry.update(scan.controls);
            glyphs.insert(glyphs.end(),update.characters.begin(),update.characters.end());
        }
        script=scans;scripted=true;scanIndex=0;loopBoundary=false;boundary=0x5b836;cpu(0x5b6de);scripted=false;
        require(scanIndex==scans.size(),"Original consumed an unexpected scan count");
        require(originalGlyphs.size()>=3&&originalGlyphs[0]==31&&originalGlyphs[1]==14&&originalGlyphs[2]==16,
            "Original input did not position its cursor with the text command");
        originalGlyphs.erase(originalGlyphs.begin(),originalGlyphs.begin()+3);
        if(originalGlyphs!=glyphs){
            std::string message="Complete entry glyph sequence mismatch player="+std::to_string(player)+" expected=";
            for(auto v:originalGlyphs)message+=std::to_string(v)+",";
            message+=" native=";for(auto v:glyphs)message+=std::to_string(v)+",";
            throw std::runtime_error(message);
        }
        for(unsigned i=0;i<256;++i)compareByte(0x1ecab+i,"entry");
        const bool blank=original[0x1ecab+(11-player)*16]==' ';
        require(update.complete==!blank&&update.retriedBlank==blank,"Blank-name retry mismatch");
        if(blank){
            // Re-entry preserves the old name bytes until the next completion,
            // waits for fire release, and begins afresh at position zero.
            require(update.beganAttempt&&!entry.complete(),"Blank retry did not begin another attempt");
            require(!entry.update(16).complete,"Held fire passed release gate");
            (void)entry.update(0);setNativeRawKey(native,0x20,true);(void)entry.update(0);
            setNativeRawKey(native,0x44,true);const auto done=entry.update(0);
            require(done.complete&&native[0x1ecab+(11-player)*16]=='a',"Retry did not accept a fresh nonempty name");
        }
        ++sequenceCases;
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
    if(argc!=2)throw std::invalid_argument("Usage: native_player_name_tests original-payload.bin");
    std::ifstream file(argv[1],std::ios::binary);const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    require(payload.size()==408540,"Wrong original payload size");std::copy(payload.begin(),payload.end(),baseline.begin()+0xe700);
    initializeNativeAssets(baseline);putLong(baseline,0x6a584,0x6a594);putLong(baseline,0x6a588,0x6a594);putLong(baseline,0x6a58c,0x6a594);
    baseline[0x5983e]=15;baseline[0x5983f]=16;baseline[0x5924a]=baseline[0x1bbd3]=baseline[0x1bbbc]=0;
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    scanner();text();sequences();
    std::cout<<"{\"scanner_cases\":"<<scannerCases<<",\"text_loop_cases\":"<<textCases<<",\"complete_entry_cases\":"<<sequenceCases
        <<",\"compared_bytes\":"<<comparedBytes<<",\"original_instructions\":"<<totalInstructions<<",\"mismatches\":0}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
