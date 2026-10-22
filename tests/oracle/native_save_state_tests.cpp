#include "native_save_state.h"
#include "native_opening_flow.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using namespace scr::viewer;
NativeMemory original{},native{},baseline{},encoded{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned entry{},stopAt{},pc{},instructions{},maximumInstructions{};
bool stopped{},invalid{},carry{};
std::uint64_t cases{},comparedBytes{},instructionCount{},codecCases{},transferCases{},transactionCases{},replayCases{},roundtripCases{},rejectionCases{};
std::uint32_t randomState=0x53415645;
unsigned next(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
unsigned read(unsigned address){
    if(address<original.size())return original[address];
    if(address>=stackBase&&address<stackTop)return stack[address-stackBase];
    if(address>=sentinel&&address<sentinel+4){constexpr std::uint8_t stop[]{0x4e,0x72,0x27,0};return stop[address-sentinel];}
    if((address>=0xbfd000&&address<0xbff000)||(address>=0xdff000&&address<0xdff200))return 0;
    invalid=true;return 0;
}
void write(unsigned address,unsigned value){
    if(address<original.size()){original[address]=std::uint8_t(value);return;}
    if(address>=stackBase&&address<stackTop){stack[address-stackBase]=std::uint8_t(value);return;}
    if((address>=0xbfd000&&address<0xbff000)||(address>=0xdff000&&address<0xdff200))return;
    invalid=true;
}
void returnFromCall(){
    const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);
    const unsigned target=(read(sp)<<24)|(read(sp+1)<<16)|(read(sp+2)<<8)|read(sp+3);
    m68k_set_reg(M68K_REG_SP,sp+4);m68k_set_reg(M68K_REG_PC,target);
}
void hook(unsigned address){
    pc=address;
    if(address==stopAt||address==sentinel){stopped=true;carry=(m68k_get_reg(nullptr,M68K_REG_SR)&1)!=0;m68k_end_timeslice();return;}
    if(entry==0x5bbda&&address==0x5ba3e){returnFromCall();return;}
    if(entry==0x5bb58){
        if(address==0x612be){m68k_set_reg(M68K_REG_SR,m68k_get_reg(nullptr,M68K_REG_SR)&~4u);returnFromCall();return;}
        if(address==0x64532){returnFromCall();return;}
    }
    if(address<0xe700||address>=0x724dc||++instructions>20000000)invalid=true;
    if(invalid)m68k_end_timeslice();
}
void execute(unsigned start,unsigned d0=0,unsigned stop=sentinel){
    entry=start;stopAt=stop;instructions=0;invalid=stopped=carry=false;stack.fill(0);
    m68k_pulse_reset();for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_D0,d0);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_PC,start);
    while(!stopped&&!invalid)m68k_execute(1000000);
    if(invalid||!stopped){std::ostringstream message;message<<"Original save routine failed at0x"<<std::hex<<pc;throw std::runtime_error(message.str());}
    maximumInstructions=std::max(maximumInstructions,instructions);instructionCount+=instructions;
}
void compare(const char* label){
    for(unsigned address=0;address<original.size();++address){
        ++comparedBytes;
        if(original[address]!=native[address]){
            std::ostringstream message;message<<label<<" case="<<cases<<" address=0x"<<std::hex<<address
                <<" original="<<unsigned(original[address])<<" native="<<unsigned(native[address]);
            throw std::runtime_error(message.str());
        }
    }
    ++cases;
}
void prepare(unsigned variation=0){
    original=baseline;original[0x5eb79]=11;initializeNativeGameData(original);initializeNativeOpeningSession(original);
    original[0x1c9ce]=std::uint8_t(variation%4);original[0x1c9d0]=(variation&4)?11:0;original[0x1c9cf]=std::uint8_t(variation%11);
    for(unsigned i=0;i<12;++i)original[0x1ed5b+i]=std::uint8_t(i<1+variation%12?'A'+(i+variation)%26:' ');
    for(unsigned i=12;i>1;--i)std::swap(original[0x1c9c2+i-1],original[0x1c9c2+next()%i]);
    for(unsigned a=0x625a2;a<0x625a7;++a)original[a]=variation?std::uint8_t(next()):0;
    original[0x60c86]=std::uint8_t(variation);original[0x1ca34]=0;
    for(unsigned i=0;i<512;++i)original[0x7a41a+i]=std::uint8_t(next());
}
bool codec(std::uint8_t mode){
    native=original;const bool accepted=encodeDecodeNativeLeague(native,mode);execute(0x609ae,mode);
    require(accepted!=carry,"League codec carry result differs");compare("league-codec");++codecCases;return accepted;
}
void transfer(std::uint8_t direction){
    native=original;const bool accepted=syncNativeSaveTransfer(native,direction);execute(0x5f374,direction);
    require(accepted!=carry,"Save transfer carry result differs");compare("save-transfer");++transferCases;
}
void transactions(){
    for(unsigned variation=0;variation<24;++variation)for(unsigned save:{0u,1u}){
        prepare(variation);original[0x1ca34]=std::uint8_t(next());
        for(unsigned i=0;i<256;++i)original[0x7a01a+i]=std::uint8_t(next());
        native=original;beginNativeSaveTransaction(native,save!=0);execute(0x5bbda,save,0x5bc22);compare("begin-save-transaction");++transactionCases;
        for(unsigned i=0;i<256;++i)original[0x7a01a+i]=std::uint8_t(next());
        original[0x1ca34]=std::uint8_t(next());native=original;restoreNativeSaveTransaction(native);
        execute(0x5bca8);compare("restore-save-transaction");++transactionCases;
    }
}
void league(){
    for(unsigned variation=0;variation<48;++variation){
        prepare(variation);codec(std::uint8_t(variation%3));encoded=original;
        for(unsigned players:{0u,1u,7u}){
            original=encoded;original[0x5eb76]=std::uint8_t(players);
            for(unsigned i=0;i<27;++i)original[0x1c9b6+i]=std::uint8_t(next());
            codec(std::uint8_t(0x80+(variation%3)));
        }
        original=encoded;original[0x7a01a+(variation%27)]^=0x80;codec(0x80);
        original=encoded;original[0x7a03a+(variation%5)]^=1;codec(0x80);
        original=encoded;original[0x1ca34]=std::uint8_t(variation&0x7f);codec(0x80);
    }
}
void transfers(){
    for(unsigned kind:{0u,1u,0x40u})for(unsigned variation=0;variation<16;++variation){
        prepare(variation);original[0x1bb6b]=std::uint8_t(kind);original[0x1ca31]=1;original[0x1bb94]=0;
        original[0x5eb76]=3;
        for(unsigned a=0x7a61a;a<0x7a81a;++a)original[a]=std::uint8_t(next());
        transfer(1);encoded=original;
        original[0x1ca31]=0;transfer(0);
        if(kind){original=encoded;original[0x1ca31]=0;original[0x7a439]^=1;transfer(0);}
        original=encoded;original[0x1ca31]=0;transfer(1);
        original=encoded;original[0x1bb94]=0x81;transfer(1);
        original=encoded;original[0x1bb6b]=0x80;transfer(1);
    }
}
void replays(){
    for(unsigned variation=0;variation<24;++variation)for(unsigned mode=0;mode<4;++mode){
        prepare(variation);require(encodeDecodeNativeLeague(original,0),"Replay fixture encode failed");
        if(mode==1)original[0x7a03a]^=1;
        if(mode==2)original[0x5eb76]=3;
        if(mode==3)original[0x1ca34]=0;
        native=original;(void)prepareNativeReplay(native);execute(0x5bb58,0,0x5bc9c);compare("replay-career-transition");++replayCases;
    }
}
void hostPayloads(){
    auto source=std::make_unique<NativeMemory>(),destination=std::make_unique<NativeMemory>(),before=std::make_unique<NativeMemory>();
    for(unsigned variation=0;variation<12;++variation)for(auto kind:{NativeSaveKind::Career,NativeSaveKind::Multiplayer,NativeSaveKind::Records}){
        prepare(variation);*source=original;
        if(kind==NativeSaveKind::Multiplayer){
            (*source)[0x5eb76]=std::uint8_t(1+variation%7);
            for(unsigned player=0;player<8;++player)for(unsigned character=0;character<12;++character)
                (*source)[0x1eceb+16*player+character]=std::uint8_t('A'+(variation+player+character)%26);
            for(unsigned i=0;i<60;++i)(*source)[0x1c9de+i]=std::uint8_t((variation+i)%7);
            for(unsigned i=0;i<12;++i)(*source)[0x1ca36+i]=std::uint8_t((variation+i)%11);
        }
        if(kind==NativeSaveKind::Records)for(unsigned table=0;table<2;++table)for(unsigned record=0;record<16;++record){
            const unsigned address=0x7a61a+256*table+16*record;
            for(unsigned character=0;character<12;++character)(*source)[address+character]=std::uint8_t('A'+(variation+record+character)%26);
            (*source)[address+12]=0;(*source)[address+13]=std::uint8_t(0x10+(record%10));(*source)[address+14]=std::uint8_t(variation%10);
        }
        beginNativeSaveTransaction(*source,true);const auto payload=makeNativeSavePayload(*source,kind);restoreNativeSaveTransaction(*source);
        prepare(variation+3);*destination=original;
        beginNativeSaveTransaction(*destination,false);require(applyNativeSavePayload(*destination,kind,payload),"Valid original save payload rejected");
        if(kind==NativeSaveKind::Career){
            require(std::equal(source->begin()+0x1c9c2,source->begin()+0x1c9d1,destination->begin()+0x1c9c2),"Career fields did not round trip");
            require(std::equal(source->begin()+0x1ed5b,source->begin()+0x1ed67,destination->begin()+0x1ed5b),"Career name did not round trip");
        }
        if(kind==NativeSaveKind::Multiplayer){
            require((*destination)[0x5eb76]==(*source)[0x5eb76],"Local player count did not round trip");
            require(std::equal(source->begin()+0x1ecea,source->begin()+0x1ed6a,destination->begin()+0x1ecea),"Local names did not round trip");
            require(std::equal(source->begin()+0x1c9de,source->begin()+0x1ca1a,destination->begin()+0x1c9de),"Local standings did not round trip");
            require(std::equal(source->begin()+0x1ca36,source->begin()+0x1ca42,destination->begin()+0x1ca36),"Local damage allowances did not round trip");
        }
        if(kind==NativeSaveKind::Records)for(unsigned table=0;table<2;++table)for(unsigned record=0;record<16;++record){
            const unsigned address=0x7a61a+256*table+16*record;
            require(std::equal(source->begin()+address,source->begin()+address+15,destination->begin()+address),"Hall names and times did not round trip");
        }
        ++roundtripCases;
        auto corrupt=payload;corrupt[kind==NativeSaveKind::Career?32:31]^=1;
        *before=*destination;require(!applyNativeSavePayload(*destination,kind,corrupt),"Corrupt save validation marker accepted");
        require(*destination==*before,"Rejected save modified live state");++rejectionCases;
    }
    for(auto kind:{NativeSaveKind::Career,NativeSaveKind::Multiplayer,NativeSaveKind::Records}){
        prepare(7);*source=original;
        if(kind==NativeSaveKind::Career)(*source)[0x1c9c2]=(*source)[0x1c9c3];
        if(kind==NativeSaveKind::Multiplayer)(*source)[0x5eb76]=9;
        if(kind==NativeSaveKind::Records)(*source)[0x7a626]=0xff;
        beginNativeSaveTransaction(*source,true);const auto payload=makeNativeSavePayload(*source,kind);
        prepare(8);*destination=original;beginNativeSaveTransaction(*destination,false);*before=*destination;
        require(!applyNativeSavePayload(*destination,kind,payload),"Unsafe decoded game state accepted");
        require(*destination==*before,"Rejected decoded state modified live state");++rejectionCases;
    }
}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){return read(a);}
unsigned int m68k_read_memory_16(unsigned int a){if(a&1)invalid=true;return (read(a)<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned int a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){write(a,v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(a&1)invalid=true;write(a,v>>8);write(a+1,v);}
void m68k_write_memory_32(unsigned int a,unsigned int v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    require(argc==2,"Usage: native_save_state_tests payload.bin");std::ifstream file(argv[1],std::ios::binary);
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    require(payload.size()==408540,"Payload size mismatch");std::copy(payload.begin(),payload.end(),baseline.begin()+0xe700);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    transactions();league();transfers();replays();hostPayloads();
    std::cout<<"{\"cases\":"<<cases<<",\"codec_cases\":"<<codecCases<<",\"transfer_cases\":"<<transferCases
        <<",\"transaction_cases\":"<<transactionCases<<",\"replay_cases\":"<<replayCases<<",\"roundtrip_cases\":"<<roundtripCases
        <<",\"rejection_cases\":"<<rejectionCases<<",\"compared_bytes\":"<<comparedBytes<<",\"original_instructions\":"<<instructionCount
        <<",\"maximum_instructions\":"<<maximumInstructions<<",\"mismatches\":0,\"whole_ram_compared\":true,\"original_code_unmodified\":true"
        <<",\"replay_key_f1_not_pressed\":true,\"replay_menu_pixels_and_begin_delay_excluded\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
