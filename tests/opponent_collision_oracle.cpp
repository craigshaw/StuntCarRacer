#include "opponent_collision_fixture.h"
#include "m68k.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace f=scr::test::contact;
constexpr unsigned base=0xe700,positionEntry=0x636c0,forceEntry=0x63ce4,distanceEntry=0x6076c,stack=0x7f000,sentinel=0x7e000;
constexpr std::uint32_t seed=0x434f4e54,cpuSeed=0x3638434f;
struct Case {scr::OpponentContactData data;scr::OpponentContactInputs inputs;scr::OpponentContactState state;};
std::array<std::uint8_t,0x80000> ram{},expected{};
std::array<bool,0x80000> readable{},writable{};
std::uint32_t rng=seed,cpuRng=cpuSeed;
std::uint64_t total=0,totalInstructions=0,forceCases=0,positionCases=0,distanceCases=0,distancePositionCases=0;
constexpr std::array<unsigned,20> branchPcs{0x6372a,0x6377a,0x63786,0x637d0,0x63800,0x63826,0x63832,0x6383e,
    0x6389e,0x638e0,0x638f0,0x6391e,0x63cee,0x63cf0,0x63d4e,0x63d82,0x63dbc,0x63dc2,0x63e16,0x5c52c};
std::array<std::uint64_t,branchPcs.size()> branches{};
unsigned currentPc=0,instructions=0,maxInstructions=0;
bool executing=false,invalid=false,returned=false;
std::string failure;
std::uint32_t random32(std::uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
std::uint32_t next(){return random32(rng);}
std::vector<std::uint8_t> readFile(const char* p){std::ifstream s(p,std::ios::binary);if(!s)throw std::runtime_error("Cannot read fixture");return {std::istreambuf_iterator<char>(s),std::istreambuf_iterator<char>()};}
bool contains(unsigned a,unsigned n,unsigned first,unsigned end){return a>=first&&a<end&&n<=end-a;}
bool valid(unsigned a,unsigned n){return contains(a,n,0,unsigned(ram.size()))&&(n==1||!(a&1));}
void fail(const char* why,unsigned a){invalid=true;if(failure.empty())failure=std::string(why)+" address="+std::to_string(a)+" pc="+std::to_string(currentPc);}
void mark(std::array<bool,0x80000>& mask,unsigned a,unsigned n){std::fill(mask.begin()+a,mask.begin()+a+n,true);}
void hook(unsigned pc){
    currentPc=pc;if(invalid){m68k_end_timeslice();return;}
    if(pc==sentinel){returned=true;m68k_end_timeslice();return;}
    bool allowed=false;for(auto range:f::codeRanges)if(pc>=range.begin&&pc<range.end)allowed=true;
    if((pc&1)||!allowed||++instructions>300){fail("Invalid instruction/budget",pc);m68k_end_timeslice();return;}
    for(unsigned i=0;i<branchPcs.size();++i)if(pc==branchPcs[i])++branches[i];
}
bool permittedRead(unsigned a,unsigned n){
    if(!valid(a,n))return false;if(!executing)return true;
    if(contains(a,n,stack-32,stack+4)||contains(a,n,sentinel,sentinel+8))return true;
    for(unsigned i=0;i<n;++i)if(!readable[a+i])return false;return true;
}
bool permittedWrite(unsigned a,unsigned n){
    if(!executing||!valid(a,n))return false;if(contains(a,n,stack-32,stack))return true;
    if(currentPc>=distanceEntry&&currentPc<0x607d0)return contains(a,n,0x1bc38,0x1bc3a)||contains(a,n,0x1bc3e,0x1bc40)||contains(a,n,0x1bbb8,0x1bbb9);
    for(unsigned i=0;i<n;++i)if(!writable[a+i])return false;return true;
}
void initializeMasks(){
    for(auto r:f::codeRanges)mark(readable,r.begin,r.end-r.begin+4);
    for(auto r:f::dataRanges)mark(readable,r.begin,r.end-r.begin);
    for(auto a:f::inputWords)mark(readable,a,2);for(auto a:f::inputBytes)mark(readable,a,1);
    for(auto a:f::stateWords){mark(readable,a,2);mark(writable,a,2);}
    for(auto a:f::stateBytes){mark(readable,a,1);mark(writable,a,1);}
    mark(readable,0x1c7e0,200);mark(readable,0x1ca1e,2);mark(readable,0x1bb0a,4);mark(readable,0x1bb1c,2);
}
scr::OpponentContactState check(const Case& c,bool forceOnly=false){
    f::writeData(ram.data(),c.data);f::writeInputs(ram.data(),c.inputs);f::writeState(ram.data(),c.state);
    const auto entry=forceOnly?forceEntry:positionEntry;
    f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,entry);f::putLong(ram.data(),stack,sentinel);
    f::putWord(ram.data(),sentinel,0x4e72);f::putWord(ram.data(),sentinel+2,0x2700);
    executing=false;m68k_pulse_reset();
    for(unsigned reg=0;reg<15;++reg)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+reg),random32(cpuRng));
    if(!forceOnly){m68k_set_reg(M68K_REG_D1,random32(cpuRng)&0xffff00ffu);m68k_set_reg(M68K_REG_D2,random32(cpuRng)&0xffff00ffu);}
    m68k_set_reg(M68K_REG_SR,0x2700|(random32(cpuRng)&31));m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);
    invalid=returned=false;instructions=0;failure.clear();currentPc=entry;executing=true;m68k_execute(20000);executing=false;
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4)
        throw std::runtime_error("Opponent execution failure "+failure+" case="+std::to_string(total));
    totalInstructions+=instructions;maxInstructions=std::max(maxInstructions,instructions);
    auto state=c.state;if(forceOnly)scr::calculateOpponentCollisionForces(c.inputs,state);else scr::handleOpponentPositioning(c.data,c.inputs,state);
    f::writeState(expected.data(),state);
    auto compare=[&](unsigned a){if(ram[a]!=expected[a]){
        std::cerr<<"Opponent mismatch case="<<total<<" forceOnly="<<forceOnly<<" address=0x"<<std::hex<<a<<std::dec
                 <<" original="<<unsigned(ram[a])<<" native="<<unsigned(expected[a])<<" distance="<<c.inputs.opponentDistance
                 <<" playerLateral="<<unsigned(c.inputs.playerLateral)<<" opponentLateral="<<unsigned(c.state.opponentLateral)<<'\n';
        throw std::runtime_error("Opponent state mismatch");}};
    for(auto a:f::stateWords){compare(a);compare(a+1);}for(auto a:f::stateBytes)compare(a);
    ++total;if(forceOnly)++forceCases;else ++positionCases;return state;
}
Case randomCase(){
    Case c{};auto& d=c.data;auto& i=c.inputs;auto& s=c.state;
    d.segmentCount=std::uint8_t(next()%100+1);for(auto& v:d.properties)v=std::uint8_t(next());for(auto& v:d.behaviorTraits)v=std::uint8_t(next());for(auto& v:d.geometryFlags)v=std::uint8_t(next());
    i={std::uint8_t(next()%32),std::uint8_t(next()),std::uint16_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint16_t(next()),
       std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next()),std::uint8_t(next()%d.segmentCount)};
    s={std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),
       {std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next())},{std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next())},std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next())};return c;
}
Case contactCase(){
    auto c=randomCase();c.inputs.opponentDistance=4;c.inputs.networkMode=0;c.inputs.lateralRoadPosition=0x80;
    c.inputs.opponentAhead=0;c.inputs.movementOverride=0;c.inputs.raceStartComplete=1;c.inputs.aiEnabled=1;c.inputs.wheelMovementActive=0;
    c.inputs.carHeight=c.inputs.opponentHeight=0;c.inputs.crashRecoveryTimer=0;c.inputs.playerLateral=0x80;
    c.state.opponentLateral=0x80;c.state.lateralDistance=0;c.state.collisionTimer=1;c.state.collisionLatched=0;
    c.data.geometryFlags.fill(0);return c;
}
void checkDistance(const scr::PlayerDistanceData& data,const scr::PlayerDistanceInputs& inputs,bool compose=false,Case c={}){
    c.inputs.opponentSegment=inputs.opponentSegment;
    f::writeData(ram.data(),c.data);f::writeInputs(ram.data(),c.inputs);f::writeState(ram.data(),c.state);
    f::writeDistanceData(ram.data(),data);f::writeDistanceInputs(ram.data(),inputs);
    f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,distanceEntry);f::putLong(ram.data(),stack,sentinel);
    // The hook ends the timeslice before this NOP. Musashi may still execute
    // that fetched instruction, so STOP would incorrectly stop the next call
    // when preserving CPU state across distance and positioning.
    f::putWord(ram.data(),sentinel,0x4e71);f::putWord(ram.data(),sentinel+2,0x4e71);
    executing=false;m68k_pulse_reset();for(unsigned reg=0;reg<15;++reg)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+reg),random32(cpuRng));
    m68k_set_reg(M68K_REG_D1,random32(cpuRng)&0xffff00ffu);m68k_set_reg(M68K_REG_D2,random32(cpuRng)&0xffff00ffu);
    m68k_set_reg(M68K_REG_SR,0x2700|(random32(cpuRng)&31));
    auto run=[&](unsigned entry){
        f::putLong(ram.data(),stack,sentinel);m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);
        invalid=returned=false;instructions=0;failure.clear();currentPc=entry;executing=true;m68k_execute(20000);executing=false;
        if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4)throw std::runtime_error("Distance/composed execution failure "+failure+
            " case="+std::to_string(total)+" entry="+std::to_string(entry)+" pc="+std::to_string(currentPc)+" returned="+std::to_string(returned)+
            " sp="+std::to_string(m68k_get_reg(nullptr,M68K_REG_SP))+" instructions="+std::to_string(instructions));
        totalInstructions+=instructions;maxInstructions=std::max(maxInstructions,instructions);
    };
    run(distanceEntry);
    const auto native=scr::calculatePlayerDistance(data,inputs),actual=f::readDistanceState(ram.data());
    if(native.signedDifference!=actual.signedDifference||native.distance!=actual.distance||native.opponentAhead!=actual.opponentAhead)
        throw std::runtime_error("Player distance mismatch case="+std::to_string(total));
    if(compose){
        // Preserve the original distance routine's registers and RAM into its
        // immediately following positioner; only model the caller's JSR stack.
        run(positionEntry);c.inputs.opponentDistance=native.distance;c.inputs.opponentAhead=native.opponentAhead;
        scr::handleOpponentPositioning(c.data,c.inputs,c.state);f::writeState(expected.data(),c.state);
        for(auto a:f::stateWords)for(unsigned k=0;k<2;++k)if(ram[a+k]!=expected[a+k])throw std::runtime_error("Distance-position word mismatch");
        for(auto a:f::stateBytes)if(ram[a]!=expected[a])throw std::runtime_error("Distance-position byte mismatch");
        ++distancePositionCases;
    }
    ++distanceCases;++total;
}
}

