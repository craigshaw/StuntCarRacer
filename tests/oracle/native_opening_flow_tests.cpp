#include "native_opening_flow.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using namespace scr::viewer;
NativeMemory baseline{},original{},native{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
bool stopped{},invalid{},route{};
unsigned stopAt{},instructions{},rowIndex{};
std::uint64_t instructionCount{},comparedBytes{},sortCases{},routeCases{},timingCases{};
std::vector<unsigned> rows,names;
void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
unsigned read(unsigned a){
    if(a<original.size())return original[a];
    if(a>=stackBase&&a<stackTop)return stack[a-stackBase];
    if(a>=sentinel&&a<sentinel+4)return a==sentinel?0x4e:a==sentinel+1?0x72:a==sentinel+2?0x27:0;
    invalid=true;return 0;
}
void write(unsigned a,unsigned v){
    if(a<original.size()){original[a]=std::uint8_t(v);return;}
    if(a>=stackBase&&a<stackTop){stack[a-stackBase]=std::uint8_t(v);return;}
    invalid=true;
}
void returnFromCall(){
    const unsigned sp=m68k_get_reg(nullptr,M68K_REG_SP);
    const unsigned target=(read(sp)<<24)|(read(sp+1)<<16)|(read(sp+2)<<8)|read(sp+3);
    m68k_set_reg(M68K_REG_SP,sp+4);m68k_set_reg(M68K_REG_PC,target);
}
void hook(unsigned pc){
    if(++instructions>4000000){invalid=true;m68k_end_timeslice();return;}
    if(pc==stopAt||pc==sentinel){stopped=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;}
    if(route&&pc==0x5b840){
        require(rowIndex<rows.size(),"Original requested an unexpected opening menu");
        const auto row=rows[rowIndex++];
        const auto finalRow=m68k_get_reg(nullptr,M68K_REG_D2)&255;
        require(row<=finalRow,"Oracle script selected an absent opening row");
        original[0x1bb8d]=std::uint8_t(finalRow-1);
        original[0x1bb8b]=std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D1));
        original[0x1bbd6]=std::uint8_t(row);original[0x1bbca]=16;
        m68k_set_reg(M68K_REG_D0,row);returnFromCall();return;
    }
    if(route&&pc==0x5b648){names.push_back(original[0x5eb76]);returnFromCall();return;}
    if(pc<0xe700||pc>=0x724dc){invalid=true;m68k_end_timeslice();}
}
void execute(unsigned entry,unsigned stop=sentinel){
    stopAt=stop;instructions=0;stopped=invalid=false;stack.fill(0);m68k_pulse_reset();
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-i*8));
    m68k_set_reg(M68K_REG_PC,entry);
    while(!stopped&&!invalid)m68k_execute(100000);
    instructionCount+=instructions;require(stopped&&!invalid,"Original opening routine failed");
}
void compareMemory(const char* label){
    for(unsigned a=0;a<original.size();++a){
        ++comparedBytes;
        if(original[a]!=native[a]){
            std::ostringstream s;s<<label<<" address0x"<<std::hex<<a<<" original="<<unsigned(original[a])<<" native="<<unsigned(native[a]);
            throw std::runtime_error(s.str());
        }
    }
}
void templates(){
    original=baseline;
    for(unsigned i=0;i<256;++i){original[0x7a91a+i]=0xa5;original[0x7a61a+i]=std::uint8_t(i);original[0x7a71a+i]=std::uint8_t(~i);}
    native=original;initializeNativeOpeningSession(native);execute(0x5c890,0x5c8e0);
    // Opening additionally applies original0x5cf5c..0x5cf7e and menu display
    // state. Compare the template routine's complete memory effect separately.
    for(unsigned a:{0x57c3cu,0x57c62u,0x60fbcu,0x1bbbcu,0x5d724u,0x64530u})original[a]=native[a];
    compareMemory("Opening templates");
    for(unsigned flag:{0u,1u,0x80u,0xffu}){
        native=baseline;native[0x60fbc]=std::uint8_t(flag);initializeNativeOpeningSession(native);
        require(native[0x60fbc]==(flag?flag:0x80),"Opening menu flag initialization differs");
        require(native[0x57c62]==0x40&&!native[0x57c3c],"Opening context differs");
    }
}
void standings(){
    for(unsigned players=2;players<=8;++players)for(unsigned variant=0;variant<128;++variant){
        original=baseline;original[0x57c3c]=0;original[0x5eb76]=std::uint8_t(players-1);
        std::uint32_t random=variant*0x9e3779b9u+players;
        for(unsigned id=0;id<12;++id){
            random=1664525u*random+1013904223u;
            original[0x1c9c2+id]=std::uint8_t(id);
            original[0x1c9de+id]=variant<4?0:std::uint8_t(random>>24);
            original[0x1c9ea+id]=variant<4?0:std::uint8_t(random>>16);
        }
        if(variant&1)std::reverse(original.begin()+0x1c9c2,original.begin()+0x1c9c2+12);
        native=original;prepareNativeOpeningLocalStandings(native);
        // configureRaceSetup calls setupGameConfiguration, stores its initial
        // rendering index, then displayLeagueRaceResults calls sortRaceResults.
        execute(0x646e8,0x5e944);compareMemory("Opening local standings");++sortCases;
    }
}
void ready(NativeOpeningFlow& flow){flow.update({},1);flow.update({},1);}
NativeOpeningAction choose(NativeOpeningFlow& flow,unsigned row){
    ready(flow);flow.update({0,std::uint8_t(1u<<row)},0);ready(flow);
    flow.update({16,0},0);return flow.update({},1);
}
void modes(){
    for(unsigned players=1;players<=8;++players)for(unsigned rejected=0;rejected<4;++rejected){
        original=baseline;original[0x57c3c]=0;native=original;
        NativeOpeningFlow flow(native);std::vector<unsigned> observed;
        rows={players==1?0u:1u};
        require(choose(flow,rows.front())==NativeOpeningAction::BeginName,"Opening mode did not begin name entry");
        for(unsigned id=0;id<players;++id){
            observed.push_back(native[0x5eb76]);const auto action=flow.nameCompleted();
            if(players==1||id==7){require(action==NativeOpeningAction::ShowRoster,"Opening did not show final roster");break;}
            require(action==NativeOpeningAction::Changed&&flow.page()==NativeOpeningPage::AddPlayer,"Opening omitted add-player menu");
            if(!id)for(unsigned n=0;n<rejected;++n){
                rows.push_back(1);require(choose(flow,1)==NativeOpeningAction::Changed,"One-player multiplayer was accepted");
                require(flow.page()==NativeOpeningPage::AddPlayer,"One-player multiplayer escaped its menu");
            }
            if(id+1==players){rows.push_back(1);require(choose(flow,1)==NativeOpeningAction::ShowRoster,"Local roster was not selected");break;}
            rows.push_back(0);require(choose(flow,0)==NativeOpeningAction::BeginName,"Next player name was omitted");
        }
        names.clear();rowIndex=0;route=true;execute(0x5e8ac);route=false;
        require(rowIndex==rows.size()&&names==observed,"Original name-entry call sequence differs");
        require(original[0x5eb76]==native[0x5eb76]&&original[0x5eb75]==native[0x5eb75],"Original selected mode state differs");
        require(native[0x5eb79]==11&&!native[0x1c9d0],"Opening changed local Practice driver/context");
        require(flow.update({16,0},1)!=NativeOpeningAction::Complete,"Held confirm dismissed original roster");
        flow.update({},0);flow.update({},1);
        const auto before=native;const auto action=flow.update({16,0},0);
        require(action==NativeOpeningAction::Complete&&flow.complete(),"Roster new-fire did not finish opening");
        original=before;execute(0x5cfa4,0x5cfb8);
        // Roster's final input poll and resetTextYOffset precede this suffix.
        original[0x1bb47]=native[0x1bb47];original[0x1bb6e]=native[0x1bb6e];
        compareMemory("Opening saved context");++routeCases;
    }
}
void delays(){
    native=baseline;NativeOpeningFlow flow(native);const double paint=nativeMenuDelaySeconds(15);
    flow.update({16,0},paint-1e-7);require(flow.page()==NativeOpeningPage::Mode,"Opening painted too soon");
    flow.update({16,0},2e-7);flow.update({16,0},10);
    require(flow.page()==NativeOpeningPage::Mode&&!native[0x1bbca],"Held fire bypassed opening release gate");
    flow.update({},0);const double debounce=nativeMenuDelaySeconds(2);
    flow.update({16,0},debounce-1e-7);require(!native[0x1bbca],"Opening skipped release debounce");
    flow.update({16,0},2e-7);require(native[0x1bbca]==16,"Opening did not admit fresh fire");
    flow.update({},paint-1e-7);require(flow.page()==NativeOpeningPage::Mode,"Opening confirmation delay was shortened");
    require(flow.update({},2e-7)==NativeOpeningAction::BeginName,"Opening confirmation delay did not finish");++timingCases;
    native=baseline;NativeOpeningFlow link(native);ready(link);link.update({0,4},0);ready(link);
    require(link.page()==NativeOpeningPage::Mode&&link.renderState().selected==0&&link.renderState().disabledRows==4,"Disabled serial row accepted its numbered key");
    link.update({2,0},0);ready(link);link.update({2,0},0);ready(link);
    require(link.renderState().selected==1,"Disabled serial row accepted direction input");
    native[0x1bbd6]=2;
    require(link.update({16,0},0)==NativeOpeningAction::None&&!native[0x1bbca]&&link.page()==NativeOpeningPage::Mode,"Disabled serial row accepted confirmation");
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){return read(a);}
unsigned int m68k_read_memory_16(unsigned int a){return (read(a)<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned int a){return (read(a)<<24)|(read(a+1)<<16)|(read(a+2)<<8)|read(a+3);}
void m68k_write_memory_8(unsigned int a,unsigned int v){write(a,v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){write(a,v>>8);write(a+1,v);}
void m68k_write_memory_32(unsigned int a,unsigned int v){write(a,v>>24);write(a+1,v>>16);write(a+2,v>>8);write(a+3,v);}
}
int main(int argc,char** argv)try{
    if(argc<2||argc>3)throw std::runtime_error("native_opening_flow_tests payload [report.json]");
    std::ifstream in(argv[1],std::ios::binary);std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(in),{}};
    require(payload.size()==408540,"Unexpected original payload length");std::copy(payload.begin(),payload.end(),baseline.begin()+0xe700);
    baseline[0x57c3c]=0;baseline[0x1c9d0]=0;baseline[0x5eb79]=11;
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    templates();standings();modes();delays();
    std::ostringstream report;report<<"{\n  \"status\": \"passed\",\n  \"standingsCases\": "<<sortCases
        <<",\n  \"routeCases\": "<<routeCases<<",\n  \"timingCases\": "<<timingCases
        <<",\n  \"comparedRamBytes\": "<<comparedBytes<<",\n  \"originalInstructions\": "<<instructionCount<<"\n}\n";
    if(argc==3){std::ofstream out(argv[2]);out<<report.str();require(bool(out),"Cannot write opening report");}
    std::cout<<report.str();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
