// Independent original-byte execution of steering, segment configuration and
// forward transition. No enhanced-source timestep or integrity-check removal.
#include "steering_response_fixture.h"
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
namespace f=scr::test::steering;
constexpr unsigned base=0xe700,payloadSize=408540,stack=0x7f000,sentinel=0x7e000;
constexpr std::uint32_t seed=0x53544545,cpuSeed=0x36383038;
constexpr std::array<std::array<unsigned,2>,3> spans{{{f::entry,f::end},{0x5fe56,0x5ff94},{0x5c51a,0x5c538}}};
scr_test::ChipRam ram{},expectedRam{};
std::array<bool,0x80000> readable{},writable{},written{};
std::uint32_t rng=seed,cpuRng=cpuSeed;
std::uint64_t totalCases=0,totalInstructions=0,totalOutputBytes=0;
unsigned pcNow=0,instructions=0,outputBytes=0,stackBytes=0,configCalls=0,advanceCalls=0,minInstructions=1000,maxInstructions=0;
bool executing=false,invalid=false,returned=false;
std::string failure;
std::map<std::string,std::uint64_t> coverage;

std::uint32_t random32(std::uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
std::uint32_t next(){return random32(rng);}
std::vector<std::uint8_t> readFile(const char* path){std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error(std::string("Cannot read ")+path);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
bool contains(unsigned a,unsigned n,unsigned first,unsigned end){return a>=first && a<end && n<=end-a;}
void fail(const char* why,unsigned a,unsigned n){invalid=true;if(!failure.empty())return;std::ostringstream s;s<<why<<" address=0x"<<std::hex<<a<<" size="<<std::dec<<n<<" pc=0x"<<std::hex<<pcNow;failure=s.str();}
void allow(unsigned a,unsigned n){for(unsigned j=0;j<n;++j)readable[a+j]=true;}
void permissions(){
    for(auto span:spans)allow(span[0],span[1]-span[0]+4);allow(stack-4,8);allow(sentinel,8);allow(f::tableAddress,6);
    for(auto a:f::outputBytes()){readable[a]=writable[a]=true;}
    for(auto a:{0x1bd5a,0x1bd30,0x1bcf2})allow(a,2);
    for(auto a:{0x1bb1c,0x1bb0a,0x1bb7e,0x1bc33,0x1bc45,0x1ca1a})allow(a,1);
    allow(f::integrityAddress,4);
    allow(0x1c4c0,100);allow(0x1c524,100);allow(0x1c5ec,100);allow(0x1c650,400);allow(0x1ef82,0x10100);
}
bool permittedRead(unsigned a,unsigned n){
    if(!contains(a,n,0,unsigned(ram.size())) || (n>1 && (a&1)))return false;if(!executing)return true;
    for(unsigned j=0;j<n;++j)if(!readable[a+j])return false;return true;
}
bool permittedWrite(unsigned a,unsigned n){
    if(!executing || !contains(a,n,0,unsigned(ram.size())) || (n>1 && (a&1))){fail("Invalid write",a,n);return false;}
    if(contains(a,n,stack-4,stack)){
        if((n!=2 && n!=4) || f::word(ram.data(),pcNow)!=0x4eb9){fail("Write outside JSR stack",a,n);return false;}
        stackBytes+=n;return true;
    }
    for(unsigned j=0;j<n;++j)if(!writable[a+j]){fail("Write outside steering/configuration outputs",a,n);return false;}
    for(unsigned j=0;j<n;++j)written[a+j]=true;outputBytes+=n;return true;
}
void hook(unsigned pc){
    pcNow=pc;if(invalid){m68k_end_timeslice();return;}if(pc==sentinel){returned=true;m68k_end_timeslice();return;}
    bool allowed=false;for(auto span:spans)allowed|=contains(pc,2,span[0],span[1]);
    if(!allowed || (pc&1) || ++instructions>350){fail("Invalid steering PC/budget",pc,2);m68k_end_timeslice();return;}
    switch(pc){
    case 0x5fe56:++configCalls;break;case 0x5c51a:++advanceCalls;break;
    case 0x5c52c:++coverage["advance_wrap"];break;
    case 0x61046:++coverage["bank_offset"];break;case 0x61054:++coverage["inverted_bank_offset"];break;
    case 0x6107c:++coverage["saturated_steering_scale"];break;case 0x61084:++coverage["linear_steering_scale"];break;
    case 0x610a0:++coverage["advance_segment"];break;case 0x610ca:++coverage["manual_input"];break;
    case 0x610f4:++coverage["manual_bank_same_direction"];break;case 0x61104:++coverage["manual_bank_opposite_direction"];break;
    case 0x61120:++coverage["manual_unbanked"];break;case 0x61130:++coverage["add_scale_high_byte"];break;
    case 0x6114a:++coverage["automatic_bank_steering"];break;case 0x61160:++coverage["automatic_yaw_correction"];break;
    case 0x6117c:++coverage["nonzero_distance_high_byte"];break;case 0x61184:++coverage["saturate_distance_factor"];break;
    case 0x611a2:++coverage["speed_word_clamp"];break;case 0x611c0:++coverage["minimum_yaw_correction"];break;
    case 0x611cc:++coverage["negative_yaw_correction"];break;case 0x61206:++coverage["integrity_pass"];break;
    case 0x61210:++coverage[f::longword(ram.data(),f::integrityAddress)==0x9cedcd02 ? "inactive_wheel_gate" : "integrity_failure"];break;
    case 0x6121c:++coverage["steering_force"];break;case 0x61244:++coverage["negate_steering_force"];break;
    case 0x61256:++coverage["force_then_yaw_correction"];break;default:break;
    }
}
void original(){
    f::putLong(ram.data(),0,stack);f::putLong(ram.data(),4,f::entry);f::putLong(ram.data(),stack,sentinel);
    f::putWord(ram.data(),sentinel,0x4e72);f::putWord(ram.data(),sentinel+2,0x2700);
    executing=false;m68k_pulse_reset();for(unsigned r=0;r<15;++r){auto value=random32(cpuRng);if(r==1 || r==2)value&=0xffff00ff;m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),value);}
    m68k_set_reg(M68K_REG_SR,0x2700|(random32(cpuRng)&31));m68k_set_reg(M68K_REG_SP,stack);m68k_set_reg(M68K_REG_PC,f::entry);
    instructions=outputBytes=stackBytes=configCalls=advanceCalls=0;invalid=returned=false;failure.clear();for(auto a:f::outputBytes())written[a]=false;
    pcNow=f::entry;executing=true;m68k_execute(30000);executing=false;
    if(invalid || !returned || m68k_get_reg(nullptr,M68K_REG_SP)!=stack+4 || configCalls!=1+advanceCalls || advanceCalls>1 || stackBytes!=(configCalls+advanceCalls)*4)
        throw std::runtime_error("Steering oracle contract failure case="+std::to_string(totalCases)+": "+failure);
    for(auto [a,m]:f::configBytes){(void)m;if(!written[a])throw std::runtime_error("Missing configuration byte write");}
    for(auto [a,m]:f::configWords){(void)m;if(!written[a] || !written[a+1])throw std::runtime_error("Missing configuration word write");}
    for(auto a:{0x1bb85,0x1bb5d,0x1bc2a,0x1bc2b,0x1bc3c,0x1bc3d,0x1bbf6,0x1bbf7,0x1bcfe,0x1bcff})if(!written[a])throw std::runtime_error("Missing required steering write");
    if((m68k_get_reg(nullptr,M68K_REG_D1)&0xff00) || (m68k_get_reg(nullptr,M68K_REG_D2)&0xffff))throw std::runtime_error("Original steering index/zero-D2 return contract failed");
    totalInstructions+=instructions;totalOutputBytes+=outputBytes;minInstructions=std::min(minInstructions,instructions);maxInstructions=std::max(maxInstructions,instructions);
}
scr::SteeringResponseState check(const scr::TrackGeometryData& data,const scr::SteeringOffsetTable& offsets,const scr::SteeringResponseInputs& in,const scr::SteeringResponseState& initial){
    f::writeInputs(ram.data(),in);f::writeOffsets(ram.data(),offsets);f::writeState(ram.data(),initial);
    std::copy(ram.begin()+0x1bb00,ram.begin()+0x1bd00,expectedRam.begin()+0x1bb00);
    auto state=initial;scr::calculateSteeringResponse(data,offsets,in,state);f::writeState(expectedRam.data(),state);original();
    for(unsigned a=0x1bb00;a<0x1bd00;++a)if(ram[a]!=expectedRam[a]){
        std::ostringstream s;s<<"Steering mismatch case="<<totalCases<<" address=0x"<<std::hex<<a<<" original="<<unsigned(ram[a])<<" native="<<unsigned(expectedRam[a]);throw std::runtime_error(s.str());
    }
    ++totalCases;return state;
}
scr::SteeringResponseInputs randomInputs(unsigned count){return {std::uint16_t(next()),std::uint16_t(next()),std::uint16_t(next()),std::uint8_t(next()%count),std::uint8_t(next()),std::uint8_t(next()),std::uint8_t(next()),(next()&1)?0x9cedcd02u:next()};}
scr::SteeringResponseState randomState(){for(unsigned a=0x1bb00;a<0x1bd00;++a)ram[a]=std::uint8_t(next());return f::readState(ram.data());}
std::uint16_t encodedOffset(unsigned offset){const auto word=std::uint16_t(offset+0xb100);return std::uint16_t((word<<8)|(word>>8));}
void geometryWord(scr::TrackGeometryData& d,unsigned a,std::uint16_t value){d.geometry[a]=std::uint8_t(value>>8);d.geometry[a+1]=std::uint8_t(value);}
scr::TrackGeometryData syntheticData(){
    scr::TrackGeometryData d;d.segmentCount=16;d.geometry.resize(0x10100);for(auto& b:d.geometry)b=std::uint8_t(next());
    constexpr std::array<unsigned,16> limits{4,6,8,10,12,16,30,64,126,128,130,254,0,1,2,255};
    for(unsigned i=0;i<16;++i){const auto a=0x1000+i*256;geometryWord(d,i*2,encodedOffset(a));d.geometry[a]=0xfa;d.geometry[a+1]=std::uint8_t((i&1)?0x80:0);
        d.geometry[a+0xfa]=std::uint8_t(limits[i]);d.geometry[a+0xfb]=std::uint8_t((i>>1)&1);d.geometry[a+0xfc]=std::uint8_t(next());d.geometry[a+0xfd]=std::uint8_t(next());}
    for(unsigned i=0;i<128;++i)geometryWord(d,32+i*2,encodedOffset(0x3000+i*256));
    for(unsigned i=0;i<100;++i){d.primaryGeometry[i]=std::uint8_t(next());d.alternateGeometry[i]=std::uint8_t(next());d.properties[i]=std::uint8_t((i&15)|((i&4)?0x10:0));d.primaryBase[i]=std::uint16_t(next());d.alternateBase[i]=std::uint16_t(next());}
    return d;
}
std::uint64_t segmentBoundaries(const scr::TrackGeometryData& data,const scr::SteeringOffsetTable& offsets){
    const auto before=totalCases;scr_test::encodeTrackGeometry(ram,data);
    constexpr std::array<unsigned,17> deltas{0,1,0xff,0x100,0x7ff,0x800,0x801,0x1dff,0x1e00,0x1e01,0x7fff,0x8000,0x8001,0xffff,0xff00,0xe200,0xe1ff};
    for(unsigned segment=0;segment<data.segmentCount;++segment)for(unsigned delta:deltas)for(unsigned direction:{0u,1u,0x7fu,0x80u,0xffu})for(unsigned step:{0u,1u,2u}){
        const auto config=scr::loadTrackSegmentConfiguration(data,std::uint8_t(segment));auto in=randomInputs(data.segmentCount);auto state=randomState();
        in.playerSegment=std::uint8_t(segment);in.trackProgression=std::uint8_t(config.mode-step);in.targetAngle=std::uint16_t(state.yawAngle+delta);state.steeringDirection=std::uint8_t(direction);
        (void)check(data,offsets,in,state);
    }
    return totalCases-before;
}
} // namespace

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a){if(!permittedRead(a,1)){fail("Invalid read",a,1);return 0;}return ram[a];}
unsigned int m68k_read_memory_16(unsigned int a){if(!permittedRead(a,2)){fail("Invalid read",a,2);return 0;}return f::word(ram.data(),a);}
unsigned int m68k_read_memory_32(unsigned int a){if(!permittedRead(a,4)){fail("Invalid read",a,4);return 0;}return f::longword(ram.data(),a);}
void m68k_write_memory_8(unsigned int a,unsigned int v){if(permittedWrite(a,1))ram[a]=std::uint8_t(v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(permittedWrite(a,2))f::putWord(ram.data(),a,std::uint16_t(v));}
void m68k_write_memory_32(unsigned int a,unsigned int v){if(permittedWrite(a,4))f::putLong(ram.data(),a,v);}
}