extern "C"{
unsigned int m68k_read_memory_8(unsigned int a){if(!permittedRead(a,1)){fail("Invalid read",a);return 0;}return ram[a];}
unsigned int m68k_read_memory_16(unsigned int a){if(!permittedRead(a,2)){fail("Invalid read",a);return 0;}return f::word(ram.data(),a);}
unsigned int m68k_read_memory_32(unsigned int a){if(!permittedRead(a,4)){fail("Invalid read",a);return 0;}return f::longword(ram.data(),a);}
void m68k_write_memory_8(unsigned int a,unsigned int v){if(!permittedWrite(a,1)){fail("Invalid write",a);return;}ram[a]=std::uint8_t(v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(!permittedWrite(a,2)){fail("Invalid write",a);return;}f::putWord(ram.data(),a,std::uint16_t(v));}
void m68k_write_memory_32(unsigned int a,unsigned int v){if(!permittedWrite(a,4)){fail("Invalid write",a);return;}f::putLong(ram.data(),a,v);}
}

int main(int argc,char** argv)try{
    if(argc<2||(argc-2)%2)throw std::runtime_error("Usage: opponent_collision_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Payload size mismatch");
    std::copy(payload.begin(),payload.end(),ram.begin()+base);initializeMasks();m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"seed\":"<<seed<<",\"cpu_register_seed\":"<<cpuSeed<<",\"captures\":[";
    for(int arg=2;arg<argc;arg+=2){
        const std::string label=argv[arg];if(label.empty()||label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)throw std::runtime_error("Invalid capture label");
        const auto capture=readFile(argv[arg+1]);if(capture.size()!=ram.size())throw std::runtime_error("Capture size mismatch");
        for(auto range:f::codeRanges)if(!std::equal(payload.begin()+range.begin-base,payload.begin()+range.end-base,capture.begin()+range.begin))throw std::runtime_error("Capture code mismatch");
        std::copy(capture.begin(),capture.end(),ram.begin());Case c{f::readData(ram.data()),f::readInputs(ram.data()),f::readState(ram.data())};check(c,true);
        const bool validData=c.data.segmentCount>0&&c.data.segmentCount<=100&&c.inputs.opponentSegment<c.data.segmentCount&&c.inputs.opponentId<32;
        if(validData)check(c);
        const auto distanceInputs=f::readDistanceInputs(capture.data());
        const bool validDistance=distanceInputs.opponentSegment<100&&distanceInputs.playerSegment<100;
        if(validDistance)checkDistance(f::readDistanceData(capture.data()),distanceInputs,validData,c);
        if(arg>2)std::cout<<',';std::cout<<"{\"label\":\""<<label<<"\",\"force_checked\":true,\"position_checked\":"<<(validData?"true":"false")<<",\"distance_checked\":"<<(validDistance?"true":"false")<<"}";
    }
    const auto captureCases=total;ram.fill(0);std::copy(payload.begin(),payload.end(),ram.begin()+base);
    auto c=contactCase();const auto wordsStart=total;
    for(unsigned timer:{0u,1u,255u}){c.state.collisionTimer=std::uint8_t(timer);for(unsigned value=0;value<65536;++value){c.inputs.carHeight=std::uint16_t(value);check(c,true);}}
    c=contactCase();c.inputs.wheelMovementActive=1;c.state.lateralDistance=0xff;c.state.collisionLatched=0x80;
    for(unsigned axis=0;axis<3;++axis)for(unsigned value=0;value<65536;++value){c.state.forces[axis]=std::uint16_t(value);check(c,true);}
    c=contactCase();c.inputs.wheelMovementActive=1;
    for(unsigned value=0;value<65536;++value){c.inputs.opponentSpeed=std::uint16_t(value);c.inputs.playerForwardSpeed=0;check(c,true);}
    const auto forceWordCases=total-wordsStart;const auto lateralStart=total;
    c=contactCase();for(unsigned player=0;player<256;++player)for(unsigned opponent=0;opponent<256;++opponent){c.inputs.playerLateral=std::uint8_t(player);c.state.opponentLateral=std::uint8_t(opponent);check(c);}
    c=contactCase();for(unsigned value=0;value<65536;++value){c.inputs.lateralRoadPosition=std::uint16_t(value);check(c);}
    c=contactCase();for(unsigned value=0;value<65536;++value){c.inputs.opponentDistance=std::uint16_t(value);check(c);}
    const auto contactWordCases=total-lateralStart;const auto behaviorStart=total;
    c=contactCase();for(unsigned traits=0;traits<256;++traits)for(unsigned distance=0;distance<256;++distance){
        c.data.behaviorTraits[c.inputs.opponentId]=std::uint8_t(traits);c.inputs.opponentDistance=std::uint16_t(distance);
        c.inputs.opponentAhead=std::uint8_t(traits);c.inputs.playerLateral=std::uint8_t(distance^0x80);check(c);}
    c=contactCase();for(unsigned overrideValue=0;overrideValue<256;++overrideValue)for(unsigned lateral=0;lateral<256;++lateral){
        c.inputs.movementOverride=std::uint8_t(overrideValue);c.state.opponentLateral=std::uint8_t(lateral);check(c);}
    c=contactCase();for(unsigned flag=0;flag<256;++flag){
        auto item=c;item.inputs.raceStartComplete=std::uint8_t(flag);check(item,true);item=c;item.inputs.aiEnabled=std::uint8_t(flag);check(item,true);
        item=c;item.inputs.wheelMovementActive=std::uint8_t(flag);check(item,true);item=c;item.inputs.crashRecoveryTimer=std::uint8_t(flag);check(item,true);
        item=c;item.state.collisionLatched=std::uint8_t(flag);check(item,true);item=c;item.state.lateralDirection=std::uint8_t(flag);check(item,true);
        item=c;item.inputs.networkMode=std::uint8_t(flag);item.inputs.distanceOffset=1;check(item);item=c;item.inputs.networkMode=1;item.inputs.distanceOffset=std::uint8_t(flag);check(item);
    }
    for(unsigned count=1;count<=100;++count)for(unsigned segment=0;segment<count;++segment){
        c=contactCase();c.data.segmentCount=std::uint8_t(count);c.inputs.opponentSegment=std::uint8_t(segment);c.inputs.opponentDistance=0x100;
        for(auto& flag:c.data.geometryFlags)flag=std::uint8_t(next());check(c);}
    const auto behaviorCases=total-behaviorStart;constexpr unsigned randomCases=200000;
    for(unsigned n=0;n<randomCases;++n){c=randomCase();if(n%3==0)c.inputs.opponentDistance=std::uint8_t(next());
        if(n%5==0){c.inputs.networkMode=0;c.inputs.lateralRoadPosition=std::uint16_t(next()%0x180);}
        if(n%7==0)c.inputs.movementOverride=0;check(c,n%2==0);}
    const auto sequenceStart=total;
    for(unsigned direction:{0u,0x80u}){
        c=contactCase();c.inputs.opponentAhead=std::uint8_t(direction);c.data.behaviorTraits[c.inputs.opponentId]=0x2a;
        for(unsigned frame=0;frame<1024;++frame){
            c.inputs.opponentDistance=std::uint16_t((frame<512?511-frame:frame-512)/4);
            c.inputs.opponentHeight=std::uint16_t((frame%41)-20);c.inputs.opponentSpeed=std::uint16_t(0x500+(frame%97));
            c.inputs.playerForwardSpeed=0x530;c.state=check(c);
        }
    }
    const auto sequentialCases=total-sequenceStart;
    scr::PlayerDistanceData distanceData{};scr::PlayerDistanceInputs distanceInputs{};
    distanceData.lapLength=0x6780;for(unsigned index=0;index<100;++index)distanceData.segmentWorldPositions[index]=std::uint16_t(index*0x123);
    for(unsigned value=0;value<65536;++value){distanceInputs.opponentProgress=std::uint16_t(value);checkDistance(distanceData,distanceInputs);}
    distanceInputs={0,0,1,0};for(unsigned value=0;value<65536;++value){distanceData.segmentWorldPositions[1]=std::uint16_t(value);checkDistance(distanceData,distanceInputs);}
    distanceData.segmentWorldPositions[1]=0x8000;for(unsigned value=0;value<65536;++value){distanceData.lapLength=std::uint16_t(value);checkDistance(distanceData,distanceInputs);}
    for(unsigned opponent=0;opponent<100;++opponent)for(unsigned player=0;player<100;++player){
        distanceInputs.opponentSegment=std::uint8_t(opponent);distanceInputs.playerSegment=std::uint8_t(player);checkDistance(distanceData,distanceInputs);}
    for(unsigned n=0;n<20000;++n){c=contactCase();
        distanceInputs={std::uint16_t(next()),std::uint16_t(next()),std::uint8_t(next()%c.data.segmentCount),std::uint8_t(next()%c.data.segmentCount)};
        for(auto& position:distanceData.segmentWorldPositions)position=std::uint16_t(next());distanceData.lapLength=std::uint16_t(next());
        checkDistance(distanceData,distanceInputs,true,c);
    }
    unsigned rejected=0;for(unsigned kind=0;kind<4;++kind){c=contactCase();if(kind==0)c.inputs.opponentId=32;if(kind==1)c.data.segmentCount=0;
        if(kind==2)c.data.segmentCount=101;if(kind==3)c.inputs.opponentSegment=c.data.segmentCount;
        try{scr::handleOpponentPositioning(c.data,c.inputs,c.state);}catch(const std::invalid_argument&){++rejected;}}
    if(rejected!=4)throw std::runtime_error("Invalid table domain accepted");
    for(unsigned i=0;i<branches.size();++i)if(!branches[i])throw std::runtime_error("Missing opponent branch at "+std::to_string(branchPcs[i]));
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"force_word_cases\":"<<forceWordCases<<",\"contact_word_cases\":"<<contactWordCases
        <<",\"behavior_cases\":"<<behaviorCases<<",\"random_cases\":"<<randomCases<<",\"sequential_cases\":"<<sequentialCases
        <<",\"force_only_cases\":"<<forceCases<<",\"position_cases\":"<<positionCases<<",\"total_cases\":"<<total<<",\"mismatches\":0"
        <<",\"distance_cases\":"<<distanceCases<<",\"distance_position_cases\":"<<distancePositionCases
        <<",\"invalid_domains_rejected\":"<<rejected<<",\"maximum_instructions\":"<<maxInstructions<<",\"total_instructions\":"<<totalInstructions
        <<",\"entry_d1_d2_high_bytes\":0,\"controlled_trajectory_is_ai_simulation\":false,\"branches\":[";
    for(unsigned i=0;i<branches.size();++i){if(i)std::cout<<',';std::cout<<branches[i];}std::cout<<"]}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
