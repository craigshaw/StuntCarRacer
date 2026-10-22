// Complete original selector and nested helpers are interpreted independently.
// Native production code has neither a CPU state nor a Musashi dependency.
#include "track_sampling_fixture.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr unsigned entry=0x5c1d0,loadAddress=0xe700,payloadSize=408540;
constexpr unsigned stack=0x7f000,sentinel=0x7e000;
constexpr std::uint32_t initialSeed=0x53435237,initialCpuSeed=0x36383035;
constexpr std::array<std::array<unsigned,2>,9> spans{{
    {0x5c0aa,0x5c138},{0x5c1d0,0x5c3da},{0x5c484,0x5c51a},
    {0x5c51a,0x5c538},{0x5c538,0x5c554},{0x5fe56,0x5ff94},
    {0x5c554,0x5c5f2},{0x5c5f2,0x5c65a},{0x5c808,0x5c890}}};
scr_test::ChipRam ram{},nativeRam{};
std::array<bool,0x80000> readable{},writable{};
std::array<scr::WheelTrackSample,3> originalEvents{};
std::uint32_t randomState=initialSeed,cpuRandomState=initialCpuSeed;
std::uint64_t totalCases=0,totalInstructions=0,totalWrites=0;
unsigned currentPc=entry,instructions=0,eventCount=0,completedContacts=0,writes=0,stackBytes=0;
unsigned minimumInstructions=10000,maximumInstructions=0,maximumStackBytes=0;
bool executing=false,invalidAccess=false,returned=false;
std::string accessFailure;
std::map<std::string,std::uint64_t> coverage;