int main(int argc,char** argv)try{
    if(argc<2 || (argc-2)%2)throw std::runtime_error("Usage: steering_response_oracle payload.bin [label chipram.bin]...");const auto payload=readFile(argv[1]);if(payload.size()!=payloadSize)throw std::runtime_error("Expected original payload size");
    std::copy(payload.begin(),payload.end(),ram.begin()+base);permissions();const auto offsets=f::readOffsets(ram.data());
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    std::uint64_t captureCases=0,skippedCaptures=0,loadedBoundaryCases=0;
    std::cout<<"{\"cpu\":\"Musashi MC68000\",\"seed\":"<<seed<<",\"cpu_register_seed\":"<<cpuSeed<<",\"captures\":[";
    for(int argument=2;argument<argc;argument+=2){
        const std::string label=argv[argument];if(label.empty() || label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)throw std::runtime_error("Invalid capture label");
        const auto bytes=readFile(argv[argument+1]);if(bytes.size()!=ram.size())throw std::runtime_error("Capture must have512KiB RAM");
        for(auto span:spans)if(!std::equal(payload.begin()+span[0]-base,payload.begin()+span[1]-base,bytes.begin()+span[0]))throw std::runtime_error("Capture code differs from original");
        if(!std::equal(payload.begin()+f::tableAddress-base,payload.begin()+f::tableAddress-base+6,bytes.begin()+f::tableAddress))throw std::runtime_error("Capture offset table differs");
        std::copy(bytes.begin(),bytes.end(),ram.begin());const auto data=scr_test::decodeTrackGeometry(ram);const auto in=f::readInputs(ram.data());const auto old=f::readState(ram.data());
        if(argument>2)std::cout<<',';std::cout<<"{\"label\":\""<<label<<"\",\"segment_count\":"<<unsigned(data.segmentCount)<<",\"integrity_word\":"<<in.integrityWord;
        if(!data.segmentCount || data.segmentCount>100 || in.playerSegment>=data.segmentCount){++skippedCaptures;std::cout<<",\"replayed\":false,\"reason\":\"No valid loaded track at snapshot\"}";continue;}
        const auto result=check(data,offsets,in,old);++captureCases;
        std::cout<<",\"replayed\":true,\"stored_yaw\":"<<old.yawAngle<<",\"replayed_yaw\":"<<result.yawAngle<<",\"stored_acceleration\":"<<old.yawAcceleration<<",\"replayed_acceleration\":"<<result.yawAcceleration
                 <<",\"stored_outputs_agree\":"<<(old.yawAngle==result.yawAngle && old.yawAcceleration==result.yawAcceleration?"true":"false")<<'}';
        loadedBoundaryCases+=segmentBoundaries(data,offsets);
    }
    ram.fill(0);std::copy(payload.begin(),payload.end(),ram.begin()+base);auto data=syntheticData();const auto syntheticBoundaryCases=segmentBoundaries(data,offsets);scr_test::encodeTrackGeometry(ram,data);
    const auto beforeWords=totalCases;
    for(unsigned bank=0;bank<2;++bank)for(unsigned field=0;field<4;++field){
        auto in=randomInputs(data.segmentCount);auto state=randomState();in.playerSegment=std::uint8_t(bank);in.trackProgression=0x80;in.integrityWord=0x9cedcd02;in.wheelMovementActive=1;state.steeringDirection=bank?0x80:0;
        for(unsigned value=0;value<65536;++value){if(field==0)in.targetAngle=std::uint16_t(value);if(field==1)state.yawAngle=std::uint16_t(value);if(field==2)in.forwardVelocity=std::uint16_t(value);if(field==3)in.yawVelocity=std::uint16_t(value);(void)check(data,offsets,in,state);}
    }
    const auto wordSweeps=totalCases-beforeWords;
    const auto beforeBytes=totalCases;
    for(unsigned bank=0;bank<2;++bank){auto in=randomInputs(data.segmentCount);auto state=randomState();in.playerSegment=std::uint8_t(bank);in.integrityWord=0x9cedcd02;
        for(unsigned direction=0;direction<256;++direction)for(unsigned progression=0;progression<256;++progression){state.steeringDirection=std::uint8_t(direction);in.trackProgression=std::uint8_t(progression);(void)check(data,offsets,in,state);}}
    const auto bytePairs=totalCases-beforeBytes;
    const auto beforeNeighbor=totalCases;
    for(unsigned reverse=0;reverse<256;++reverse)for(unsigned inversion=0;inversion<256;++inversion){auto in=randomInputs(data.segmentCount);auto state=randomState();in.reverseDirectionLowByte=std::uint8_t(reverse);ram[0x1bc45]=std::uint8_t(inversion);(void)check(data,offsets,in,state);}
    const auto neighborPairs=totalCases-beforeNeighbor;
    // Keep the descriptor's steering base separate from its index byte here,
    // then cover every base value, including +0x2d overflow/-0x23 underflow.
    const auto beforeBase=totalCases;
    auto baseData=data;baseData.geometry[0x1000]=2;baseData.geometry[0x1002]=16;baseData.geometry[0x1003]=0;baseData.properties[0]=0;
    for(unsigned bank=0;bank<2;++bank)for(unsigned steeringBase=0;steeringBase<256;++steeringBase){
        baseData.geometry[0x1001]=std::uint8_t(bank?0x80:0);baseData.geometry[0x1008]=std::uint8_t(steeringBase);scr_test::encodeTrackGeometry(ram,baseData);
        for(unsigned direction:{0u,1u,0x80u,0xffu})for(unsigned velocity:{0u,1u,0xffffu,0x7fffu,0x8000u,0x8001u,0x75ffu,0x7600u,0x7601u}){
            auto in=randomInputs(data.segmentCount);auto state=randomState();in.playerSegment=0;in.trackProgression=0;in.reverseDirectionLowByte=0;in.targetAngle=0x100;state.yawAngle=0;
            in.forwardVelocity=std::uint16_t(velocity);in.integrityWord=0x9cedcd02;in.wheelMovementActive=1;state.steeringDirection=std::uint8_t(direction);(void)check(baseData,offsets,in,state);
        }
    }
    const auto baseByteCases=totalCases-beforeBase;scr_test::encodeTrackGeometry(ram,data);
    constexpr unsigned randomCases=200000;
    for(unsigned i=0;i<randomCases;++i){
        if(i%257==0){for(unsigned type=0;type<16;++type){const unsigned a=0x1000+type*256;data.geometry[a+1]=std::uint8_t(next());data.geometry[a+0xfb]=std::uint8_t(next());}
            for(unsigned s=0;s<data.segmentCount;++s)data.properties[s]=std::uint8_t(next());scr_test::encodeTrackGeometry(ram,data);}
        const auto state=randomState();(void)check(data,offsets,randomInputs(data.segmentCount),state);
    }
    // The runtime gate is equality, not a truthiness shortcut. Exercise every
    // single-bit corruption of the loaded word with otherwise live steering.
    const auto beforeGuard=totalCases;
    for(unsigned bit=0;bit<32;++bit)for(unsigned moving:{0u,1u,0x80u,0xffu}){auto in=randomInputs(data.segmentCount);auto state=randomState();in.integrityWord=0x9cedcd02u^(1u<<bit);in.wheelMovementActive=std::uint8_t(moving);(void)check(data,offsets,in,state);}
    const auto guardCases=totalCases-beforeGuard;
    unsigned rejected=0;for(unsigned variant=0;variant<4;++variant){auto malformed=data;auto in=randomInputs(data.segmentCount);auto state=randomState();if(variant==0)malformed.segmentCount=0;if(variant==1)malformed.segmentCount=101;if(variant==2)in.playerSegment=malformed.segmentCount;if(variant==3)malformed.geometry.clear();
        try{scr::calculateSteeringResponse(malformed,offsets,in,state);}catch(const std::out_of_range&){++rejected;continue;}catch(const std::invalid_argument&){++rejected;continue;}throw std::runtime_error("Malformed steering track accepted");}
    for(const char* name:{"advance_wrap","bank_offset","inverted_bank_offset","saturated_steering_scale","linear_steering_scale","advance_segment","manual_input","manual_bank_same_direction","manual_bank_opposite_direction","manual_unbanked","add_scale_high_byte","automatic_bank_steering","automatic_yaw_correction","nonzero_distance_high_byte","saturate_distance_factor","speed_word_clamp","minimum_yaw_correction","negative_yaw_correction","integrity_pass","inactive_wheel_gate","integrity_failure","steering_force","negate_steering_force","force_then_yaw_correction"})if(!coverage[name])throw std::runtime_error(std::string("Uncovered steering branch: ")+name);
    std::cout<<"],\"capture_cases\":"<<captureCases<<",\"skipped_captures\":"<<skippedCaptures<<",\"loaded_segment_boundary_cases\":"<<loadedBoundaryCases<<",\"synthetic_segment_boundary_cases\":"<<syntheticBoundaryCases
             <<",\"word_sweep_cases\":"<<wordSweeps<<",\"direction_progression_byte_pair_cases\":"<<bytePairs<<",\"retained_neighbor_byte_pair_cases\":"<<neighborPairs<<",\"steering_base_byte_cases\":"<<baseByteCases<<",\"random_cases\":"<<randomCases
             <<",\"integrity_corruption_cases\":"<<guardCases<<",\"rejected_invalid_inputs\":"<<rejected<<",\"total_cases\":"<<totalCases<<",\"mismatches\":0"
             <<",\"all_512_state_neighborhood_bytes_compared\":true,\"reads_confined_to_documented_state_geometry_code_and_stack\":true,\"writes_confined_to40_output_bytes_and_stack\":true"
             <<",\"original_d1_high_byte_zero_and_d2_word_zero_return_verified\":true,\"maximum_stack_depth_bytes\":4,\"minimum_instructions\":"<<minInstructions<<",\"maximum_instructions\":"<<maxInstructions
             <<",\"total_instructions\":"<<totalInstructions<<",\"total_output_bytes_written\":"<<totalOutputBytes<<",\"coverage\":{";
    bool first=true;for(const auto& p:coverage){if(!first)std::cout<<',';first=false;std::cout<<'"'<<p.first<<"\":"<<p.second;}
    std::cout<<"},\"final_input_prng_state\":"<<rng<<",\"final_cpu_prng_state\":"<<cpuRng<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<"\n"<<e.what()<<'\n';return 1;}
