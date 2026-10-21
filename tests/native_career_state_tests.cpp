#include "../app/native_race_initialization.h"
#include "../app/native_menu_flow.h"
#include "../app/native_opening_flow.h"
#include "../app/native_save_state.h"
#include "race_transitions_fixture.h"
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
namespace transitions=scr::test::transitions;
NativeMemory baseline{},original{},native{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned pc{},stopAt{},instructions{},maximumInstructions{};
bool stopped{},invalid{},promotion{},seeking{},exiting{};
std::uint32_t randomState=0x43415245;
std::uint64_t cases{},comparedBytes{},instructionCount{},forfeitCases{},promotionCases{},damageCases{},initializationCases{},seekCases{},scoreCases{},advanceCases{},seasonSequences{},humanFixtures{},aiFixtures{},tieBreaks{},flowSeasons{},flowRaces{},saveRoundtrips{},replayCases{},normalChampions{},superChampions{},relegations{};
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
    if(address==stopAt||address==sentinel||(seeking&&(address==0x58580||address==0x5d1be))){
        stopped=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;
    }
    if(promotion&&(address==0x5deb6||address==0x64340||address==0x64b0e||address==0x64c2e||address==0x5a656)){
        returnFromCall();return;
    }
    if(seeking&&(address==0x645c6||address==0x64834||address==0x58e30||address==0x5914a||address==0x58914||
        address==0x59806||address==0x5eae0||address==0x5f074||address==0x64828)){returnFromCall();return;}
    if(exiting&&(address==0x6444e||address==0xf3f0)){returnFromCall();return;}
    if(address==0x5adac)++tieBreaks;
    if(address<0xe700||address>=0x724dc||++instructions>8000000)invalid=true;
    if(invalid)m68k_end_timeslice();
}
void execute(unsigned entry,unsigned stop=sentinel,unsigned d0=0){
    stopAt=stop;instructions=0;invalid=stopped=false;stack.fill(0);
    m68k_pulse_reset();for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_D0,d0);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_PC,entry);unsigned slices=0;
    while(!stopped&&!invalid){require(++slices<128,"Original career execution did not reach its boundary");m68k_execute(1000000);}
    if(invalid||!stopped){std::ostringstream message;message<<"Original career routine failed at0x"<<std::hex<<pc;throw std::runtime_error(message.str());}
    maximumInstructions=std::max(maximumInstructions,instructions);instructionCount+=instructions;
}
void compare(const char* label){
    for(unsigned address=0;address<original.size();++address){
        ++comparedBytes;
        if(original[address]!=native[address]){
            std::ostringstream message;message<<label<<" case="<<cases<<" address=0x"<<std::hex<<address
                <<" original="<<unsigned(original[address])<<" native="<<unsigned(native[address]);throw std::runtime_error(message.str());
        }
    }
    ++cases;
}
void fresh(NativeMemory& memory,unsigned division,unsigned context,unsigned position=2,unsigned variation=0){
    memory=baseline;memory[0x5eb79]=11;initializeNativeGameData(memory);initializeNativeOpeningSession(memory);
    memory[0x57c3c]=memory[0x5eb76]=0;memory[0x1c9ce]=std::uint8_t(division);memory[0x1bb5f]=std::uint8_t(division);
    memory[0x1c9d0]=std::uint8_t(context);memory[0x1ca22]=0x80;memory[0x1bb5b]=0;
    memory[0x5eb78]=6;memory[0x1ca35]=0;memory[0x1c9cf]=std::uint8_t(variation%11);
    std::swap(memory[0x1c9c2+11],memory[0x1c9c2+memory[0x5ab42+division]+position]);
    for(unsigned i=0;i<12;++i)memory[0x1ed5b+i]=std::uint8_t(i<1+variation%12?'A'+(variation+i)%26:' ');
    for(unsigned address=0x625a2;address<0x625a7;++address)memory[address]=std::uint8_t(next());
    seeking=promotion=exiting=false;
}
void forfeits(){
    for(unsigned division=0;division<4;++division)for(unsigned context:{0u,11u})for(unsigned position=0;position<3;++position)
    for(unsigned round=0;round<6;++round)for(unsigned variation=0;variation<4;++variation){
        fresh(original,division,context,position,variation);original[0x1ca35]=std::uint8_t(round);
        original[0x5eb7b]=std::uint8_t(round);original[0x1bbb4]=std::uint8_t(variation*0x40);
        for(unsigned id=0;id<12;++id){original[0x1c9de+id]=std::uint8_t(next()%8);original[0x1c9ea+id]=std::uint8_t(next()%8);}
        native=original;forfeitNativeSeason(native);execute(0x5d000,0x5d1be);compare("F1-remainder-of-season");
        require(!native[0x1ca35]&&native[0x1bb5b]==0xc0&&native[0x1bbb4]==0xc0,"F1 did not complete the original six-round remainder");++forfeitCases;
    }
}
void promotions(){
    for(unsigned division=0;division<4;++division)for(unsigned context:{0u,11u})for(unsigned position=0;position<3;++position)for(unsigned holes=0;holes<=10;++holes){
        fresh(original,division,context,position,holes);
        std::copy_n(original.begin()+0x1c9c2,12,original.begin()+0x1ca0e);original[0x1c9cf]=std::uint8_t(holes);
        native=original;finishNativeSeason(native);promotion=true;execute(0x64916);compare("promotion-relegation-championship");promotion=false;
        if(division==3&&position==0){
            if(!context){require(native[0x1c9d0]==11&&!native[0x1c9ce]&&native[0x593f6]==11,"Normal championship did not enter Super League");++normalChampions;}
            else {require(native[0x1c9d0]==11&&native[0x1c9ce]==3&&native[0x593f6]==0x80,"Super champion incorrectly requested a season award bitmap");++superChampions;}
            require(native[0x1c9cf]==10,"Champion did not receive original damage reset");
        }
        ++promotionCases;
    }
}
void damage(){
    for(unsigned division=0;division<4;++division)for(unsigned context:{0u,11u})for(unsigned holes=0;holes<=10;++holes)for(unsigned practice:{0u,1u}){
        fresh(original,division,context,2,holes);original[0x1c9cf]=std::uint8_t(holes);original[0x1bbb5]=std::uint8_t(10-holes);
        original[0x1ca22]=practice?0:0x80;native=original;
        const auto input=transitions::readInputs(native.data());auto state=transitions::readState(native.data());
        (void)scr::finishLocalRaceExit(input,state);transitions::writeState(native.data(),state);
        exiting=true;execute(0x5d674);compare("persistent-career-damage");exiting=false;
        require(native[0x1c9cf]==(practice?10-holes:holes),"Practice/season damage retention differs");++damageCases;
    }
}
bool seek(){
    const unsigned before=native[0x1ca35];
    const bool found=seekNativeSeasonRace(native);if(found)native[0x57c62]=0x11;
    seeking=true;execute(0x5d068);compare("six-round-fixture-seek");seeking=false;
    require(found==(pc==0x58580),"Original/native human fixture boundary differs");
    if(found){aiFixtures+=native[0x1ca35]-before;++humanFixtures;}
    else aiFixtures+=6-before;
    ++seekCases;return found;
}
void completeOriginalSeasons(){
    for(unsigned division=0;division<4;++division)for(unsigned context:{0u,11u})for(unsigned position=0;position<3;++position)for(unsigned outcome:{0u,0x40u,0x80u,0xc0u}){
        fresh(original,division,context,position,outcome/0x40);native=original;
        initializeNativeSeason(native);execute(0x609ae);execute(0x5d014,0x5d068);compare("complete-season-initialization");++initializationCases;
        unsigned human=0;
        while(seek()){
            const unsigned ordinal=2*division+(native[0x1ca35]&1);
            require(native[0x1ca33]==native[0x648c2+ordinal]&&native[0x1ca29]!=11,"Incorrect original career fixture or track");
            original[0x1bbb4]=native[0x1bbb4]=std::uint8_t(outcome);
            transferNativeLapRecords(native,false);scoreNativeSeasonRound(native);execute(0x5d126,0x5d136);compare("post-race-records-and-all-divisions");++scoreCases;
            advanceNativeSeasonRound(native);++advanceCases;++human;
            if(native[0x1ca35]>=6){
                require(!seekNativeSeasonRace(native),"Completed season unexpectedly found another fixture");
                execute(0x5d18c,0x5d1be);compare("post-race-season-completion");break;
            }
            execute(0x5d18c,0x5d068);compare("post-race-round-advance");
        }
        require(human==4,"Six-round season did not contain four human fixtures");
        // The original season-end header at0x645c6 selects the local division.
        original[0x1bb5f]=native[0x1bb5f]=std::uint8_t(division);
        finishNativeSeason(native);promotion=true;execute(0x64916);compare("complete-season-final-standings");promotion=false;++seasonSequences;
    }
}
void ready(NativeMenuFlow& flow){for(unsigned i=0;i<4;++i)(void)flow.update({},1);}
void choose(NativeMenuFlow& flow,unsigned row){
    ready(flow);(void)flow.update({0,std::uint8_t(1u<<row)},0);ready(flow);(void)flow.update({16,0},0);(void)flow.update({},1);
}
void finishFlowSeason(NativeMemory& state,NativeMenuFlow& flow,unsigned outcome){
    choose(flow,2);unsigned human=0,updates=0;
    while(flow.page()!=NativeMenuPage::Main){
        require(++updates<300,"Career result-page route did not terminate");
        if(flow.page()==NativeMenuPage::Matchup){
            (void)flow.update({},1);(void)flow.update({},1);(void)flow.update({16,0},1);
            require(flow.page()==NativeMenuPage::Preview,"Career matchup did not enter original overview");
            (void)flow.update({},1);const auto action=flow.update({16,0},1);
            require(action==NativeMenuAction::StartRace,"Career overview did not begin the race");
            state[0x1bbb4]=std::uint8_t(outcome);if(state[0x1c9cf])--state[0x1c9cf];
            flow.returnFromRace();++human;++flowRaces;
        }else{
            (void)flow.update({},1);(void)flow.update({},1);(void)flow.update({16,0},1);
        }
    }
    require(human==4,"Career flow omitted a human fixture");++flowSeasons;
}
void flowCareers(){
    auto state=std::make_unique<NativeMemory>(),loaded=std::make_unique<NativeMemory>(),replay=std::make_unique<NativeMemory>();
    fresh(*state,0,0,2,10);
    for(unsigned season=0;season<8;++season){
        const unsigned oldDivision=(*state)[0x1c9ce],oldContext=(*state)[0x1c9d0];
        NativeMenuFlow flow(*state);finishFlowSeason(*state,flow,0);
        const unsigned expectedContext=season<3?0:11;
        const unsigned expectedDivision=season<3?season+1:season==3?0:season<7?season-3:3;
        require((*state)[0x1c9ce]==expectedDivision&&(*state)[0x1c9d0]==expectedContext,"Eight winning seasons did not complete both original leagues");
        *replay=*state;require(prepareNativeReplay(*replay),"Stored career could not replay its season");
        require((*replay)[0x1c9ce]==oldDivision&&(*replay)[0x1c9d0]==oldContext,"Replay did not restore the season's starting division/context");++replayCases;
        beginNativeSaveTransaction(*state,true);const auto payload=makeNativeSavePayload(*state,NativeSaveKind::Career);restoreNativeSaveTransaction(*state);
        fresh(*loaded,0,0,2,0);beginNativeSaveTransaction(*loaded,false);
        require(applyNativeSavePayload(*loaded,NativeSaveKind::Career,payload),"Saved promoted career failed to load into fresh state");
        require(std::equal(state->begin()+0x1c9c2,state->begin()+0x1c9d1,loaded->begin()+0x1c9c2),"Career continuation lost order/division/damage/context");
        require(std::equal(state->begin()+0x1ed5b,state->begin()+0x1ed67,loaded->begin()+0x1ed5b),"Career continuation lost its entered name");
        require(std::equal(state->begin()+0x625a2,state->begin()+0x625a7,loaded->begin()+0x625a2),"Career continuation lost its original final RNG state");++saveRoundtrips;
        *state=*loaded;
    }
    for(unsigned context:{0u,11u}){
        fresh(*state,3,context,0,10);
        for(unsigned season=0;season<4;++season){
            const unsigned before=(*state)[0x1c9ce];NativeMenuFlow flow(*state);finishFlowSeason(*state,flow,0xc0);
            const unsigned expected=before?before-1:0;
            require((*state)[0x1c9ce]==expected&&(*state)[0x1c9d0]==context,"Losing career season did not relegate or retain the bottom division");++relegations;
        }
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
    require(argc==2,"Usage: native_career_state_tests payload.bin");std::ifstream file(argv[1],std::ios::binary);
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    require(payload.size()==408540,"Payload size mismatch");std::copy(payload.begin(),payload.end(),baseline.begin()+0xe700);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    forfeits();promotions();damage();completeOriginalSeasons();flowCareers();require(tieBreaks>0,"Original random tie-break branch was not exercised");
    std::cout<<"{\"cases\":"<<cases<<",\"forfeit_cases\":"<<forfeitCases<<",\"promotion_cases\":"<<promotionCases<<",\"damage_cases\":"<<damageCases
        <<",\"initialization_cases\":"<<initializationCases<<",\"seek_cases\":"<<seekCases<<",\"score_cases\":"<<scoreCases<<",\"advance_cases\":"<<advanceCases
        <<",\"season_sequences\":"<<seasonSequences<<",\"human_fixtures\":"<<humanFixtures<<",\"ai_fixtures\":"<<aiFixtures<<",\"random_tie_breaks\":"<<tieBreaks
        <<",\"flow_seasons\":"<<flowSeasons<<",\"flow_synthetic_races\":"<<flowRaces<<",\"save_roundtrips\":"<<saveRoundtrips<<",\"replay_cases\":"<<replayCases
        <<",\"normal_champion_cases\":"<<normalChampions<<",\"super_champion_cases\":"<<superChampions<<",\"relegation_seasons\":"<<relegations
        <<",\"compared_bytes\":"<<comparedBytes<<",\"original_instructions\":"<<instructionCount<<",\"maximum_instructions\":"<<maximumInstructions
        <<",\"mismatches\":0,\"whole_ram_compared\":true,\"original_code_unmodified\":true,\"drawing_and_fade_calls_excluded\":true,\"outcomes_injected_at_test_boundary\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
