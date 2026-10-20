// Headless execution of the original main-loop gameplay interval. Renderer CPU
// instructions run; pixel output and asynchronous Amiga devices are boundaries.
#include "driving_replay_io.h"
#include "m68k.h"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
namespace a=scr::test::audio;
namespace replay=scr::test::replay;
namespace fx=scr::test::effects;
scr_test::ChipRam ram{};
std::unique_ptr<replay::NativeReplay> native;
unsigned currentTick=0;
bool fullLoop=false,outerRunning=false,firstEntry=true,restarting=false;
std::vector<std::uint8_t> savedCpu;
unsigned originalRestarts=0,originalWaitVblanks=0;
unsigned observedAction=0;
std::string failure;
std::uint64_t compared=0,totalInstructions=0;
std::uint64_t comparedAudioWrites=0;
std::vector<scr::AudioRegisterWrite> originalAudio;
std::map<std::string,std::vector<unsigned>> projections;
std::map<std::string,unsigned> stages;
void compare(const std::string& stage,const std::vector<unsigned>& bytes){
    projections[stage]=bytes;
    ++stages[stage];
    for(auto address:bytes){++compared;if(ram[address]!=native->ram[address]){
        std::ostringstream s;s<<"tick="<<currentTick<<" stage="<<stage<<" address=0x"<<std::hex<<address<<std::dec<<" original="<<unsigned(ram[address])<<" native="<<unsigned(native->ram[address]);
        throw std::runtime_error(s.str());}}
}
std::vector<unsigned> range(unsigned a,unsigned count){std::vector<unsigned> v;while(count--)v.push_back(a++);return v;}
void stage(unsigned address){
    if(address==0x5d48a){native->controlsStage();std::vector<unsigned> v(scr::test::controls::byteOutputs.begin(),scr::test::controls::byteOutputs.end());for(auto a:range(0x625a2,4))v.push_back(a);v.push_back(0x1bd2a);v.push_back(0x1bd2b);compare("controls",v);}
    if(address==0x5d490){native->physicsStage();compare("physics",replay::physicsOutputs());}
    if(address==0x5d496){native->engineStage();compare("engine",{0x1bbf4,0x1bbf5,0x5b646,0x625a2,0x625a3,0x625a4,0x625a5,0x625a6});}
    if(address==0x64f80){native->progressStage();
        // Renderer arithmetic reuses polar/view scratch after this boundary.
        // Compare the complete persistent gameplay projection, including all
        // track outputs consumed by the next physics update.
        std::vector<unsigned> v{0x1bb1c,0x1bb9b,0x1bb9c,0x1bbd1,0x1bbe1,0x1bb41,0x1bb75,0x1bb9f,0x1bb9e};
        for(auto a0:{0x1bcd0,0x1bc5e,0x1bc5c,0x1bb10,0x1bd5a,0x1bb0a,0x1bc28})for(auto a1:range(a0,2))v.push_back(a1);
        for(auto a0:range(0x1bcd8,12))v.push_back(a0);for(auto a0:range(0x1c3c0,64))v.push_back(a0);compare("track",v);}
    if(address==0x65102){native->groundStage();compare("particles",range(0x1c380,256));compare("particle-rng",{0x625a2,0x625a3,0x625a4,0x625a5,0x625a6,0x1bba7});}
    if(address==0x65122){native->wheelsStage();compare("wheel-bounce",{0x1bbda});}
    if(address==0x5d4a2){native->bridgeStage();
        std::vector<unsigned> v{0x1ca33};
        if(ram[0x1ca33]==5){v=scr::test::drawBridge::outputs(ram);
            // Shared renderer scratch is checked by the isolated animator
            // oracle. On held phases it retains the renderer's last values.
            for(unsigned a:{0x1bb1b,0x1bc2a,0x1bc2b,0x1bbf6,0x1bbf7,0x1bcc0,0x1bcc1})
                v.erase(std::remove(v.begin(),v.end(),a),v.end());
        }compare("drawbridge",v);}
    if(address==0x5d4ae){native->damageStage();compare("damage",{0x1bb4f,0x1bb50,0x1bb51,0x1bb54,0x1bbb6,0x1bb55,0x662aa,0x662ab,0x1bb73,0x1c9cf,0x1bb6c,0x1bca2,0x1bbe2,0x1bc56,0x1bbdd,0x625a2,0x625a3,0x625a4,0x625a5,0x625a6});
        const auto frame=fx::damageBuffer(ram),nativeFrame=fx::damageBuffer(native->ram);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<256;++x){++compared;if(fx::occupied(ram,frame,x,y)!=fx::occupied(native->ram,nativeFrame,x,y))throw std::runtime_error("Damage-strip occupancy mismatch at pixel "+std::to_string(y*256+x));}}
    if(address==0x5d4b4){native->timingStage();compare("timing",replay::timingOutputs());}
}
std::array<std::uint8_t,4096> stackRam{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,stopPc=0xa0000,entry=0x5d47e,boundary=0x5d4ba;
unsigned pc=0,instructions=0,badAddress=0;bool invalid=false,done=false,running=false;
std::map<unsigned,unsigned> readPorts,writePorts;
unsigned joy=0,cia=0xff,ddra=0xff;
std::uint8_t readByte(unsigned address){
    if(address<ram.size())return ram[address];
    if(address>=stackBase&&address<stackTop)return stackRam[address-stackBase];
    if(address==0xbfe001)return std::uint8_t(cia);
    if(address==0xbfe201)return std::uint8_t(ddra);
    if(address==0xdff00c)return std::uint8_t(joy>>8);
    if(address==0xdff00d)return std::uint8_t(joy);
    if(address>=stopPc&&address<stopPc+4){constexpr std::uint8_t stop[4]={0x4e,0x72,0x27,0};return stop[address-stopPc];}
    // DMACONR: headless renderer sees a completed blit; no bus wait needed.
    if(address==0xdff002||address==0xdff003)return 0;
    badAddress=address;invalid=true;return 0;
}
void writeByte(unsigned address,std::uint8_t value){
    if(address<ram.size()){ram[address]=value;return;}
    if(address>=stackBase&&address<stackTop){stackRam[address-stackBase]=value;return;}
    if(address==0xbfe201){ddra=value;return;}
    if(address>=0xdff000&&address<0xdff200){++writePorts[address];return;}
    badAddress=address;invalid=true;
}
void hook(unsigned address){
    pc=address;
    if(invalid){m68k_end_timeslice();return;}
    try{
        if(fullLoop&&address==0x5dac0){originalWaitVblanks+=ram[0x616d8];ram[0x616d8]=0;}
        // These CPU delay loops are presentation waits. This headless schedule
        // advances their pending countdown; it does not run an Amiga interrupt.
        if(fullLoop&&outerRunning&&restarting&&(address==0x5ba42||address==0x64470)){
            originalWaitVblanks+=ram[0x616d8];
            ram[0x616d8]=0;ram[0x616da]=0;
        }
        if(fullLoop&&address==entry){
            if(firstEntry)firstEntry=false;
            else {
                if(m68k_get_reg(nullptr,M68K_REG_SP)!=stackTop)throw std::runtime_error("Original loop did not restore its stack");
                m68k_get_context(savedCpu.data());done=true;
                m68k_set_reg(M68K_REG_PC,stopPc);m68k_end_timeslice();return;
            }
        }
        if(!outerRunning)stage(address);
        if(address==boundary){
            if(fullLoop){native->outerStage();outerRunning=true;}
            else{done=true;m68k_set_reg(M68K_REG_PC,stopPc);m68k_end_timeslice();return;}
        }
        if(fullLoop&&outerRunning){
            if(address==0x5d402){restarting=true;++originalRestarts;observedAction=1;}
            // Wait-for-fire and the fade into the menu are explicit terminal
            // platform boundaries. The race transitions are complete here.
            if(address==0x6488a||address==0x5d674){observedAction=address==0x6488a?2:3;done=true;m68k_set_reg(M68K_REG_PC,stopPc);m68k_end_timeslice();return;}
        }
    }catch(const std::exception& e){failure=e.what();invalid=true;m68k_end_timeslice();return;}
    if(address<0xe700||address>=0x724dc||++instructions>3000000){invalid=true;m68k_end_timeslice();}
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned int x){if(x>=0xdff000)++readPorts[x];return readByte(x);}
unsigned int m68k_read_memory_16(unsigned int x){return (unsigned(readByte(x))<<8)|readByte(x+1);}
unsigned int m68k_read_memory_32(unsigned int x){return (m68k_read_memory_16(x)<<16)|m68k_read_memory_16(x+2);}
void m68k_write_memory_8(unsigned int x,unsigned int v){writeByte(x,std::uint8_t(v));}
void m68k_write_memory_16(unsigned int x,unsigned int v){
    if(((pc>=0xf362&&pc<0xf3ee)||(fullLoop&&pc>=0xf3f0&&pc<0xf402))&&x>=0xdff000&&x<0xdff200)originalAudio.push_back({x,std::uint16_t(v)});
    writeByte(x,std::uint8_t(v>>8));writeByte(x+1,std::uint8_t(v));}
void m68k_write_memory_32(unsigned int x,unsigned int v){m68k_write_memory_16(x,v>>16);m68k_write_memory_16(x+2,v);}
}
int main(int argc,char** argv)try{
    if(argc!=4&&argc!=5)throw std::runtime_error("Usage: driving_replay_oracle chipram inputs.csv trace.csv [--full-loop]");
    fullLoop=argc==5&&std::string(argv[4])=="--full-loop";
    if(argc==5&&!fullLoop)throw std::runtime_error("Unknown replay option");
    ram=*replay::loadCapture(argv[1]);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);m68k_pulse_reset();
    native=std::make_unique<replay::NativeReplay>(ram);
    savedCpu.resize(m68k_context_size());
    const auto masks=replay::loadInputs(argv[2]);
    if(!fullLoop)for(const auto& input:masks)if(input.quit)throw std::runtime_error("Quit controls require --full-loop");
    std::ofstream trace(argv[3]);if(!trace)throw std::runtime_error("Cannot create trace");
    replay::traceHeader(trace);
    const unsigned ticks=unsigned(masks.size());
    for(unsigned tick=0;tick<ticks;++tick){
        currentTick=tick;native->begin(masks[tick].controls);auto hardware=replay::hardware(masks[tick].controls,std::uint8_t(ddra));joy=hardware.joy1dat;cia=hardware.ciaPortA;
        std::fill_n(ram.begin()+0xead6,128,0);
        ram[0xead6+0x45]=native->ram[0xead6+0x45]=masks[tick].quit?0xb3:0;
        if(!fullLoop||!tick){
            m68k_pulse_reset();
            for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
            m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop);m68k_set_reg(M68K_REG_PC,entry);
        }else m68k_set_context(savedCpu.data());
        outerRunning=restarting=false;firstEntry=true;observedAction=0;
        instructions=0;badAddress=0;invalid=done=false;originalAudio.clear();running=true;m68k_execute(80000000);running=false;
        if(!invalid&&done)try{
            if(fullLoop){
                compare("outer",replay::transitionOutputs());
                if(originalRestarts!=native->restarts)failure="Restart count mismatch";
                if(observedAction!=unsigned(native->outerAction))failure="Outer transition action mismatch";
                if(restarting){
                    // The two warmup render passes reuse sampler/camera scratch.
                    // Compare the complete reset vehicle block and persistent
                    // suspension, particles, RNG, timing and restart outputs.
                    // The initializer oracle separately checks its checkpoints.
                    std::set<unsigned> persistent;
                    for(auto r:std::array<std::pair<unsigned,unsigned>,6>{{{0x1bccc,0x92},{0x1bc94,40},{0x1c380,256},{0x625a2,5},{0x1bc60,2},{0x1c230,72}}})
                        for(auto a:range(r.first,r.second))persistent.insert(a);
                    for(auto a:projections.at("track"))persistent.insert(a);
                    for(auto a:replay::timingOutputs())persistent.insert(a);
                    for(unsigned n=0;n<4;++n)for(auto a:range(a::channels+4*n,2))persistent.insert(a);
                    for(auto address:range(a::guard,2))persistent.insert(address);
                    for(auto a:{0x1bbdf,0x1bb72,0x5d724,0x1bb1c,0x1bb9b,0x1bb9c,0x1bbe9,0x1bbea,0x1bbac})persistent.insert(a);
                    compare("restart",{persistent.begin(),persistent.end()});
                }
            }
            if(originalAudio.size()!=native->audioWrites.size())failure="Ordered sample audio write count mismatch";
            else for(unsigned i=0;i<originalAudio.size();++i)if(originalAudio[i].address!=native->audioWrites[i].address||originalAudio[i].value!=native->audioWrites[i].value){failure="Ordered sample audio register mismatch";break;}
            for(const auto& name:{"controls","physics","engine","track","particles","particle-rng","wheel-bounce","drawbridge","damage","timing"})
                if(stages[name]!=tick+1)failure=std::string("Missing or repeated comparison stage: ")+name;
        }catch(const std::exception& e){failure=e.what();}
        if(invalid||!done||!failure.empty()){
            const auto prefix=std::string(argv[3])+".failure";
            for(const auto& item:std::array<std::pair<const char*,const scr_test::ChipRam*>,2>{{{".original.bin",&ram},{".native.bin",&native->ram}}}){
                std::ofstream dump(prefix+item.first,std::ios::binary);dump.write(reinterpret_cast<const char*>(item.second->data()),item.second->size());}
            std::ofstream detail(prefix+".txt");detail<<"tick="<<tick<<" controls="<<masks[tick].controls<<" pc=0x"<<std::hex<<pc<<" address=0x"<<badAddress<<std::dec<<" instructions="<<instructions<<'\n'<<failure<<'\n';
            std::cerr<<"tick="<<tick<<" pc="<<std::hex<<pc<<" memory="<<badAddress<<std::dec<<" instructions="<<instructions<<'\n';throw std::runtime_error(failure.empty()?"Original loop failed":failure);}
        totalInstructions+=instructions;
        comparedAudioWrites+=originalAudio.size();replay::traceRow(trace,ram,tick,masks[tick].controls,instructions,observedAction,originalRestarts,masks[tick].quit);
        if(fullLoop&&(native->outerAction==scr::RaceTransitionAction::raceEnd||native->outerAction==scr::RaceTransitionAction::quit)&&tick+1<ticks)throw std::runtime_error("Inputs continue after terminal race transition");
    }
    std::cout<<"{\"ticks\":"<<ticks<<",\"mismatches\":0,\"compared_bytes\":"<<compared<<",\"original_instructions\":"<<totalInstructions
        <<",\"native_state_reseeded\":false,\"original_code_patched_by_harness\":false,\"registers_reset_between_ticks\":"<<(fullLoop?"false":"true")<<",\"ordered_sample_audio_writes_compared\":true,\"audio_register_writes\":"<<comparedAudioWrites
        <<",\"whole_ram_compared\":false,\"interrupts_emulated\":false,\"damage_occupancy_compared\":true,\"per_tick_stages_complete\":true,\"full_loop\":"<<(fullLoop?"true":"false")<<",\"restarts\":"<<originalRestarts<<",\"frame_wait_vblanks\":"<<originalWaitVblanks<<",\"interval\":{\"entry\":\"0x5d47e\",\"boundary_before\":\""<<(fullLoop?"next-0x5d47e-or-exit":"0x5d4ba")<<"\"},\"stages\":{";
    bool first=true;for(const auto& s:stages){if(!first)std::cout<<',';first=false;std::cout<<'"'<<s.first<<"\":"<<s.second;}std::cout<<"},\"projection_byte_addresses\":{";
    first=true;for(const auto& p:projections){if(!first)std::cout<<',';first=false;std::cout<<'"'<<p.first<<"\":[";for(unsigned i=0;i<p.second.size();++i){if(i)std::cout<<',';std::cout<<p.second[i];}std::cout<<']';}std::cout<<"}}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