std::uint32_t random32(std::uint32_t& s) { s^=s<<13;s^=s>>17;s^=s<<5;return s; }
std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error(std::string("Cannot read ")+path);
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
bool contains(unsigned a,unsigned n,unsigned begin,unsigned end) { return a>=begin && a<end && n<=end-a; }
void failAccess(const char* why,unsigned a,unsigned n) {
    invalidAccess=true;if(!accessFailure.empty())return;
    std::ostringstream s;s<<why<<" address=0x"<<std::hex<<a<<" bytes="<<std::dec<<n<<" pc=0x"<<std::hex<<currentPc;
    accessFailure=s.str();
}
void allow(unsigned a,unsigned n) { for(unsigned j=0;j<n;++j)readable[a+j]=true; }
void initializePermissions() {
    for(const auto& span:spans)allow(span[0],span[1]-span[0]+4); // instruction prefetch
    allow(stack-8,12);allow(sentinel,8);
    for(unsigned a:scr_test::trackSamplingOutputBytes()) {readable[a]=writable[a]=true;}
    for(auto range:std::array<std::array<unsigned,2>,12>{{{0xff,1},{0x1bb10,2},{0x1bb1c,1},{0x1bc5e,2},
        {0x1bd02,12},{0x1bd5c,1},{0x1bce4,1},{0x1c4c0,100},{0x1c524,100},{0x1c5ec,100},
        {0x1c650,400},{0x1ca1a,1}}})allow(range[0],range[1]);
    allow(0x1ef82,0x10100);
}
bool permittedRead(unsigned a,unsigned n) {
    if(!contains(a,n,0,unsigned(ram.size())) || (n>1 && (a&1)))return false;
    if(!executing)return true;
    for(unsigned j=0;j<n;++j)if(!readable[a+j])return false;
    return true;
}
bool permittedWrite(unsigned a,unsigned n) {
    if(!executing || !contains(a,n,0,unsigned(ram.size())) || (n>1 && (a&1))) {failAccess("Invalid write",a,n);return false;}
    if(contains(a,n,stack-8,stack)) {
        // Only JSR is permitted to write the call stack; two nested calls max.
        if(n!=2 && n!=4) {failAccess("Invalid stack width",a,n);return false;}
        if(scr_test::readWord(ram,currentPc)!=0x4eb9) {failAccess("Stack write outside JSR",a,n);return false;}
        stackBytes+=n;maximumStackBytes=std::max(maximumStackBytes,stack-a);return true;
    }
    for(unsigned j=0;j<n;++j)if(!writable[a+j]) {failAccess("Write outside selector outputs",a,n);return false;}
    writes+=n;return true;
}
void instructionHook(unsigned pc) {
    currentPc=pc;if(invalidAccess){m68k_end_timeslice();return;}
    if(pc==sentinel){returned=true;m68k_end_timeslice();return;}
    bool allowed=false;for(const auto& span:spans)allowed|=contains(pc,2,span[0],span[1]);
    if(!allowed || (pc&1) || ++instructions>1800) {failAccess("Instruction outside recovered routines/budget",pc,2);m68k_end_timeslice();return;}
    switch(pc) {
    case 0x5c0b6:++coverage["word_geometry_points"];break;
    case 0x5c0f6:++coverage["packed_geometry_points"];break;
    case 0x5c204:++coverage["reload_player_segment"];break;
    case 0x5c24e:++coverage["outside_positive"];break;
    case 0x5c256:++coverage["outside_negative"];break;
    case 0x5c280:++coverage["absolute_ff_clamp"];break;
    case 0x5c33c:++coverage["forward_control_order"];break;
    case 0x5c37e:++coverage["reverse_control_order"];break;
    case 0x5c496:++coverage["retreat"];break;
    case 0x5c4b0:++coverage["advance"];break;
    case 0x5c4f4:++coverage["flip_fractions"];break;
    case 0x5c4fe:++coverage["zero_blend_becomes_ff"];break;
    case 0x5c510:++coverage["zero_lateral_becomes_ff"];break;
    case 0x5c52c:++coverage["advance_wrap"];break;
    case 0x5c544:++coverage["retreat_wrap"];break;
    case 0x5c5f2: {
        if(eventCount>=3) {failAccess("Too many wheel contacts",pc,2);return;}
        auto& e=originalEvents[eventCount++];
        e.wheelIndex=std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D1)&255)/2;
        e.segmentIndex=ram[0x1bb85];e.lateral=ram[0x1bc4d];e.blend=ram[0x1bc41];
        e.reverseDirection=ram[0x1bc32];e.outOfBoundsFlag=ram[0x1bb65];e.outOfBoundsPosition=scr_test::readWord(ram,0x1bc22);
        for(unsigned i=0;i<4;++i)e.controls[i]=scr_test::readWord(ram,0x1bc02+2*i);
        break;
    }
    case 0x5c5f8:
        if(!eventCount) {failAccess("Interpolation result without event",pc,2);return;}
        originalEvents[eventCount-1].rawHeight=scr_test::readLong(ram,0x1bb18);break;
    case 0x5c3d2:
        if(!eventCount || completedContacts+1!=eventCount){failAccess("Unordered contact return",pc,2);return;}
        originalEvents[eventCount-1].sampledHeight=scr_test::readLong(ram,0x1bb18);
        originalEvents[eventCount-1].contactHeight=scr_test::readLong(ram,0x1bca4+4*originalEvents[eventCount-1].wheelIndex);
        ++completedContacts;break;
    default:break;
    }
}
void runOriginal() {
    scr_test::writeLong(ram,0,stack);scr_test::writeLong(ram,4,entry);scr_test::writeLong(ram,stack,sentinel);
    scr_test::writeWord(ram,sentinel,0x4e72);scr_test::writeWord(ram,sentinel+2,0x2700);
    executing=false;m68k_pulse_reset();
    for(unsigned r=0;r<15;++r) {
        auto value=random32(cpuRandomState);
        // Original byte-sized table indices retain the upper byte of their
        // word. All remaining register bits are deliberately randomized.
        if(r==1 || r==2)value&=0xffff00ff;
        m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),value);
    }
    m68k_set_reg(M68K_REG_SR,0x2700|(random32(cpuRandomState)&31));
    m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,entry);
    instructions=eventCount=completedContacts=writes=stackBytes=0;invalidAccess=returned=false;accessFailure.clear();
    currentPc=entry;executing=true;m68k_execute(80000);executing=false;
    if(invalidAccess || !returned || eventCount!=3 || completedContacts!=3
       || m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4 || !stackBytes || !writes) {
        std::ostringstream s;s<<"Original selector contract failed at case "<<totalCases<<": "<<accessFailure
            <<" returned="<<returned<<" contacts="<<completedContacts<<" instructions="<<instructions;
        throw std::runtime_error(s.str());
    }
    totalInstructions+=instructions;totalWrites+=writes;
    minimumInstructions=std::min(minimumInstructions,instructions);maximumInstructions=std::max(maximumInstructions,instructions);
}
bool sameEvent(const scr::WheelTrackSample& a,const scr::WheelTrackSample& b) {
    return a.wheelIndex==b.wheelIndex && a.segmentIndex==b.segmentIndex && a.lateral==b.lateral && a.blend==b.blend
        && a.reverseDirection==b.reverseDirection && a.outOfBoundsFlag==b.outOfBoundsFlag && a.outOfBoundsPosition==b.outOfBoundsPosition
        && a.controls==b.controls && a.rawHeight==b.rawHeight && a.contactHeight==b.contactHeight && a.sampledHeight==b.sampledHeight;
}
scr::TrackSamplingState check(const scr::TrackGeometryData& data,const scr::TrackSamplingInputs& input) {
    scr_test::encodeTrackSamplingInputs(ram,input);
    auto state=scr_test::decodeTrackSamplingState(ram);
    // Compare the entire mutable neighborhood, including all unwritten holes.
    std::copy(ram.begin()+0x1bb00,ram.begin()+0x1bd00,nativeRam.begin()+0x1bb00);
    const auto events=scr::sampleTrackWheelHeights(data,input,state);
    scr_test::encodeTrackSamplingState(nativeRam,state);
    runOriginal();
    for(unsigned a=0x1bb00;a<0x1bd00;++a)if(ram[a]!=nativeRam[a]) {
        std::ostringstream s;s<<"Selector state mismatch case "<<totalCases<<" address=0x"<<std::hex<<a
            <<" original="<<unsigned(ram[a])<<" native="<<unsigned(nativeRam[a]);throw std::runtime_error(s.str());
    }
    for(unsigned n=0;n<3;++n)if(!sameEvent(events[n],originalEvents[n])) {
        std::ostringstream s;s<<"Selector event mismatch case "<<totalCases<<" wheel "<<n
            <<" raw original="<<originalEvents[n].rawHeight<<" native="<<events[n].rawHeight;
        throw std::runtime_error(s.str());
    }
    ++totalCases;return state;
}
void randomizeOutputs() {
    // Every field and adjacent hole starts with varying, nonzero old bytes.
    for(unsigned a=0x1bb00;a<0x1bd00;++a)ram[a]=std::uint8_t(random32(randomState));
}
scr::TrackSamplingInputs randomInputs(unsigned count) {
    scr::TrackSamplingInputs in;
    auto point=[] {return scr::WheelPoint{scr_test::signedWord(std::uint16_t(random32(randomState))),
                                        scr_test::signedWord(std::uint16_t(random32(randomState)))};};
    in.wheels={point(),point(),point()};in.lateralOrigin=std::uint16_t(random32(randomState));in.forwardOrigin=std::uint16_t(random32(randomState));
    in.playerSegment=std::uint8_t(random32(randomState)%count);in.clampByteAtFF=std::uint8_t(random32(randomState));
    in.speedHighByte=std::uint8_t(random32(randomState));in.pitchHighByte=std::uint8_t(random32(randomState));return in;
}
void geometryWord(scr::TrackGeometryData& d,unsigned index,std::uint16_t v) {d.geometry[index]=std::uint8_t(v>>8);d.geometry[index+1]=std::uint8_t(v);}
std::uint16_t encodedOffset(unsigned o) {const auto v=std::uint16_t(o+0xb100);return std::uint16_t((v<<8)|(v>>8));}
scr::TrackGeometryData syntheticData() {
    scr::TrackGeometryData d;d.segmentCount=16;d.geometry.resize(0x10100);
    for(auto& b:d.geometry)b=std::uint8_t(random32(randomState));
    constexpr std::array<unsigned,16> limits{4,6,8,10,12,16,30,64,126,128,130,254,0,1,2,255};
    for(unsigned type=0;type<16;++type) {
        const unsigned base=0x1000+type*256;geometryWord(d,type*2,encodedOffset(base));
        d.geometry[base]=0xfa; // descriptor indices wrap between orientation and steering fields
        d.geometry[base+0xfa]=std::uint8_t(limits[type]);
        d.geometry[base+0xfb]=std::uint8_t(type);
        d.geometry[base+0xfc]=std::uint8_t(255-type*11);
        d.geometry[base+0xfd]=std::uint8_t(255-type*7);
        d.geometry[base]=0xfa; // steeringBase at wrapped index0 deliberately aliases descriptor index
    }
    for(unsigned i=0;i<128;++i)geometryWord(d,32+i*2,encodedOffset(0x3000+i*256));
    for(unsigned i=0;i<100;++i) {
        d.primaryGeometry[i]=std::uint8_t((i*7%128)|((i&1)?0x80:0));
        d.alternateGeometry[i]=std::uint8_t((i*13%128)|((i&2)?0x80:0));
        d.properties[i]=std::uint8_t((i&15)|((i&2)?0x10:0)|((i&3)<<6));
        d.primaryBase[i]=std::uint16_t(random32(randomState));d.alternateBase[i]=std::uint16_t(random32(randomState));
    }
    return d;
}
void printHeights(const std::array<std::uint32_t,3>& h) {std::cout<<'['<<h[0]<<','<<h[1]<<','<<h[2]<<']';}
std::uint64_t loadedSegmentTests(scr::TrackGeometryData data) {
    const auto before=totalCases;
    constexpr std::array<unsigned,13> forwards{0,1,0x7f,0xff,0x100,0x1ff,0x7fff,0x8000,0x8001,0xff00,0xffff,0x4000,0xc000};
    constexpr std::array<unsigned,13> laterals{0,1,0x17f,0x180,0x181,0xffff,0x8000,0x7fff,0xffcf,0xffd0,0x1af,0x1b0,0x1b1};
    for(unsigned format=0;format<2;++format) {
        for(unsigned i=0;i<data.segmentCount;++i)data.primaryGeometry[i]=std::uint8_t((data.primaryGeometry[i]&0x7f)|(format<<7));
        scr_test::encodeTrackGeometry(ram,data);
        for(unsigned segment=0;segment<data.segmentCount;++segment)for(unsigned forward:forwards)for(unsigned lateral:laterals) {
            randomizeOutputs();auto in=randomInputs(data.segmentCount);
            in.playerSegment=std::uint8_t(segment);in.forwardOrigin=std::uint16_t(forward);in.lateralOrigin=std::uint16_t(lateral);
            in.wheels={{0,0},{0,0},{0,0}};check(data,in);
        }
    }
    return totalCases-before;
}
} // namespace

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) {if(!permittedRead(a,1)){failAccess("Invalid read",a,1);return 0;}return ram[a];}
unsigned int m68k_read_memory_16(unsigned int a) {if(!permittedRead(a,2)){failAccess("Invalid read",a,2);return 0;}return scr_test::readWord(ram,a);}
unsigned int m68k_read_memory_32(unsigned int a) {if(!permittedRead(a,4)){failAccess("Invalid read",a,4);return 0;}return scr_test::readLong(ram,a);}
void m68k_write_memory_8(unsigned int a,unsigned int v) {if(permittedWrite(a,1))ram[a]=std::uint8_t(v);}
void m68k_write_memory_16(unsigned int a,unsigned int v) {if(permittedWrite(a,2))scr_test::writeWord(ram,a,std::uint16_t(v));}
void m68k_write_memory_32(unsigned int a,unsigned int v) {if(permittedWrite(a,4))scr_test::writeLong(ram,a,v);}
}

