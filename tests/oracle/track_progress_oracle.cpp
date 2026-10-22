// Execute original camera code, then the untouched local-practice gameplay
// prefix of processGameFrame, stopping at its explicit rendering boundary.
#include "track_progress_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
namespace f=scr::test::trackProgress;
using Ram=f::Ram;
constexpr unsigned stack=0x7f000,harness=0x7d000,boundary=0x64f80;
constexpr std::uint32_t seed=0x54524143,cpuSeed=0x36385052;
const std::vector<std::array<unsigned,2>> spans{
 {0x600a6,0x60246},{0x5fe04,0x5fe56},{0x5ff94,0x600a6},{0x5be44,0x5c0aa},
 {0x5c138,0x5c1d0},{0x5c3da,0x5c484},{0x5c51a,0x5c554},{0x5fe56,0x5ff94},
 {0x5c65a,0x5c6b8},{0x5c6c4,0x5c808},{0x6135a,0x61368},{0x64d66,0x64e18},
 {0x60246,0x602e4},{0x60cde,0x60cf8},{0x64e52,0x64ede},{0x64ef6,0x64f80}};
Ram ram{},expected{},captured{};
std::array<bool,0x80000> readable{},writable{};
std::uint32_t rng=seed,cpuRng=cpuSeed;
std::uint64_t total=0,totalInstructions=0,foundCases=0,offGridCases=0;
std::map<unsigned,std::uint64_t> coverage;
bool executing=false,invalid=false,finished=false;
unsigned pcNow=0,instructions=0,maximumInstructions=0;
std::string failure;
std::uint32_t next(std::uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
std::uint32_t random(){return next(rng);}
bool range(unsigned a,unsigned n,unsigned b,unsigned e){return a>=b&&a<e&&n<=e-a;}
void fail(const char* why,unsigned a){invalid=true;if(!failure.empty())return;std::ostringstream o;o<<why<<" address=0x"<<std::hex<<a<<" PC=0x"<<pcNow;failure=o.str();}
void allow(unsigned a,unsigned n){std::fill(readable.begin()+a,readable.begin()+a+n,true);}
std::vector<std::uint8_t> file(const char* p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error(std::string("Cannot read ")+p);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
void permissions(){
    for(auto s:spans)allow(s[0],s[1]-s[0]+4);
    allow(harness,16);allow(stack-32,32);
    for(auto a:f::outputBytes())readable[a]=writable[a]=true;
    for(unsigned a:{0x1bce4,0x1bce6,0x1bd38})allow(a,2);
    allow(0x1bcdc,4);allow(0x1bc4b,1);allow(0x1bc45,1);allow(0x57c3c,1);allow(0x1ca22,1);allow(0x1ca1a,1);
    allow(0x1c280,256);allow(0x1c4c0,100);allow(0x1c524,100);allow(0x1c588,100);allow(0x1c5ec,100);
    allow(0x1c650,400);allow(0x1cc46,8192);allow(0x1ef82,0x10100);allow(0x5c6b8,12);
}
bool readAllowed(unsigned a,unsigned n){
    if(!range(a,n,0,unsigned(ram.size()))||(n>1&&(a&1)))return false;
    if(!executing)return true;for(unsigned i=0;i<n;++i)if(!readable[a+i])return false;return true;
}
bool writeAllowed(unsigned a,unsigned n){
    if(!executing||!range(a,n,0,unsigned(ram.size()))||(n>1&&(a&1))){fail("Invalid write",a);return false;}
    if(range(a,n,stack-32,stack)) {
        const auto opcode=f::readWord(ram,pcNow);
        if(opcode!=0x4eb9&&opcode!=0x3f00){fail("Unexpected stack write",a);return false;}return true;
    }
    for(unsigned i=0;i<n;++i)if(!writable[a+i]){fail("Write outside track outputs",a);return false;}return true;
}
void hook(unsigned pc){
    pcNow=pc;if(invalid){m68k_end_timeslice();return;}
    if(pc==boundary){finished=true;m68k_end_timeslice();return;}
    bool permitted=range(pc,2,harness,harness+12);for(auto s:spans)permitted|=range(pc,2,s[0],s[1]);
    if(!permitted||(pc&1)||++instructions>1800){fail("Invalid PC/instruction budget",pc);m68k_end_timeslice();return;}
    for(unsigned p:{0x64e8e,0x64ef6,0x6001c,0x5be9a,0x5bece,0x5bf50,0x5c072,0x602be,0x602c6,
                    0x600c8,0x600e0,0x64d82,0x64d92,0x64dc0,0x5c16a,0x5c194})if(pc==p)++coverage[p];
}
void original(){
    executing=false;f::writeLong(ram,0,stack);f::writeLong(ram,4,harness);
    f::writeWord(ram,harness,0x4eb9);f::writeLong(ram,harness+2,0x60190);
    f::writeWord(ram,harness+6,0x4ef9);f::writeLong(ram,harness+8,0x64e52);
    m68k_pulse_reset();
    for(unsigned r=0;r<15;++r){auto v=next(cpuRng);if(r==1||r==2)v&=0xffff00ff;m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),v);}
    m68k_set_reg(M68K_REG_SR,0x2700|(next(cpuRng)&31));m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,harness);
    invalid=finished=false;failure.clear();instructions=0;executing=true;m68k_execute(100000);executing=false;
    if(invalid||!finished||m68k_get_reg(nullptr,M68K_REG_SP)!=stack)throw std::runtime_error("Track progress original failure case="+std::to_string(total)+": "+failure);
    totalInstructions+=instructions;maximumInstructions=std::max(maximumInstructions,instructions);
}
void check(const scr::TrackProgressData& d){
    std::copy(ram.begin()+0x1bb00,ram.begin()+0x1bdf0,expected.begin()+0x1bb00);
    for(auto a:f::outputBytes())expected[a]=ram[a];
    auto state=f::readState(expected);const bool found=scr::updateTrackProgress(d,f::readInputs(expected),state);f::writeState(expected,state);original();
    // Strict write allow-list rejects all other writes; compare every state byte
    // and surrounding retained holes, including the two renderer scratch bytes.
    for(unsigned a=0x1bb00;a<0x1bdf0;++a)if(ram[a]!=expected[a]){
        std::ostringstream s;s<<"Track progress mismatch case="<<total<<" address=0x"<<std::hex<<a<<" original="<<unsigned(ram[a])<<" native="<<unsigned(expected[a]);throw std::runtime_error(s.str());}
    for(auto a:f::outputBytes())if(ram[a]!=expected[a])throw std::runtime_error("Track progress output mismatch at "+std::to_string(a));
    if(found)++foundCases;else ++offGridCases;++total;
}
void reset(){
    // All other RAM is immutable under the original write allow-list. Reset
    // only the mutable neighborhood/outputs rather than copying512K per case.
    std::copy(captured.begin()+0x1bb00,captured.begin()+0x1bdf0,ram.begin()+0x1bb00);
    for(auto a:f::outputBytes())ram[a]=captured[a];
    ram[0x57c3c]=0;ram[0x1ca22]=captured[0x1ca22]&0x7f;
}
void position(unsigned cell,unsigned x,unsigned z){
    f::writeLong(ram,0x1bcd8,(std::uint32_t(cell&15)<<23)|(std::uint32_t(x)<<15)|1);
    f::writeLong(ram,0x1bce0,(std::uint32_t(cell>>4)<<23)|(std::uint32_t(z)<<15)|1);
}
void randomState(){
    for(auto a:f::outputBytes())ram[a]=std::uint8_t(random());
    for(unsigned a:{0x1bce4,0x1bce6,0x1bd38})f::writeWord(ram,a,std::uint16_t(random()));
    ram[0x1bc4b]=std::uint8_t(random());ram[0x1bc45]=std::uint8_t(random());
}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){if(!readAllowed(a,1)){fail("Invalid read",a);return 0;}return ram[a];}
unsigned int m68k_read_memory_16(unsigned int a){if(!readAllowed(a,2)){fail("Invalid read",a);return 0;}return f::readWord(ram,a);}
unsigned int m68k_read_memory_32(unsigned int a){if(!readAllowed(a,4)){fail("Invalid read",a);return 0;}return f::readLong(ram,a);}
void m68k_write_memory_8(unsigned int a,unsigned int v){if(writeAllowed(a,1))ram[a]=std::uint8_t(v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(writeAllowed(a,2))f::writeWord(ram,a,std::uint16_t(v));}
void m68k_write_memory_32(unsigned int a,unsigned int v){if(writeAllowed(a,4))f::writeLong(ram,a,v);}
}
int main(int argc,char** argv)try{
    if(argc<4||(argc-2)%2)throw std::runtime_error("Usage: track_progress_oracle payload label chipram [...]");
    const auto payload=file(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Unexpected payload size");
    permissions();m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    unsigned validCaptures=0,skipped=0;std::uint64_t gridCases=0,sweepCases=0,randomCases=0,subGridCases=0;
    std::cout<<"{\"seed\":"<<seed<<",\"cpu_register_seed\":"<<cpuSeed<<",\"captures\":[";
    for(int arg=2;arg<argc;arg+=2){
        const std::string label=argv[arg];if(label.empty()||label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)throw std::runtime_error("Invalid capture label");
        auto bytes=file(argv[arg+1]);if(bytes.size()!=ram.size())throw std::runtime_error("Expected 512K capture");
        for(auto s:spans)if(!std::equal(payload.begin()+s[0]-0xe700,payload.begin()+s[1]-0xe700,bytes.begin()+s[0]))throw std::runtime_error("Captured code differs from original");
        for(auto s:{std::array<unsigned,2>{0x1cc46,0x1ec46},std::array<unsigned,2>{0x5c6b8,0x5c6c4}})
            if(!std::equal(payload.begin()+s[0]-0xe700,payload.begin()+s[1]-0xe700,bytes.begin()+s[0]))throw std::runtime_error("Captured math table differs from original");
        std::copy(bytes.begin(),bytes.end(),captured.begin());ram=captured;reset();const auto d=f::readData(ram);
        if(arg>2)std::cout<<',';std::cout<<"{\"label\":\""<<label<<'"';
        if(!d.track.segmentCount){++skipped;std::cout<<",\"skipped\":\"No loaded track\"}";continue;}
        ++validCaptures;const auto begin=total;check(d);
        const auto beforeGrid=total;
        constexpr std::array<unsigned,11> fractions{0,1,2,63,64,127,128,191,253,254,255};
        for(unsigned cell=0;cell<256;++cell)for(auto x:fractions)for(auto z:fractions)for(unsigned rotation=0;rotation<4;++rotation){
            reset();position(cell,x,z);f::writeWord(ram,0x1bce6,std::uint16_t(rotation*0x4000));
            ram[0x1bb9c]=std::uint8_t((cell%4)*0x40);check(d);}
        gridCases+=total-beforeGrid;
        // Exhaust all sub-grid pairs for the first instance of each descriptor
        // type in the loaded track, covering straight, diagonal and curved cells.
        const auto beforeSub=total;std::array<bool,16> seen{};
        for(unsigned segment=0;segment<d.track.segmentCount;++segment){
            const unsigned type=d.track.properties[segment]&15;if(seen[type])continue;seen[type]=true;
            const auto cell=d.segmentCoordinates[segment];
            for(unsigned pair=0;pair<65536;++pair){reset();position(cell,pair>>8,pair&255);check(d);}
        }
        subGridCases+=total-beforeSub;
        const auto beforeSweep=total;
        for(unsigned field=0;field<5;++field)for(unsigned value=0;value<65536;++value){
            reset();
            if(field<3)f::writeWord(ram,std::array<unsigned,3>{0x1bce4,0x1bce6,0x1bd38}[field],std::uint16_t(value));
            else if(field==3){ram[0x1bc4b]=std::uint8_t(value>>8);ram[0x1bc45]=std::uint8_t(value);}
            else {f::writeWord(ram,0x1bcd8+2,std::uint16_t(value));f::writeWord(ram,0x1bce0+2,std::uint16_t(65535-value));}
            check(d);
        }
        sweepCases+=total-beforeSweep;
        for(unsigned i=0;i<100000;++i){reset();randomState();const auto cell=random()&255;position(cell,random()&255,random()&255);
            if((i&31)==0)f::writeLong(ram,0x1bcd8,random());if((i&31)==1)f::writeLong(ram,0x1bce0,random());
            check(d);++randomCases;}
        std::cout<<",\"segment_count\":"<<unsigned(d.track.segmentCount)<<",\"cases\":"<<total-begin<<'}';
    }
    if(!validCaptures)throw std::runtime_error("Loaded-track capture required");
    if(foundCases!=coverage[0x64ef6]||offGridCases!=coverage[0x64e8e])throw std::runtime_error("Native/original segment-found decisions differ");
    for(unsigned p:{0x64e8e,0x64ef6,0x6001c,0x5be9a,0x5bece,0x5bf50,0x602be,0x602c6,0x600c8,0x600e0,0x64d82,0x64d92,0x64dc0,0x5c16a,0x5c194})
        if(!coverage[p])throw std::runtime_error("Missing track progress branch "+std::to_string(p));
    std::cout<<"],\"total_cases\":"<<total<<",\"mismatches\":0,\"grid_boundary_cases\":"<<gridCases<<",\"exhaustive_subgrid_cases\":"<<subGridCases
             <<",\"word_sweep_cases\":"<<sweepCases<<",\"random_cases\":"<<randomCases<<",\"found_cases\":"<<foundCases<<",\"off_grid_cases\":"<<offGridCases
             <<",\"valid_track_captures\":"<<validCaptures<<",\"skipped_captures\":"<<skipped<<",\"total_instructions\":"<<totalInstructions
             <<",\"maximum_instructions\":"<<maximumInstructions<<",\"original_boundary\":\"0x64f80\",\"renderer_executed\":false,\"branch_counts\":{";
    bool comma=false;for(auto [pc,count]:coverage){if(comma)std::cout<<',';comma=true;std::cout<<'"'<<pc<<"\":"<<count;}
    std::cout<<"},\"final_prng_state\":"<<rng<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
