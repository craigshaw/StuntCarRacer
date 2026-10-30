#include "session/native_multiplayer_state.h"
#include "menus/native_opening_flow.h"
#include "persistence/native_save_state.h"
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
using namespace scr::runtime;
NativeMemory original{},native{},baseline{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned entry{},stopAt{},pc{},instructions{},maximumInstructions{};
bool stopped{},invalid{},preparing{},bonus{};
std::uint64_t cases{},comparedBytes{},instructionCount{},assignmentCases{},configurationCases{},scoreCases{},sortCases{},bonusCases{},timerCases{},damageCases{},exitCases{},controlsCases{},advanceCases{},forfeitCases{},seasonCases{},championships{},races{},saveRoundtrips{};
std::uint32_t randomState=0x4d554c54;
unsigned next(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
unsigned read(unsigned address){
    if(address<original.size())return original[address];
    if(address>=stackBase&&address<stackTop)return stack[address-stackBase];
    if(address>=sentinel&&address<sentinel+4){constexpr std::uint8_t stop[]{0x4e,0x72,0x27,0};return stop[address-sentinel];}
    invalid=true;return 0;
}
void write(unsigned address,unsigned value){
    if(address<original.size()){original[address]=std::uint8_t(value);return;}
    if(address>=stackBase&&address<stackTop){stack[address-stackBase]=std::uint8_t(value);return;}
    invalid=true;
}
void returnFromCall(){
    const auto sp=m68k_get_reg(nullptr,M68K_REG_SP);
    const unsigned target=(read(sp)<<24)|(read(sp+1)<<16)|(read(sp+2)<<8)|read(sp+3);
    m68k_set_reg(M68K_REG_SP,sp+4);m68k_set_reg(M68K_REG_PC,target);
}
void hook(unsigned address){
    pc=address;
    if(address==stopAt||address==sentinel){stopped=true;m68k_end_timeslice();return;}
    if(preparing&&(address==0x645c6||address==0x64834||address==0x58e30||address==0x5914a||address==0x58914||
        address==0x59806||address==0x5eae0||address==0x5f074||address==0x64828)){returnFromCall();return;}
    if(bonus&&(address==0x59806||address==0x64b12||address==0x5ec7e||address==0x64c2e||address==0x64340||address==0x5deb6)){
        returnFromCall();return;
    }
    if(address<0xe700||address>=0x724dc||++instructions>4000000)invalid=true;
    if(invalid)m68k_end_timeslice();
}
void execute(unsigned start,unsigned d0=0,unsigned stop=sentinel){
    entry=start;stopAt=stop;instructions=0;invalid=stopped=false;stack.fill(0);
    m68k_pulse_reset();for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_D0,d0);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_PC,start);
    unsigned slices=0;
    while(!stopped&&!invalid){require(++slices<100,"Original multiplayer execution did not reach its boundary");m68k_execute(1000000);}
    if(invalid||!stopped){std::ostringstream message;message<<"Original multiplayer routine failed at0x"<<std::hex<<pc;throw std::runtime_error(message.str());}
    maximumInstructions=std::max(maximumInstructions,instructions);instructionCount+=instructions;
}
void compare(const char* label){
    for(unsigned address=0;address<original.size();++address){
        if(preparing&&address==0x1bbaa)continue;
        if(bonus&&(address==0x1bb16||address==0x64c18))continue;
        ++comparedBytes;
        if(original[address]!=native[address]){
            std::ostringstream message;message<<label<<" case="<<cases<<" address=0x"<<std::hex<<address
                <<" original="<<unsigned(original[address])<<" native="<<unsigned(native[address]);throw std::runtime_error(message.str());
        }
    }
    ++cases;
}
void check(unsigned address,const std::function<void(NativeMemory&)>& function,const char* label,unsigned d0=0,unsigned stop=sentinel){
    native=original;function(native);execute(address,d0,stop);compare(label);
}
void prepare(unsigned count,unsigned division,unsigned context,unsigned variation=0){
    original=baseline;original[0x5eb79]=11;initializeNativeGameData(original);initializeNativeOpeningSession(original);
    original[0x5eb76]=std::uint8_t(count-1);original[0x1c9ce]=std::uint8_t(division);original[0x1bb5f]=std::uint8_t(division);
    original[0x1c9d0]=std::uint8_t(context);original[0x1ca22]=0x80;original[0x57c3c]=0;
    original[0x5eb77]=std::uint8_t(variation&1);original[0x5eb78]=std::uint8_t(count);original[0x5eb7e]=std::uint8_t(variation%3);
    original[0x1ca35]=std::uint8_t(variation%count);original[0x5eb79]=std::uint8_t(11-original[0x1ca35]);
    original[0x57c67]=std::uint8_t(variation%4);original[0x1bbb4]=std::uint8_t((variation%4)*0x40);
    for(unsigned id=0;id<12;++id){
        original[0x1c9de+id]=variation<2?0:std::uint8_t(next()%(variation==7?256:8));
        original[0x1c9ea+id]=variation<2?0:std::uint8_t(next()%(variation==7?256:8));
        original[0x1c9f6+id]=std::uint8_t(next());original[0x1ca36+id]=std::uint8_t((id+variation)%11);
    }
    for(unsigned a=0x1c908;a<0x1c950;++a)original[a]=std::uint8_t(next());
    for(unsigned a=0x625a2;a<0x625a7;++a)original[a]=std::uint8_t(next());
    for(unsigned a=0x5fb3a;a<0x5fb62;++a)original[a]=std::uint8_t(next());
    original[0x1ca23]=variation%3?std::uint8_t(12-count):0;original[0x1ca24]=variation%2?11:0;
    const unsigned begin=12-count;
    if(variation&1)std::reverse(original.begin()+0x1c9c2+begin,original.begin()+0x1c9c2+12);
    preparing=bonus=false;
}
void primitives(){
    for(unsigned count=2;count<=8;++count)for(unsigned division=0;division<4;++division)for(unsigned context:{0u,11u})for(unsigned variation=0;variation<8;++variation){
        prepare(count,division,context,variation);check(0x5ee68,randomizeNativeMultiplayerCars,"pace-car-assignment");++assignmentCases;
        prepare(count,division,context,variation);check(0x5ab46,configureNativeMultiplayerRace,"local-fixture");++configurationCases;
        prepare(count,division,context,variation);check(0x5acfe,sortNativeMultiplayerStandings,"local-sort");++sortCases;
        prepare(count,division,context,variation);check(0x5aa84,scoreNativeMultiplayerRace,"local-race-scoring");++scoreCases;
        prepare(count,division,context,variation);bonus=true;check(0x5ef8e,awardNativeMultiplayerTrackBonuses,"track-bonus");++bonusCases;bonus=false;
        prepare(count,division,context,variation);check(0x5d038,initializeNativeMultiplayerTrack,"track-timer-reset",0,0x5d068);++timerCases;
        prepare(count,division,context,variation);preparing=true;check(0x5d068,prepareNativeMultiplayerRace,"track-and-record-selection",0,0x58580);++configurationCases;preparing=false;
        prepare(count,division,context,variation);const bool more=original[0x1ca35]+1<count;
        check(0x5d18c,[](auto& m){(void)advanceNativeMultiplayerRace(m);},"next-driver",0,more?0x5d068:0x5d1dc);++advanceCases;
        prepare(count,division,context,variation);check(0x5d1e2,[](auto& m){(void)advanceNativeMultiplayerTrack(m);},"next-track-or-championship",0,(original[0x5eb77]==1)?0x5d068:0x5cfb8);++advanceCases;
        prepare(count,division,context,variation);check(0x5d000,forfeitNativeMultiplayerTrack,"forfeit-current-track",0,0x5d1dc);++forfeitCases;
    }
}
void driverData(){
    for(unsigned count=2;count<=8;++count)for(unsigned division=0;division<4;++division)for(unsigned context:{0u,11u})for(unsigned driver=0;driver<count;++driver){
        prepare(count,division,context,driver);original[0x5eb79]=std::uint8_t(11-driver);
        check(0x5d87e,loadNativeMultiplayerDamage,"per-driver-damage",0,0x60988);++damageCases;
        for(unsigned lap=0;lap<6;++lap){
            original[0x1bb20]=std::uint8_t(lap);original[0x1c9cf]=std::uint8_t((driver+lap)%11);original[0x1bbb5]=std::uint8_t((driver+2)%11);
            check(0x5d698,finishNativeMultiplayerRaceExit,"per-driver-race-exit",0,0x5d714);++exitCases;
        }
        for(unsigned load:{0u,1u}){
            check(0x5faf2,[&](auto& m){transferNativeMultiplayerControls(m,load!=0);},"per-driver-controls",load?0x80:0);++controlsCases;
        }
    }
}
void seasons(){
    for(unsigned count=2;count<=8;++count)for(unsigned division=0;division<4;++division)for(unsigned context:{0u,11u}){
        prepare(count,division,context);original[0x1ca35]=0;
        native=original;initializeNativeMultiplayerSeason(native);execute(0x609ae);execute(0x5d014,0,0x5d068);compare("complete-season-initialization");++seasonCases;
    }
}
void completeChampionships(){
    auto state=std::make_unique<NativeMemory>(),loaded=std::make_unique<NativeMemory>();
    for(unsigned count=2;count<=8;++count)for(unsigned division=0;division<4;++division)for(unsigned context:{0u,11u}){
        prepare(count,division,context);*state=original;(*state)[0x1ca35]=0;initializeNativeGameTables(*state);
        for(unsigned championship=0;championship<4;++championship){
            initializeNativeMultiplayerSeason(*state);unsigned completed=0;
            for(unsigned track=0;track<2;++track){
                for(unsigned driver=0;driver<count;++driver){
                    prepareNativeMultiplayerRace(*state);
                    require((*state)[0x5eb79]==11-driver&&(*state)[0x1ca29]==1-track,"Incorrect human/pace-car schedule");
                    const unsigned ordinal=2*division+track;
                    require((*state)[0x1ca33]==(*state)[0x648c2+ordinal],"Incorrect local championship track");
                    (*state)[0x1bbb4]=std::uint8_t(((driver+track)%4)*0x40);scoreNativeMultiplayerRace(*state);
                    const bool more=advanceNativeMultiplayerRace(*state);require(more==(driver+1<count),"Incorrect local fixture count");
                    ++completed;++races;
                }
                (*state)[0x1ca23]=11;(*state)[0x1ca24]=std::uint8_t(12-count);awardNativeMultiplayerTrackBonuses(*state);
                const auto nextTrack=advanceNativeMultiplayerTrack(*state);
                const auto expected=track==0?NativeMultiplayerTrackAdvance::NextTrack:championship==3?NativeMultiplayerTrackAdvance::ChampionshipReset:NativeMultiplayerTrackAdvance::ChampionshipComplete;
                require(nextTrack==expected,"Incorrect championship completion/reset");
            }
            require(completed==2*count,"Championship did not contain two races per driver");++championships;
            if(championship<3){
                beginNativeSaveTransaction(*state,true);const auto payload=makeNativeSavePayload(*state,NativeSaveKind::Multiplayer);restoreNativeSaveTransaction(*state);
                *loaded=*state;beginNativeSaveTransaction(*loaded,false);require(applyNativeSavePayload(*loaded,NativeSaveKind::Multiplayer,payload),"Multiplayer continuation payload rejected");
                require(std::equal(state->begin()+0x1c9de,state->begin()+0x1ca1a,loaded->begin()+0x1c9de),"Saved continuation lost scores");
                require(std::equal(state->begin()+0x1ca36,state->begin()+0x1ca42,loaded->begin()+0x1ca36),"Saved continuation lost damage");++saveRoundtrips;
            }
        }
        for(unsigned address=0x1c9de;address<0x1ca0e;++address)require(!(*state)[address],"Fourth championship did not reset scores");
        for(unsigned id=0;id<12;++id)require((*state)[0x1ca0e+id]==id,"Fourth championship did not reset standings order");
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
    require(argc==2,"Usage: native_multiplayer_state_tests payload.bin");std::ifstream file(argv[1],std::ios::binary);
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    require(payload.size()==408540,"Payload size mismatch");std::copy(payload.begin(),payload.end(),baseline.begin()+0xe700);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    primitives();driverData();seasons();completeChampionships();
    std::cout<<"{\"cases\":"<<cases<<",\"assignment_cases\":"<<assignmentCases<<",\"configuration_cases\":"<<configurationCases
        <<",\"score_cases\":"<<scoreCases<<",\"sort_cases\":"<<sortCases<<",\"bonus_cases\":"<<bonusCases<<",\"timer_cases\":"<<timerCases
        <<",\"damage_cases\":"<<damageCases<<",\"exit_cases\":"<<exitCases<<",\"controls_cases\":"<<controlsCases<<",\"advance_cases\":"<<advanceCases
        <<",\"forfeit_cases\":"<<forfeitCases<<",\"season_cases\":"<<seasonCases<<",\"championships\":"<<championships<<",\"synthetic_races\":"<<races
        <<",\"save_roundtrips\":"<<saveRoundtrips<<",\"compared_bytes\":"<<comparedBytes<<",\"original_instructions\":"<<instructionCount
        <<",\"maximum_instructions\":"<<maximumInstructions<<",\"mismatches\":0,\"player_counts\":[2,3,4,5,6,7,8],\"divisions\":4,\"contexts\":[0,11]"
        <<",\"original_code_unmodified\":true,\"presentation_calls_excluded\":true,\"bonus_presentation_bytes_excluded\":2,\"prepare_layout_byte_excluded\":1}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
