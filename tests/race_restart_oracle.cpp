// Runs the complete original restart initializer, including geometry, physics,
// sound requests and both camera transforms. Native state never reads results.
#include "race_restart_fixture.h"
#include "driving_replay_fixture.h"
#include "m68k.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
namespace {
namespace f=scr::test::restart;
namespace replay=scr::test::replay;
f::Ram ram{},native{},baseline{};
std::unique_ptr<scr::PhysicsData> physics;
std::unique_ptr<scr::TrackProgressData> progress;
std::unique_ptr<scr::RaceRestartData> data;
std::vector<scr::AudioRegisterWrite> originalAudio,nativeAudio;
constexpr unsigned stackBase=0x8f000,stack=0x90000,sentinel=0xa0000;
std::array<std::uint8_t,4096> stackRam{};
unsigned pc=0,instructions=0,maximumInstructions=0;std::uint64_t cases=0,compared=0;
std::uint32_t seed=0x52535452;
std::uint32_t next(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
bool invalid=false,returned=false;std::string failure;
std::vector<std::uint8_t> readFile(const char* path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot read input");return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
void compare(const char* stage,const std::vector<unsigned>& values){for(auto a:values){++compared;if(ram[a]!=native[a]){std::ostringstream s;s<<"case="<<cases<<" stage="<<stage<<" address=0x"<<std::hex<<a<<std::dec<<" original="<<unsigned(ram[a])<<" native="<<unsigned(native[a]);throw std::runtime_error(s.str());}}}
void hook(unsigned a){
    pc=a;try{
        if(a==0x606bc)compare("prepare",f::outputs());
        if(a==0x606c2){f::camera(native);compare("first-camera",f::outputs());}
        if(a==0x606ce){f::geometry(native,*progress);compare("geometry",f::outputs());}
        if(a==0x606d4){auto s=scr::test::physics::readState(native);physics->audio=scr::test::audio::readData(native.data());nativeAudio=scr::updateGamePhysics(scr::test::physics::readInputs(native),*physics,s);scr::test::physics::writeState(native,s);compare("physics",replay::physicsOutputs());}
        if(a==0x60754){f::finish(native);compare("finish",f::outputs());}
        if(a==0x6075a){f::camera(native);compare("second-camera",f::outputs());}
        if(a==sentinel){f::chain(native);compare("chain",f::outputs());returned=true;m68k_end_timeslice();return;}
        if(a<0xe700||a>=0x724dc||++instructions>1000000)invalid=true;
    }catch(const std::exception& e){failure=e.what();invalid=true;}
    if(invalid)m68k_end_timeslice();
}
unsigned read(unsigned a){
    if(a<ram.size())return ram[a];if(a>=stackBase&&a<stack)return stackRam[a-stackBase];
    if(a>=sentinel&&a<sentinel+4){constexpr unsigned char stop[]{0x4e,0x72,0x27,0};return stop[a-sentinel];}
    if(a>=0xdff000&&a<0xdff200)return 0;invalid=true;return 0;
}
void write(unsigned a,unsigned v){
    if(a<ram.size()){ram[a]=std::uint8_t(v);return;}if(a>=stackBase&&a<stack){stackRam[a-stackBase]=std::uint8_t(v);return;}
    if(a>=0xdff000&&a<0xdff200)return;invalid=true;
}
void run(unsigned requested,unsigned flags){
    ram=baseline;ram[0x1bbc4]=flags&1?0x80:0;ram[0x1bbe1]=flags&2?0x80:0;ram[0x1bbcd]=flags&4?0xff:0;
    for(unsigned a=0x1bccc;a<0x1bd5e;++a)ram[a]=std::uint8_t(next());
    if(flags&8){ram[0x1ca2f]=1;ram[0x1c8e8]=std::uint8_t(requested);}
    *data=f::readData(ram);
    ram[0x1bb85]=std::uint8_t(requested);native=ram;f::prepare(native,*data,std::uint8_t(requested));
    originalAudio.clear();nativeAudio.clear();failure.clear();invalid=returned=false;instructions=0;
    m68k_pulse_reset();for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_D1,requested);m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stack-4);
    stackRam[4092]=std::uint8_t(sentinel>>24);stackRam[4093]=std::uint8_t(sentinel>>16);stackRam[4094]=std::uint8_t(sentinel>>8);stackRam[4095]=std::uint8_t(sentinel);
    m68k_set_reg(M68K_REG_PC,f::entry);m68k_execute(20000000);
    if(invalid||!returned||m68k_get_reg(nullptr,M68K_REG_SP)!=stack){std::ostringstream s;s<<"Original restart failed PC0x"<<std::hex<<pc<<" "<<failure;throw std::runtime_error(s.str());}
    if(originalAudio.size()!=nativeAudio.size())throw std::runtime_error("Restart audio count mismatch");
    for(unsigned n=0;n<originalAudio.size();++n)if(originalAudio[n].address!=nativeAudio[n].address||originalAudio[n].value!=nativeAudio[n].value)throw std::runtime_error("Restart audio order/value mismatch");
    maximumInstructions=std::max(maximumInstructions,instructions);++cases;
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){return read(a);}
unsigned int m68k_read_memory_16(unsigned int a){if(a&1)invalid=true;return (read(a)<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned int a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){write(a,v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(a&1)invalid=true;if(pc>=0xf362&&pc<0xf3ee&&a>=0xdff000&&a<0xdff200)originalAudio.push_back({a,std::uint16_t(v)});write(a,v>>8);write(a+1,v);}
void m68k_write_memory_32(unsigned int a,unsigned int v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc<4||(argc-2)%2)throw std::runtime_error("Usage: race_restart_oracle payload.bin label chipram.bin [...]");
    const auto payload=readFile(argv[1]);if(payload.size()!=408540)throw std::runtime_error("Payload size");
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    std::cout<<"{\"captures\":[";
    for(int a=2;a<argc;a+=2){const auto capture=readFile(argv[a+1]);if(capture.size()!=ram.size())throw std::runtime_error("Capture size");std::copy(capture.begin(),capture.end(),baseline.begin());
        if(baseline[0x57c3c])throw std::runtime_error("Restart oracle requires local mode");
        for(const auto range:std::array<std::pair<unsigned,unsigned>,4>{{{f::entry,f::end},{0x5b506,0x5b56c},{0x60cde,0x60cf8},{0x5daac,0x5dabe}}})if(!std::equal(payload.begin()+range.first-0xe700,payload.begin()+range.second-0xe700,baseline.begin()+range.first))throw std::runtime_error("Captured restart code differs");
        physics=std::make_unique<scr::PhysicsData>(scr::test::physics::readData(baseline));progress=std::make_unique<scr::TrackProgressData>(scr::test::trackProgress::readData(baseline));data=std::make_unique<scr::RaceRestartData>(f::readData(baseline));
        if(!data->track.segmentCount)throw std::runtime_error("Restart oracle requires a loaded track, unavailable in menu captures");
        const auto before=cases;for(unsigned segment=0;segment<data->track.segmentCount;++segment)for(unsigned flags=0;flags<16;++flags)run(segment,flags);
        if(a>2)std::cout<<',';std::cout<<"{\"label\":\""<<argv[a]<<"\",\"cases\":"<<cases-before<<'}';
    }
    std::cout<<"],\"total_cases\":"<<cases<<",\"mismatches\":0,\"compared_bytes\":"<<compared<<",\"maximum_instructions\":"<<maximumInstructions<<",\"original_code_unmodified\":true,\"native_state_reseeded_with_original_outputs\":false,\"whole_ram_compared\":false,\"stages\":[\"prepare\",\"first-camera\",\"geometry\",\"physics\",\"finish\",\"second-camera\",\"chain\"]}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