int main(int argc,char** argv) try {
    if(argc<2 || (argc-2)%2)throw std::runtime_error("Usage: track_sampling_oracle payload.bin [label chipram.bin]...");
    const auto payload=readFile(argv[1]);if(payload.size()!=payloadSize)throw std::runtime_error("Expected original 408540-byte payload");
    std::copy(payload.begin(),payload.end(),ram.begin()+loadAddress);initializePermissions();
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(instructionHook);
    std::uint64_t captureCases=0,skippedCaptures=0,loadedCases=0;
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":"<<initialSeed<<",\"cpu_register_seed\":"<<initialCpuSeed<<",\"captures\":[";
    for(int argument=2;argument<argc;argument+=2) {
        const std::string label=argv[argument];
        if(label.empty() || label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid capture label");
        const auto bytes=readFile(argv[argument+1]);if(bytes.size()!=ram.size())throw std::runtime_error("Capture is not 512KiB chip RAM");
        for(const auto& span:spans)if(!std::equal(payload.begin()+span[0]-loadAddress,payload.begin()+span[1]-loadAddress,bytes.begin()+span[0]))
            throw std::runtime_error("Capture code differs from original disk bytes");
        std::copy(bytes.begin(),bytes.end(),ram.begin());const auto data=scr_test::decodeTrackGeometry(ram);
        const auto in=scr_test::decodeTrackSamplingInputs(ram);const auto old=scr_test::decodeTrackSamplingState(ram);
        if(argument>2)std::cout<<',';
        std::cout<<"{\"label\":\""<<label<<"\",\"segment_count\":"<<unsigned(data.segmentCount)<<",\"player_segment\":"<<unsigned(in.playerSegment);
        if(!data.segmentCount || data.segmentCount>100 || in.playerSegment>=data.segmentCount) {
            ++skippedCaptures;std::cout<<",\"replayed\":false,\"reason\":\"No valid loaded track/segment at this snapshot\"}";continue;
        }
        const auto result=check(data,in);++captureCases;
        std::cout<<",\"replayed\":true,\"stored_heights\":";printHeights(old.heights);
        std::cout<<",\"replayed_heights\":";printHeights(result.heights);
        std::cout<<",\"stored_heights_agree\":"<<(old.heights==result.heights?"true":"false")<<'}';
        loadedCases+=loadedSegmentTests(data);
    }
    ram.fill(0);std::copy(payload.begin(),payload.end(),ram.begin()+loadAddress);
    auto data=syntheticData();const auto syntheticBoundaryCases=loadedSegmentTests(data);
    scr_test::encodeTrackGeometry(ram,data);
    const auto beforeSweep=totalCases;
    for(unsigned field=0;field<2;++field)for(unsigned value=0;value<65536;++value) {
        randomizeOutputs();auto in=randomInputs(data.segmentCount);
        // Every origin word with fixed nontrivial corners and varying segment.
        in.wheels={{-317,193},{1023,-2048},{-32768,32767}};
        if(field==0)in.lateralOrigin=std::uint16_t(value);else in.forwardOrigin=std::uint16_t(value);
        check(data,in);
    }
    const auto originSweepCases=totalCases-beforeSweep;
    const auto beforeRandom=totalCases;
    constexpr unsigned randomCases=100000;
    for(unsigned i=0;i<randomCases;++i) {
        if((i%257)==0) {
            // Vary independent segment direction/format flags and all raw base
            // words, plus curve bytes, retaining bounded descriptor/table data.
            for(unsigned s=0;s<data.segmentCount;++s) {
                data.primaryGeometry[s]=std::uint8_t(random32(randomState));data.alternateGeometry[s]=std::uint8_t(random32(randomState));
                data.properties[s]=std::uint8_t(random32(randomState));data.primaryBase[s]=std::uint16_t(random32(randomState));data.alternateBase[s]=std::uint16_t(random32(randomState));
            }
            for(unsigned j=0;j<512;++j)data.geometry[0x3000+(random32(randomState)%0x8000)]=std::uint8_t(random32(randomState));
            scr_test::encodeTrackGeometry(ram,data);
        }
        randomizeOutputs();check(data,randomInputs(data.segmentCount));
    }
    const auto actualRandomCases=totalCases-beforeRandom;
    unsigned rejectedInputs=0;
    for(unsigned variant=0;variant<4;++variant) {
        auto invalid=data;auto in=randomInputs(data.segmentCount);scr::TrackSamplingState s;
        if(variant==0)invalid.segmentCount=0;if(variant==1)invalid.segmentCount=101;
        if(variant==2)in.playerSegment=invalid.segmentCount;if(variant==3)invalid.geometry.clear();
        try {(void)scr::sampleTrackWheelHeights(invalid,in,s);}
        catch(const std::out_of_range&){++rejectedInputs;continue;}
        catch(const std::invalid_argument&){++rejectedInputs;continue;}
        throw std::runtime_error("Malformed native track input was not rejected");
    }
    for(const char* name:{"word_geometry_points","packed_geometry_points","reload_player_segment","outside_positive","outside_negative",
        "absolute_ff_clamp","forward_control_order","reverse_control_order","retreat","advance","flip_fractions",
        "zero_blend_becomes_ff","zero_lateral_becomes_ff","advance_wrap","retreat_wrap"})
        if(!coverage[name])throw std::runtime_error(std::string("Uncovered selector branch: ")+name);
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"skipped_captures\":"<<skippedCaptures
             <<",\"loaded_segment_boundary_cases\":"<<loadedCases<<",\"synthetic_segment_boundary_cases\":"<<syntheticBoundaryCases
             <<",\"origin_word_sweep_cases\":"<<originSweepCases<<",\"random_cases\":"<<actualRandomCases
             <<",\"rejected_invalid_inputs\":"<<rejectedInputs<<",\"total_cases\":"<<totalCases<<",\"mismatches\":0"
             <<",\"wheel_events_compared\":"<<totalCases*3<<",\"full_original_contact_executed\":true"
             <<",\"writes_confined_to_documented_state_and_stack\":true,\"reads_confined_to_documented_inputs_geometry_code_stack\":true"
             <<",\"all_mutable_region_bytes_compared_including_holes\":512,\"maximum_stack_depth_bytes\":"<<maximumStackBytes
             <<",\"minimum_instructions\":"<<minimumInstructions<<",\"maximum_instructions\":"<<maximumInstructions
             <<",\"total_instructions\":"<<totalInstructions<<",\"total_output_bytes_written\":"<<totalWrites<<",\"coverage\":{";
    bool first=true;for(const auto& item:coverage){if(!first)std::cout<<',';first=false;std::cout<<'"'<<item.first<<"\":"<<item.second;}
    std::cout<<"},\"final_input_prng_state\":"<<randomState<<",\"final_cpu_prng_state\":"<<cpuRandomState<<"}\n";return 0;
} catch(const std::exception& e) {std::cerr<<"\n"<<e.what()<<'\n';return 1;}
