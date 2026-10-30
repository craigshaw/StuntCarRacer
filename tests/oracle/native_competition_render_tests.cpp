#include "presentation/native_competition_render.h"
#include "race/native_race_initialization.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using Memory=scr::runtime::NativeMemory;
Memory ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000,frame=0x6a594;
unsigned stop{},pc{},instructions{};
std::uint64_t totalInstructions{},comparedPixels{};
std::uint64_t comparedPaletteWords{};
unsigned careerCases{},awardCases{},skippedAwards{},snapshotCases{};
unsigned forfeitScreenCases{};
std::filesystem::path outputDirectory;
bool invalid{},stopped{};
std::uint8_t readByte(unsigned a){
    if(a<ram.size())return ram[a];
    if(a>=stackBase&&a<stackTop)return stack[a-stackBase];
    if(a>=sentinel&&a<sentinel+4)return (a&1)?0x71:0x4e;
    if(a>=0xbfd000&&a<0xe00000)return 0;
    invalid=true;return 0;
}
void writeByte(unsigned a,std::uint8_t v){
    if(a>=stackBase&&a<stackTop){stack[a-stackBase]=v;return;}
    if(a<ram.size()){ram[a]=v;return;}
    if(a>=0xbfd000&&a<0xe00000)return;
    invalid=true;
}
void skipCall(){const unsigned sp=m68k_get_reg(nullptr,M68K_REG_SP);
    const unsigned target=(unsigned(readByte(sp))<<24)|(unsigned(readByte(sp+1))<<16)|(unsigned(readByte(sp+2))<<8)|readByte(sp+3);
    m68k_set_reg(M68K_REG_SP,sp+4);m68k_set_reg(M68K_REG_PC,target);}
