#include "native_menu_flow.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <cmath>
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
bool stopped=false,invalid=false;
unsigned instructions=0,pc=0,stopAt=0,stoppedStatus=0;
std::uint64_t cases=0,comparisons=0;
bool routeOracle=false;
std::vector<unsigned> scriptedRows;
unsigned scriptedIndex=0,routeCases=0;
bool previewOracle=false,previewLooped=false;
std::vector<unsigned> scriptedControls;
unsigned previewCases=0;
bool fireWaitOracle=false;
unsigned fireWaitCases=0,fireDelayCalls=0;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
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
void hook(unsigned a){
    pc=a;
    if(fireWaitOracle&&a==0x5ba42){++fireDelayCalls;require(m68k_get_reg(nullptr,M68K_REG_D2)==6-fireDelayCalls,"Original fire delay parameter differs");}
    if(previewOracle&&a==0x60bae){
        if(scriptedIndex>=scriptedControls.size()){
            previewLooped=stopped=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;
        }
        const unsigned controls=scriptedControls[scriptedIndex++],sp=m68k_get_reg(nullptr,M68K_REG_SP);
        original[0x1bb47]=std::uint8_t(controls);m68k_set_reg(M68K_REG_D0,controls);
        m68k_set_reg(M68K_REG_PC,(read(sp)<<24)|(read(sp+1)<<16)|(read(sp+2)<<8)|read(sp+3));
        m68k_set_reg(M68K_REG_SP,sp+4);return;
    }
    if(routeOracle&&a==0x5b840){
        if(scriptedIndex>=scriptedRows.size()){invalid=true;m68k_end_timeslice();return;}
        const unsigned row=scriptedRows[scriptedIndex++],sp=m68k_get_reg(nullptr,M68K_REG_SP);
        // The menu renderer/input routine is independently compared above and
        // by the graphics oracle. Supply its documented selected-row result to
        // exercise the original caller's division, track and Cancel branches.
        original[0x1bb8d]=std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D2)-1);
        original[0x1bb8b]=std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D1));
        original[0x1bbd6]=std::uint8_t(row);original[0x1bbca]=16;
        m68k_set_reg(M68K_REG_D0,row);
        m68k_set_reg(M68K_REG_PC,(read(sp)<<24)|(read(sp+1)<<16)|(read(sp+2)<<8)|read(sp+3));
        m68k_set_reg(M68K_REG_SP,sp+4);return;
    }
    if(routeOracle&&(a==0x5ba3e||a==0x5baea)){
        stopped=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;
    }
    if((stopAt&&a==stopAt)||(!stopAt&&(a==0x5b87e||a==0x5b99e))){
        stoppedStatus=m68k_get_reg(nullptr,M68K_REG_SR);stopped=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;
    }
    if(++instructions>3000000||a<0xe700||a>=0x724dc){invalid=true;m68k_end_timeslice();}
}
void cpu(unsigned entry,unsigned d0=0,unsigned d2=0){
    instructions=0;invalid=stopped=false;stack.fill(0);m68k_pulse_reset();
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_D0,d0);m68k_set_reg(M68K_REG_D2,d2);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-i*8));
    m68k_set_reg(M68K_REG_PC,entry);
}
void compareSelection(unsigned count,unsigned selected,unsigned controls,unsigned keys){
    original=baseline;original[0x57c3c]=0;original[0x1bb8d]=std::uint8_t(count-1);
    original[0x1bbd6]=std::uint8_t(selected);original[0x1bb47]=std::uint8_t(controls);
    std::fill_n(original.begin()+0xead6,128,std::uint8_t{});
    for(unsigned bit=0;bit<4;++bit)if(keys&(1u<<bit))original[0xead6+bit+1]=0xb3;
    native=original;const bool repainted=applyNativeMenuInput(native,{std::uint8_t(controls),std::uint8_t(keys)});
    stopAt=0;cpu(0x5b9a4,controls);m68k_execute(50000);
    if(invalid||!stopped)throw std::runtime_error("Original local menu input failed");
    for(unsigned a=0;a<original.size();++a){
        ++comparisons;
        if(original[a]!=native[a]){
            std::ostringstream text;text<<"Menu input mismatch count="<<count<<" selected="<<selected<<" controls="<<controls
                <<" keys="<<keys<<" address=0x"<<std::hex<<a<<" original="<<unsigned(original[a])<<" native="<<unsigned(native[a]);
            throw std::runtime_error(text.str());
        }
    }
    require(repainted==(pc==0x5b87e),"Menu repaint result mismatch");++cases;
}
unsigned delayCycles(unsigned units,unsigned initialCounter=0){
    original=baseline;original[0x1bb1b]=std::uint8_t(initialCounter);stopAt=sentinel;cpu(0x5ba42,0,units);
    // Musashi charges its synthetic reset transition before the first opcode;
    // that transition is not part of the game's delay subroutine.
    require(m68k_execute(0)==40,"Unexpected verification CPU reset charge");
    unsigned cycles=0;
    while(m68k_get_reg(nullptr,M68K_REG_PC)!=sentinel){cycles+=unsigned(m68k_execute(1));if(invalid)throw std::runtime_error("Original delay failed");}
    return cycles;
}
void startupTemplates(){
    original=baseline;
    for(unsigned i=0;i<256;++i){original[0x7a61a+i]=std::uint8_t(i);original[0x7a71a+i]=std::uint8_t(~i);original[0x7a91a+i]=0xa5;}
    native=original;initializeNativeMenuSession(native);
    stopAt=0x5c8e0;cpu(0x5c890);m68k_execute(100000);
    require(stopped&&!invalid,"Original startup templates failed");
    for(unsigned a=0;a<original.size();++a){
        // Native helper additionally enters the already-established local
        // session and saves its RNG. The original does those after name entry.
        if(a==0x57c3c||a==0x5eb76||a==0x5eb75||a==0x60fbc||(a>=0x636b8&&a<0x636bc))continue;
        require(original[a]==native[a],"Native names/record template initialization differs");
    }
}
void ready(NativeMenuFlow& flow){flow.update({},1);flow.update({},1);}
NativeMenuAction confirm(NativeMenuFlow& flow){
    ready(flow);flow.update({16,0},0);return flow.update({},1);
}
void choose(NativeMenuFlow& flow,unsigned row){
    ready(flow);flow.update({0,std::uint8_t(1u<<row)},0);ready(flow);
}
void compareRoutes(){
    for(unsigned context:{0u,11u})for(unsigned skipDivision:{0u,1u})
        for(unsigned division=0;division<4;++division)for(unsigned track=0;track<3;++track){
            original=baseline;original[0x57c3c]=0;original[0x1c9d0]=std::uint8_t(context);
            original[0x1ca22]=0;original[0x1c9ce]=0;original[0x5eb7c]=std::uint8_t(division^3);
            original[0x5eb7d]=std::uint8_t(skipDivision);native=original;
            NativeMenuFlow flow(native);
            if(skipDivision)flow.returnFromRace();
            else {confirm(flow);choose(flow,division);confirm(flow);}
            choose(flow,track);confirm(flow);
            scriptedRows=skipDivision?std::vector<unsigned>{track}:std::vector<unsigned>{division,track};
            scriptedIndex=0;routeOracle=true;stopAt=0;cpu(0x5ba78);m68k_execute(100000);
            routeOracle=false;
            require(!invalid&&stopped&&scriptedIndex==scriptedRows.size(),"Original Practice caller failed");
            // These are all game-state writes in the original caller and its
            // selectDivisionAndTrack/setCurrentTrackID callees. Display scratch
            // effects are verified separately by the graphics/input tests.
            for(unsigned a:{0x5eb75u,0x5eb7cu,0x5eb7du,0x1ca22u,0x1ca33u,0x1ca21u}){
                // Cancel has already re-entered the native main menu, whereas
                // the original stops immediately before handleMainMenu.
                if(track==2&&a==0x5eb75)continue;
                if(original[a]!=native[a]){
                    std::ostringstream error;error<<"Practice route mismatch context="<<context<<" skipDivision="<<skipDivision
                        <<" division="<<division<<" track="<<track<<" address="<<std::hex<<a
                        <<" original="<<unsigned(original[a])<<" native="<<unsigned(native[a]);throw std::runtime_error(error.str());
                }
            }
            ++routeCases;
        }
}
void comparePreview(){
    for(unsigned angle:{0u,127u,255u})for(unsigned controls=0;controls<32;++controls){
        native=baseline;native[0x1c9ce]=native[0x1ca22]=native[0x5eb7d]=0;
        NativeMenuFlow flow(native);confirm(flow);confirm(flow);confirm(flow);flow.update({},1);
        require(flow.page()==NativeMenuPage::Preview,"Preview fixture did not enter preview");
        native[0x1bb57]=std::uint8_t(angle);original=native;
        flow.update({},0);const auto action=flow.update({std::uint8_t(controls),0},0);
        scriptedControls={0,controls};scriptedIndex=0;previewOracle=true;previewLooped=false;
        stopAt=sentinel;cpu(0x5d980);m68k_execute(100000);previewOracle=false;
        require(stopped&&!invalid,"Original preview controls failed");
        require(native[0x1bb57]==original[0x1bb57],"Original preview camera angle differs");
        const auto expected=previewLooped?NativeMenuAction::None:
            (stoppedStatus&1)?NativeMenuAction::RotatePreview:NativeMenuAction::StartPractice;
        require(action==expected,"Original preview input outcome differs");++previewCases;
    }
}
void compareFireWait(){
    for(unsigned controls=0;controls<32;++controls)for(bool initiallyHeld:{false,true}){
        original=baseline;original[0x1bb1b]=0;
        NativeFireWait wait;
        const auto directions=std::uint8_t(controls&15),fire=std::uint8_t(directions|16);
        if(initiallyHeld)require(!wait.update(fire,0),"Held fire unexpectedly dismissed wait");
        require(!wait.update(directions,0),"Fire-release wait should debounce");
        const double delay=nativeMenuDelaySeconds(5);
        require(!wait.update(fire,delay/2),"Fire-wait debounce ended early");
        require(wait.update(fire,delay/2),"Fire pressed during debounce must be accepted after it");
        wait.reset();require(!wait.update(fire,1),"Reset did not rearm fire-release gate");
        scriptedControls=initiallyHeld?std::vector<unsigned>{fire,directions,fire}:std::vector<unsigned>{directions,fire};
        scriptedIndex=0;fireDelayCalls=0;previewLooped=false;previewOracle=fireWaitOracle=true;
        stopAt=sentinel;cpu(0x6488a);m68k_execute(2000000);previewOracle=fireWaitOracle=false;
        require(stopped&&!invalid&&!previewLooped&&scriptedIndex==scriptedControls.size()&&fireDelayCalls==5,"Original fire wait differs");
        ++fireWaitCases;
    }
}
void route(){
    native=baseline;native[0x1c9ce]=0;native[0x1ca22]=0;native[0x5eb7d]=0;
    initializeNativeMenuSession(native);NativeMenuFlow flow(native);
    require(flow.page()==NativeMenuPage::Main&&flow.renderState().selected==1,"Original main menu default differs");
    require(confirm(flow)==NativeMenuAction::Changed&&flow.page()==NativeMenuPage::Division,"Practice did not enter divisions");
    require(flow.renderState().selected==3,"Initial Division4 choice differs");
    require(confirm(flow)==NativeMenuAction::Changed&&flow.page()==NativeMenuPage::Track,"Division did not enter tracks");
    require(native[0x5eb7c]==0,"Division reversal differs");
    confirm(flow);require(flow.update({},1)==NativeMenuAction::EnterPreview&&flow.trackId()==native[0x648c2],"Practice did not enter selected preview");
    require(flow.update({16,0},0)==NativeMenuAction::None,"Held confirmation skipped preview");
    flow.update({},0);const auto angle=native[0x1bb57];
    require(flow.update({4,0},0)==NativeMenuAction::RotatePreview&&native[0x1bb57]==std::uint8_t(angle+1),"Original preview left rotation differs");
    require(flow.update({4,0},0)==NativeMenuAction::None,"Preview repeated a held control");
    flow.update({},0);require(flow.update({8,0},0)==NativeMenuAction::RotatePreview&&native[0x1bb57]==angle,"Original preview right rotation differs");
    flow.update({},0);require(flow.update({16,0},0)==NativeMenuAction::StartPractice&&flow.page()==NativeMenuPage::Race,"Preview did not start race");
    flow.returnFromRace();require(flow.page()==NativeMenuPage::Track&&native[0x5eb7d]==0,"Practice return should skip division screen");
    choose(flow,2);require(confirm(flow)==NativeMenuAction::Changed&&flow.page()==NativeMenuPage::Main,"Original Cancel did not return to main menu");
    choose(flow,3);require(confirm(flow)==NativeMenuAction::None&&flow.page()==NativeMenuPage::Main,"Load/Save/Replay omitted original entry delay");
    require(flow.update({},1)==NativeMenuAction::Changed&&flow.page()==NativeMenuPage::LoadSave,"Load/Save/Replay submenu did not open");
    require(flow.renderState().selected==3,"Load/Save/Replay default should select Cancel");
    require(confirm(flow)==NativeMenuAction::None&&flow.page()==NativeMenuPage::LoadSave,"Cancel omitted original return delay");
    require(flow.update({},1)==NativeMenuAction::Changed&&flow.page()==NativeMenuPage::Main,"Load/Save/Replay Cancel did not return to main menu");
    // Every original division/track pair maps through passive pack data.
    for(unsigned row=0;row<4;++row)for(unsigned track=0;track<2;++track){
        native=baseline;native[0x5eb7d]=native[0x1ca22]=native[0x1c9ce]=0;NativeMenuFlow selected(native);
        confirm(selected);choose(selected,row);confirm(selected);choose(selected,track);confirm(selected);selected.update({},1);
        require(selected.trackId()==native[0x648c2+(row^3)*2+track],"Practice track lookup differs");
    }
}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){return read(a);}
unsigned int m68k_read_memory_16(unsigned int a){if(a&1)invalid=true;return(read(a)<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned int a){return(m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){write(a,v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(a&1)invalid=true;write(a,v>>8);write(a+1,v);}
void m68k_write_memory_32(unsigned int a,unsigned int v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc!=2)throw std::runtime_error("native_menu_flow_tests payload.bin");
    std::ifstream file(argv[1],std::ios::binary);const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    require(payload.size()==408540,"Original payload size mismatch");std::copy(payload.begin(),payload.end(),baseline.begin()+0xe700);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    for(unsigned count:{2u,3u})for(unsigned selected=0;selected<=count;++selected)
        for(unsigned controls=0;controls<32;++controls)for(unsigned keys=0;keys<16;++keys)compareSelection(count,selected,controls,keys);
    const auto one=delayCycles(1),two=delayCycles(2),fifteen=delayCycles(15);
    if(std::abs(nativeMenuDelaySeconds(1)*7093790.0-one)>=0.01){std::ostringstream message;message<<"Native menu delay clock count differs: original "<<one<<","<<two<<","<<fifteen;throw std::runtime_error(message.str());}
    require(std::abs(nativeMenuDelaySeconds(2)*7093790.0-two)<0.01,"Native two-unit delay differs");
    require(std::abs(nativeMenuDelaySeconds(15)*7093790.0-fifteen)<0.01,"Native fifteen-unit delay differs");
    for(unsigned counter:{1u,15u,255u})require(std::abs(nativeMenuDelaySeconds(15,counter)*7093790.0-delayCycles(15,counter))<0.01,"Native nonzero-counter delay differs");
    startupTemplates();route();compareRoutes();comparePreview();compareFireWait();
    std::cout<<"{\"status\":\"passed\",\"input_cases\":"<<cases<<",\"compared_bytes\":"<<comparisons
        <<",\"delay_cycles_1\":"<<one<<",\"delay_cycles_2\":"<<two<<",\"delay_cycles_15\":"<<fifteen
        <<",\"practice_caller_cases\":"<<routeCases
        <<",\"preview_control_cases\":"<<previewCases
        <<",\"fire_wait_cases\":"<<fireWaitCases
        <<",\"startup_templates_checked\":true,\"practice_route_checked\":true,\"all_track_mappings_checked\":true,\"original_code_unmodified\":true,\"captured_state_used\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
