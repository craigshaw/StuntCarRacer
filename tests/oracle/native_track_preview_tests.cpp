#include "presentation/native_track_preview.h"
#include "assets/native_startup.h"
#include "race/native_race_initialization.h"
#include "track/track_initialization.h"
#include "render/planar_image.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using Memory=scr::runtime::NativeMemory;
Memory original{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned pc{},instructions{};bool failed{},returned{};std::uint64_t totalInstructions{},compared{};
unsigned requestedAngle{};
bool inputMode{},inputInjected{},inputWaiting{};unsigned inputMask{};
bool previewWaitMode{};unsigned previewPolls{},bridgeCalls{},previewRenders{};
unsigned word(unsigned a){return (unsigned(original[a])<<8)|original[a+1];}
unsigned longword(unsigned a){return (word(a)<<16)|word(a+2);}
void putLong(Memory& m,unsigned a,unsigned v){for(unsigned n=0;n<4;++n)m[a+n]=std::uint8_t(v>>(24-8*n));}
void hook(unsigned address){
    pc=address;
    if(address==0x5a794)++bridgeCalls;
    if(address==0x604b4)++previewRenders;
    if(inputMode){
        if(address==0x5d988){
            if(inputInjected){inputWaiting=returned=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;}
            // The already verified controller decoder's normalized digital
            // mask is this routine's input boundary. Execute its release,
            // priority, angle and carry logic using untouched instructions.
            inputInjected=true;original[0x1bb47]=std::uint8_t(inputMask);
            m68k_set_reg(M68K_REG_D0,inputMask);m68k_set_reg(M68K_REG_PC,0x5d98e);return;
        }
        if(address==sentinel){returned=true;m68k_end_timeslice();return;}
        if(address<0x5d980||address>=0x5d9da||++instructions>1000){failed=true;m68k_end_timeslice();}
        return;
    }
    if(previewWaitMode&&address==0x5d988){
        if(++previewPolls>1000){returned=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;}
        // Supply the controller decoder's neutral result, then execute the
        // untouched preview wait loop. It has no periodic redraw or animation.
        original[0x1bb47]=0;m68k_set_reg(M68K_REG_D0,0);m68k_set_reg(M68K_REG_PC,0x5d98e);return;
    }
    if(address==0x64340&&(m68k_get_reg(nullptr,M68K_REG_D1)&255)==0x2c&&original[0x64383]=='B'){
        m68k_set_reg(M68K_REG_D1,0x43);
        original[0x5983e]=12;original[0x5983f]=24;original[0x1bb6d]=6;
        original[0x59843]=original[0x59844]=0;
    }
    // Model only display interrupt countdowns; every projection, queue and
    // raster instruction remains the original unmodified payload code.
    original[0x616d8]=original[0x616da]=0;
    if(address==0x5d2ec)original[0x1bb57]=std::uint8_t(requestedAngle);
    if((address==0x5d2fc&&!previewWaitMode)||address==sentinel){returned=true;m68k_set_reg(M68K_REG_PC,sentinel);m68k_end_timeslice();return;}
    if(address<0xe700||address>=0x7abda||++instructions>20000000){failed=true;m68k_end_timeslice();}
}
std::uint8_t read(unsigned a){
    if(a<original.size())return original[a];
    if(a>=stackBase&&a<stackTop)return stack[a-stackBase];
    if(a>=sentinel&&a<sentinel+4)return a&1?0x71:0x4e;
    if(a>=0xbfd000&&a<=0xdff200)return 0;
    failed=true;return 0;
}
void write(unsigned a,unsigned v){
    if(a<original.size())original[a]=std::uint8_t(v);
    else if(a>=stackBase&&a<stackTop)stack[a-stackBase]=std::uint8_t(v);
    else if(!(a>=0xbfd000&&a<=0xdff200))failed=true;
}
void run(unsigned entry=0x5d20c){
    failed=returned=false;instructions=0;m68k_pulse_reset();
    for(unsigned n=0;n<15;++n)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+n),0);
    for(unsigned n=0;n<4;++n)stack[4092+n]=std::uint8_t(sentinel>>(24-8*n));
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_PC,entry);
    m68k_execute(1000000000);
    if(failed||!returned)throw std::runtime_error("Original preview failed pc="+std::to_string(pc)+" instructions="+std::to_string(instructions));
    totalInstructions+=instructions;
}
void ppm(const std::filesystem::path& path,const std::vector<std::uint32_t>& pixels){
    std::ofstream out(path,std::ios::binary);out<<"P6\n320 200\n255\n";
    for(auto p:pixels){out.put(char(p>>16));out.put(char(p>>8));out.put(char(p));}
}
}
extern "C" {
unsigned m68k_read_memory_8(unsigned a){return read(a);}
unsigned m68k_read_memory_16(unsigned a){return (unsigned(read(a))<<8)|read(a+1);}
unsigned m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){write(a,v);}
void m68k_write_memory_16(unsigned a,unsigned v){write(a,v>>8);write(a+1,v);}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv){try{
    if(argc<2||argc>3)throw std::invalid_argument("Usage: native_track_preview_tests local.scr-assets [image-directory]");
    auto source=scr::runtime::startNativeGame(argv[1]);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    unsigned cases=0,mismatchCases=0,rotationCases=0;std::uint64_t mismatches=0;
    for(unsigned track=0;track<8;++track)for(unsigned angle=0;angle<4;++angle){
        requestedAngle=angle;
        original=*source.memory;original[0x1ca33]=std::uint8_t(track);original[0x1bb57]=std::uint8_t(angle);
        // Original displayMenuScreen leaves these selected before Practice.
        putLong(original,0x6a588,longword(0x6a584)+32000);original[0x1bbd0]=0x80;
        auto native=std::make_unique<Memory>(original);
        scr::runtime::initializeNativePlayerConfiguration(*native);scr::initializeNativeTrack(*native,track);
        scr::runtime::NativeTrackPreview preview;preview.load(*native,std::uint8_t(angle));
        run();
        const auto planar=scr::decodePlanarImage(original.data()+longword(0x6a588),32000,320,200,4);
        std::vector<std::uint32_t> expected(64000);
        unsigned mismatch=0,first=64000;
        for(unsigned n=0;n<64000;++n){expected[n]=scr::originalPaletteRgb(std::uint16_t(word(0x2b974+2*planar.pixels[n])));++compared;
            if(expected[n]!=preview.pixels()[n]||planar.pixels[n]!=preview.indices()[n]){++mismatch;first=std::min(first,n);}}
        if(mismatch){++mismatchCases;mismatches+=mismatch;std::cerr<<"track="<<track<<" angle="<<angle<<" mismatch="<<mismatch<<" first="<<first%320<<","<<first/320<<"\n";}
        if(argc==3){std::filesystem::create_directories(argv[2]);const auto name=std::to_string(track)+"-"+std::to_string(angle);ppm(std::filesystem::path(argv[2])/(name+"-native.ppm"),preview.pixels());ppm(std::filesystem::path(argv[2])/(name+"-original.ppm"),expected);}
        ++cases;
        if(angle==0)for(unsigned turn=0;turn<8;++turn){
            // The original retains renderer scratch between quarter turns.
            // Verify the native owned-copy approach preserves the same result.
            preview.input(0);
            if(preview.input(std::uint8_t(turn<4?4:8))!=scr::runtime::NativeTrackPreviewAction::redraw)
                throw std::runtime_error("Released direction did not rotate the overview");
            original[0x1bb57]=preview.angle();run(0x604b4);
            const auto rotated=scr::decodePlanarImage(original.data()+longword(0x6a588),32000,320,200,4);
            for(unsigned n=0;n<64000;++n){++compared;if(rotated.pixels[n]!=preview.indices()[n])
                throw std::runtime_error("Repeated native preview rotation differs from original");}
            ++rotationCases;
        }
    }
    auto inputMemory=std::make_unique<Memory>(*source.memory);
    scr::runtime::initializeNativePlayerConfiguration(*inputMemory);scr::initializeNativeTrack(*inputMemory,0);
    unsigned inputCases=0;
    for(unsigned initial:{0u,255u})for(unsigned held=0;held<32;++held){
        scr::runtime::NativeTrackPreview preview;preview.load(*inputMemory,std::uint8_t(initial));
        original=*inputMemory;original[0x1bb57]=std::uint8_t(initial);bool waiting=false;
        for(unsigned mask:{held,held,0u,held,held,0u,16u}){
            const auto actual=preview.input(std::uint8_t(mask));
            inputMode=true;inputInjected=inputWaiting=false;inputMask=mask;run(waiting?0x5d988:0x5d980);inputMode=false;
            const auto expected=inputWaiting?scr::runtime::NativeTrackPreviewAction::none:
                (m68k_get_reg(nullptr,M68K_REG_SR)&1)?scr::runtime::NativeTrackPreviewAction::redraw:scr::runtime::NativeTrackPreviewAction::startRace;
            if(actual!=expected||preview.angle()!=original[0x1bb57])throw std::runtime_error("Native preview input differs from original");
            ++inputCases;waiting=inputWaiting;
            if(actual==scr::runtime::NativeTrackPreviewAction::startRace)preview.load(*inputMemory,preview.angle());
        }
    }
    auto bridge=std::make_unique<Memory>(*source.memory);(*bridge)[0x1ca33]=5;
    scr::runtime::initializeNativePlayerConfiguration(*bridge);scr::initializeNativeTrack(*bridge,5);
    // Preview initialization always starts a fresh player configuration. An
    // already animated caller must not advance the overview's phase twice.
    (*bridge)[0x1bbb0]=0xd7;(*bridge)[0x1bb71]=0x80;
    scr::runtime::NativeTrackPreview preview;preview.load(*bridge);
    const auto stationary=preview.indices();
    original=*bridge;putLong(original,0x6a588,longword(0x6a584)+32000);original[0x1bbd0]=0x80;
    requestedAngle=0;bridgeCalls=previewRenders=previewPolls=0;previewWaitMode=true;run();previewWaitMode=false;
    if(bridgeCalls!=1||previewRenders!=1||original[0x1bbb0]!=1)
        throw std::runtime_error("Original bridge overview unexpectedly animates while waiting");
    const auto waiting=scr::decodePlanarImage(original.data()+longword(0x6a588),32000,320,200,4);
    for(unsigned n=0;n<64000;++n){++compared;if(waiting.pixels[n]!=stationary[n])
        throw std::runtime_error("Bridge overview double-advanced an initialized caller");}
    for(unsigned n=0;n<1000;++n)if(preview.input(0)!=scr::runtime::NativeTrackPreviewAction::none||preview.indices()!=stationary)
        throw std::runtime_error("Native bridge overview changed while waiting");
    std::cout<<"{\"cases\":"<<cases<<",\"rotation_images\":"<<rotationCases<<",\"compared_pixels\":"<<compared<<",\"mismatch_cases\":"<<mismatchCases<<",\"mismatches\":"<<mismatches<<",\"input_cases\":"<<inputCases<<",\"drawbridge_views\":4,\"drawbridge_rotation_images\":8,\"drawbridge_wait_polls\":1000,\"drawbridge_wait_animation_calls\":"<<bridgeCalls<<",\"drawbridge_wait_render_calls\":"<<previewRenders<<",\"original_instructions\":"<<totalInstructions<<"}\n";
    if(mismatches)throw std::runtime_error("Native track preview differs from original");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