void hook(unsigned address){
    pc=address;
    if(address==stop||address==sentinel){stopped=true;m68k_end_timeslice();return;}
    if(address==0x6444e||address==0x64470){skipCall();return;}
    if((address&1)||address<0xe700||address>=0x724dc||++instructions>20000000){invalid=true;m68k_end_timeslice();}
}
void run(unsigned entry,unsigned boundary){
    invalid=stopped=false;instructions=0;stop=boundary;m68k_pulse_reset();stack.fill(0);
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(2000000000);
    if(invalid||!stopped)throw std::runtime_error("Original competition render failed PC="+std::to_string(pc));
    totalInstructions+=instructions;
}
void compare(const scr::runtime::NativeMenuImage& image,unsigned page,unsigned variation,unsigned buffer=frame,unsigned paletteAddress=0x23c32){
    for(unsigned i=0;i<16;++i){
        const unsigned colour=(unsigned(ram[paletteAddress+2*i])<<8)|ram[paletteAddress+2*i+1];
        auto channel=[](unsigned v){v&=7;return (v*2+(v?1:0))*17;};
        const unsigned expected=(channel(colour>>8)<<16)|(channel(colour>>4)<<8)|channel(colour);
        ++comparedPaletteWords;if(image.palette[i]!=expected)throw std::runtime_error("Competition palette mismatch page="+std::to_string(page));
    }
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x){
        unsigned expected=0;for(unsigned p=0;p<4;++p)
            if(ram[buffer+p*8000+y*40+x/8]&(0x80u>>(x%8)))expected|=1u<<p;
        ++comparedPixels;
        if(image.indices[y*320+x]!=expected){
            std::ofstream actualFile("analysis/opponent-races/actual.indices",std::ios::binary);actualFile.write(reinterpret_cast<const char*>(image.indices.data()),image.indices.size());
            std::array<std::uint8_t,64000> expectedIndices{};
            for(unsigned yy=0;yy<200;++yy)for(unsigned xx=0;xx<320;++xx)for(unsigned p=0;p<4;++p)
                if(ram[buffer+p*8000+yy*40+xx/8]&(0x80u>>(xx%8)))expectedIndices[yy*320+xx]|=std::uint8_t(1u<<p);
            std::ofstream expectedFile("analysis/opponent-races/expected.indices",std::ios::binary);expectedFile.write(reinterpret_cast<const char*>(expectedIndices.data()),expectedIndices.size());
            throw std::runtime_error("Competition pixel mismatch page="+std::to_string(page)+
            " variation="+std::to_string(variation)+" x="+std::to_string(x)+" y="+std::to_string(y)+
            " expected="+std::to_string(expected)+" actual="+std::to_string(image.indices[y*320+x]));}
    }
}
void exportImage(const scr::runtime::NativeMenuImage& image,const std::string& name){
    if(outputDirectory.empty())return;
    std::ofstream out(outputDirectory/(name+".ppm"),std::ios::binary);out<<"P6\n320 200\n255\n";
    for(auto index:image.indices)for(unsigned shift:{16u,8u,0u})out.put(char(image.palette[index]>>shift));
    if(!out)throw std::runtime_error("Cannot export career screen");
}
void careerEndings(const Memory& baseline){
    using namespace scr::runtime;
    unsigned variation=1000;
    for(unsigned context:{0u,11u})for(unsigned division=0;division<4;++division)
    for(unsigned place=0;place<3;++place)for(unsigned names=0;names<4;++names,++variation){
        auto native=std::make_unique<Memory>(baseline);
        (*native)[0x1c9ce]=(*native)[0x1bb5f]=std::uint8_t(division);(*native)[0x1c9d0]=std::uint8_t(context);
        (*native)[0x5eb79]=11;(*native)[0x593f6]=0x19;(*native)[0x1bbb1]=0;
        for(unsigned i=0;i<12;++i)(*native)[0x1ca0e+i]=(*native)[0x1c9c2+i]=std::uint8_t(i);
        const unsigned offset=(*native)[0x5ab42+division];std::swap((*native)[0x1ca0e+offset+place],(*native)[0x1ca0e+11]);
        static constexpr char values[][13]={"Craig       ","ABCDEFGHIJKL","abcdefghijkl","AZaz/9 .0:Cx"};
        for(unsigned i=0;i<12;++i)(*native)[0x1ecab+16*11+i]=std::uint8_t(values[names][i]);
        ram=*native;run(0x64570,0x645ac);run(0x646d6,0x64828);
        finishNativeSeason(*native);
        if((*native)[0x593f6]!=ram[0x593f6])throw std::runtime_error("Career award flag mismatch context="+std::to_string(context)+
            " division="+std::to_string(division)+" place="+std::to_string(place)+" expected="+std::to_string(ram[0x593f6])+" actual="+std::to_string((*native)[0x593f6]));
        if((*native)[0x1c9ce]!=ram[0x1c9ce]||(*native)[0x1c9d0]!=ram[0x1c9d0])throw std::runtime_error("Career presentation state transition mismatch");
        const auto image=makeNativeCompetitionImage(*native,NativeMenuPage::SeasonEnd,division,context);
        compare(image,unsigned(NativeMenuPage::SeasonEnd),variation);++careerCases;
        if((*native)[0x1c9ce]!=division||(*native)[0x1c9d0]!=context)++snapshotCases;
        const std::string label="context-"+std::to_string(context)+"-division-"+std::to_string(division)+"-place-"+std::to_string(place);
        if(!names)exportImage(image,label+"-end");
        const auto pixelsBefore=std::make_unique<Memory>(ram);
        run(0x5933a,0x6488a);
        if(ram[0x593f6]&128){
            if(pc!=sentinel)throw std::runtime_error("Original skipped award unexpectedly waited for fire");
            for(unsigned a=frame;a<frame+64000;++a)if(ram[a]!=(*pixelsBefore)[a])throw std::runtime_error("Original skipped award changed bitmap");
            ++skippedAwards;
        }else{
            if(pc!=0x6488a)throw std::runtime_error("Original award did not wait for fire");
            const auto award=makeNativeCompetitionImage(*native,NativeMenuPage::SeasonAward,division,context);
            compare(award,unsigned(NativeMenuPage::SeasonAward),variation,frame+32000,0xe85a);++awardCases;
            if(!names)exportImage(award,label+"-award");
        }
        ram=*native;run(0x58888,0x6488a);
        const auto roster=makeNativeCompetitionImage(*native,NativeMenuPage::Roster,(*native)[0x1c9ce],(*native)[0x1c9d0]);
        compare(roster,unsigned(NativeMenuPage::Roster),variation,frame+32000,0xe85a);++careerCases;
        if(!names)exportImage(roster,label+"-roster");
    }
}
void forfeitEndings(const Memory& baseline){
    using namespace scr::runtime;
    unsigned variation=2000;
    for(unsigned context:{0u,11u})for(unsigned division=0;division<4;++division)
    for(unsigned round:{0u,3u,5u})for(unsigned leading=0;leading<2;++leading,++variation){
        auto native=std::make_unique<Memory>(baseline);
        (*native)[0x1c9ce]=(*native)[0x1bb5f]=std::uint8_t(division);(*native)[0x1c9d0]=std::uint8_t(context);
        (*native)[0x5eb79]=11;(*native)[0x1ca22]=0x80;(*native)[0x5eb78]=6;
        (*native)[0x1ca35]=(*native)[0x5eb7b]=std::uint8_t(round);(*native)[0x1bb5b]=0;
        for(unsigned i=0;i<12;++i){(*native)[0x1ca0e+i]=(*native)[0x1c9c2+i]=std::uint8_t(i);
            (*native)[0x1c9de+i]=(*native)[0x1c9ea+i]=(*native)[0x1c9f6+i]=(*native)[0x1ca02+i]=0;}
        const unsigned offset=(*native)[0x5ab42+division];std::swap((*native)[0x1c9c2+offset+1],(*native)[0x1c9c2+11]);
        if(leading){(*native)[0x1c9de+11]=40;(*native)[0x1c9ea+11]=20;(*native)[0x1ca02+11]=100;}
        ram=*native;forfeitNativeSeason(*native);run(0x5d000,0x5d1be);
        run(0x64570,0x645ac);run(0x646d6,0x64828);finishNativeSeason(*native);
        compare(makeNativeCompetitionImage(*native,NativeMenuPage::SeasonEnd,division,context),unsigned(NativeMenuPage::SeasonEnd),variation);
        if((*native)[0x593f6]!=ram[0x593f6])throw std::runtime_error("F1 season-end award gate mismatch");++forfeitScreenCases;
    }
}
} // namespace
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return readByte(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1)invalid=true;return (unsigned(readByte(a))<<8)|readByte(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){writeByte(a,std::uint8_t(v));}
void m68k_write_memory_16(unsigned a,unsigned v){if(a&1)invalid=true;writeByte(a,std::uint8_t(v>>8));writeByte(a+1,std::uint8_t(v));}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc<2||argc>3)throw std::invalid_argument("Usage: native_competition_render_tests original-payload.bin [image-directory]");
    if(argc==3){outputDirectory=argv[2];std::filesystem::create_directories(outputDirectory);}
    std::ifstream file(argv[1],std::ios::binary);
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    if(payload.size()!=408540)throw std::invalid_argument("Wrong original payload size");
    auto base=std::make_unique<Memory>();std::copy(payload.begin(),payload.end(),base->begin()+0xe700);
    scr::runtime::initializeNativeAssets(*base);(*base)[0x5eb79]=11;scr::runtime::initializeNativeGameData(*base);
    scr::runtime::initializeNativeMenuSession(*base);
    (*base)[0x1bbbc]=0x80;(*base)[0x5d724]=0x41;(*base)[0x1bb6d]=(*base)[0x1bb6e]=0;(*base)[0x64530]=0;(*base)[0x5eb76]=0;
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    unsigned cases=0;
    for(unsigned variation=0;variation<16;++variation){
        auto state=std::make_unique<Memory>(*base);const unsigned division=variation&3;
        (*state)[0x1c9ce]=(*state)[0x1bb5f]=std::uint8_t(division);(*state)[0x1c9d0]=(variation&4)?11:0;
        (*state)[0x1ca35]=2;(*state)[0x1ca22]=0x80;(*state)[0x5eb78]=6;(*state)[0x5eb7b]=2;
        (*state)[0x1bbb1]=0;(*state)[0x1ca25]=9;(*state)[0x1ca26]=11;
        scr::runtime::configureNativeSeasonMatch(*state);
        (*state)[0x1ca33]=(*state)[0x648c2+2*division];
        if(variation&8)for(unsigned i=1;i<=12;++i)(*state)[0x1ecaa+16*11+i]=(*state)[0x1ecaa+16*9+i];
        for(unsigned id=0;id<12;++id){(*state)[0x1c9f6+id]=5;(*state)[0x1c9de+id]=std::uint8_t(id%5);(*state)[0x1c9ea+id]=std::uint8_t(id%3);(*state)[0x1ca02+id]=std::uint8_t(id);}
        if((variation&11)==11)std::swap((*state)[0x1ca0e],(*state)[0x1ca0e+11]);
        for(auto page:{scr::runtime::NativeMenuPage::Matchup,scr::runtime::NativeMenuPage::RoundResults,scr::runtime::NativeMenuPage::Standings}){
            ram=*state;run(0x64570,0x645ac);
            const unsigned entry=page==scr::runtime::NativeMenuPage::Matchup?0x64664:page==scr::runtime::NativeMenuPage::RoundResults?0x6465c:0x646e8;
            run(entry,0x64828);compare(scr::runtime::makeNativeCompetitionImage(*state,page,division),unsigned(page),variation);++cases;
        }
        for(unsigned flags:{0u,0x80u,0xc0u}){
            (*state)[0x1bbb4]=std::uint8_t(flags);(*state)[0x1bca2]=flags==0xc0?0x80:0;
            ram=*state;run(0x5930c,0x6488a);
            compare(scr::runtime::makeNativeCompetitionImage(*state,scr::runtime::NativeMenuPage::Results,division),unsigned(scr::runtime::NativeMenuPage::Results),variation,frame+32000,0xe85a);++cases;
        }
        for(unsigned flags:{0xc0u,0xc1u,0x81u,0x41u}){
            if(variation&8){
                const unsigned slot=16*((*state)[0x648c2+(*state)[0x1ca33]]+((*state)[0x1c9d0]?8:0));
                for(unsigned i=0;i<12;++i){(*state)[0x7a61a+slot+i]=(*state)[0x1ecaa+16*4+1+i];(*state)[0x7a71a+slot+i]=(*state)[0x1ecaa+16*7+1+i];}
                (*state)[0x7a61a+slot+12]=0;(*state)[0x7a61a+slot+13]=0x47;(*state)[0x7a61a+slot+14]=0x38;
                (*state)[0x7a71a+slot+12]=1;(*state)[0x7a71a+slot+13]=0x20;(*state)[0x7a71a+slot+14]=0x56;
            }
            (*state)[0x1bbb1]=std::uint8_t(flags);scr::runtime::transferNativeLapRecords(*state,true);
            ram=*state;run(0x64570,0x645ac);run(0x64664,0x64828);
            compare(scr::runtime::makeNativeCompetitionImage(*state,scr::runtime::NativeMenuPage::Records,division),unsigned(scr::runtime::NativeMenuPage::Records),variation);++cases;
        }
        ram=*state;run(0x58888,0x6488a);
        compare(scr::runtime::makeNativeCompetitionImage(*state,scr::runtime::NativeMenuPage::Roster,division),unsigned(scr::runtime::NativeMenuPage::Roster),variation,frame+32000,0xe85a);++cases;
        (*state)[0x593f6]=11;ram=*state;run(0x5933a,0x6488a);
        compare(scr::runtime::makeNativeCompetitionImage(*state,scr::runtime::NativeMenuPage::SeasonAward,division),unsigned(scr::runtime::NativeMenuPage::SeasonAward),variation,frame+32000,0xe85a);++cases;
        ram=*state;run(0x64570,0x645ac);run(0x646d6,0x64828);
        scr::runtime::finishNativeSeason(*state);
        compare(scr::runtime::makeNativeCompetitionImage(*state,scr::runtime::NativeMenuPage::SeasonEnd,division,(variation&4)?11:0),unsigned(scr::runtime::NativeMenuPage::SeasonEnd),variation);++cases;
    }
    careerEndings(*base);
    forfeitEndings(*base);
    std::cout<<"{\"cases\":"<<cases+careerCases+awardCases+forfeitScreenCases<<",\"compared_pixels\":"<<comparedPixels<<",\"compared_palette_words\":"<<comparedPaletteWords<<
        ",\"career_cases\":"<<careerCases<<",\"career_award_cases\":"<<awardCases<<",\"career_skipped_awards\":"<<skippedAwards<<",\"career_snapshot_cases\":"<<snapshotCases<<
        ",\"forfeit_screen_cases\":"<<forfeitScreenCases<<
        ",\"mismatches\":0,\"original_instructions\":"<<totalInstructions
        <<",\"original_code_unmodified\":true,\"palette_fades_and_hardware_skipped\":true}\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
