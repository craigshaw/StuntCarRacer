#include "native_save_menu.h"
#include "native_race_initialization.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace scr::viewer;
NativeMemory original{},baseline{},native{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000,frame=0x6a594;
unsigned pc{},boundary{},instructions{},cases{},navigationCases{},editingCases{},directoryCases{};
unsigned modalCases{},selectionCases{};
unsigned inputCases{};
std::uint64_t totalInstructions{},comparedPixels{},comparedBytes{};
bool stopped{},invalid{},textLoop{};
unsigned read(unsigned a){
    if(a<original.size())return original[a];
    if(a>=stackBase&&a<stackTop)return stack[a-stackBase];
    if(a>=sentinel&&a<sentinel+4)return (a&1)?0x71:0x4e;
    invalid=true;return 0;
}
void write(unsigned a,unsigned v){
    if(a<original.size()){original[a]=std::uint8_t(v);return;}
    if(a>=stackBase&&a<stackTop){stack[a-stackBase]=std::uint8_t(v);return;}invalid=true;
}
void putLong(NativeMemory& m,unsigned a,unsigned v){for(unsigned i=0;i<4;++i)m.at(a+i)=std::uint8_t(v>>(24-8*i));}
void require(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
void hook(unsigned a){
    pc=a;if(a==boundary||(textLoop&&(a==0x5b744||a==0x5b836))){stopped=true;m68k_end_timeslice();return;}
    if(++instructions>2000000||a<0xe700||a>=0x724dc||(a&1)){invalid=true;m68k_end_timeslice();}
}
void cpu(unsigned entry,unsigned stop=sentinel,unsigned d0=0,unsigned d1=0,unsigned d2=0){
    boundary=stop;invalid=stopped=false;instructions=0;stack.fill(0);m68k_pulse_reset();
    for(unsigned r=0;r<15;++r)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+r),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);
    for(unsigned i=0;i<4;++i)stack[4092+i]=std::uint8_t(sentinel>>(24-8*i));
    m68k_set_reg(M68K_REG_D0,d0);m68k_set_reg(M68K_REG_D1,d1);m68k_set_reg(M68K_REG_D2,d2);
    m68k_set_reg(M68K_REG_PC,entry);m68k_execute(2000000000);
    if(invalid||!stopped)throw std::runtime_error("Original save menu failed PC="+std::to_string(pc));
    totalInstructions+=instructions;
}
void compare(const NativeSaveMenu& menu,const std::string& stage){
    const auto image=menu.image();
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x){
        unsigned expected=0;for(unsigned p=0;p<4;++p)if(original[frame+p*8000+y*40+x/8]&(128u>>(x%8)))expected|=1u<<p;
        ++comparedPixels;
        if(image.indices[y*320+x]!=expected)throw std::runtime_error(stage+" pixel mismatch x="+std::to_string(x)+" y="+std::to_string(y)+
            " expected="+std::to_string(expected)+" actual="+std::to_string(image.indices[y*320+x]));
    }
    ++cases;
}
void compareByte(unsigned a,const std::string& stage){
    ++comparedBytes;require(native[a]==original[a],stage+" state mismatch address="+std::to_string(a)+
        " expected="+std::to_string(original[a])+" actual="+std::to_string(native[a]));
}
void compareSlotState(const std::string& stage){
    for(unsigned a:{0x62bb4,0x62bb6,0x62bb7,0x62bb8,0x62bb9,0x62bba,0x5b83e})compareByte(a,stage);
    for(unsigned a=0x1ed6b;a<0x1ed73;++a)compareByte(a,stage);
}
void names(NativeMemory& m,unsigned variation){
    static constexpr char values[][9]={"ABCDEFGH","        ","CRAIG   ","HALL    ","MP ONE  ","a./:9 Zz"};
    for(unsigned i=0;i<30;++i){
        const unsigned a=0x7a21a+16*i;
        std::copy_n(values[(i+variation)%6],8,m.begin()+a);m[a+15]=std::uint8_t((variation&1)&&i%4==0);
    }
    putLong(m,0x7a234,0x47826653);
}
void originalFrame(){cpu(0x64570,0x645ac);cpu(0x64c6e);}
void beginSelector(NativeSaveMenu& menu,bool writing){
    require(menu.update(0,0).action==NativeSaveMenuAction::None,"Initial modal skipped release");
    setNativeRawKey(native,0x44,true);
    require(menu.update(0,0).action==NativeSaveMenuAction::ReadDirectory,"Initial modal did not request directory");
    cpu(0x64c6e);cpu(0x59806,sentinel,15);cpu(0x625a8,sentinel,0,writing?0x16:0);
    compare(menu,"Selector title");
    require(menu.directoryLoaded().action==NativeSaveMenuAction::Changed,"Directory did not display slots");
    cpu(0x62d34);original[0x1bbd3]=0;cpu(0x627ea,0x62852);cpu(0x62858,0x62862);
    compare(menu,"Initial selected slot");compareSlotState("Initial selected slot");
    original[0x62bb5]=128;setNativeRawKey(native,0x44,false);(void)menu.update(0,0);
}
void exportPpm(const NativeSaveMenu& menu,const char* path){
    const auto image=menu.image();std::ofstream out(path,std::ios::binary);out<<"P6\n320 200\n255\n";
    for(auto index:image.indices)for(unsigned shift:{16u,8u,0u})out.put(char(image.palette[index]>>shift));
}
void pixelsAndNavigation(const char* imagePath){
    for(unsigned writing=0;writing<2;++writing)for(unsigned variation=0;variation<2;++variation)for(unsigned selected=0;selected<30;++selected){
        native=baseline;native[0x1ca31]=std::uint8_t(writing);names(native,variation);
        native[0x7a224]=native[0x62bb6]=std::uint8_t(selected);native[0x7a225]=native[0x62bb7]=std::uint8_t(selected);
        original=native;originalFrame();NativeSaveMenu menu(native,writing!=0);
        cpu(0x1b82a,sentinel,0,0x12a);compare(menu,"Insert modal");beginSelector(menu,writing!=0);
        if(imagePath&&writing&&variation==0&&selected==0)exportPpm(menu,imagePath);
        for(unsigned raw:{0x4cu,0x4du,0x4eu,0x4fu}){
            const unsigned bits=raw==0x4c?32:raw==0x4d?64:raw==0x4e?8:16;
            setNativeRawKey(native,raw,true);const auto update=menu.update(0,0);
            require(update.action==NativeSaveMenuAction::Changed,"Directional input did not repaint slots");
            original[0x5b83d]=std::uint8_t(bits);cpu(0x62a70);
            cpu(raw==0x4c?0x62ab6:raw==0x4d?0x62aca:raw==0x4e?0x62afe:0x62b12);
            cpu(0x627ea,0x62852);cpu(0x62858,0x62862);compare(menu,"Navigation");compareSlotState("Navigation");++navigationCases;
            setNativeRawKey(native,raw,false);(void)menu.update(0,0);
        }
        setNativeRawKey(native,0x45,true);require(menu.update(0,0).action==NativeSaveMenuAction::Cancel,"Escape did not cancel");
        require(native[0x1bb92]==128&&native[0x62bb5]==0,"Cancelled save menu retained input mode");
    }
}
void editNames(){
    for(unsigned length=0;length<=8;++length)for(unsigned slot:{0u,9u,10u,19u,20u,29u}){
        native=baseline;names(native,0);native[0x1ca31]=1;native[0x62bb6]=native[0x62bb7]=native[0x7a224]=native[0x7a225]=std::uint8_t(slot);
        std::fill_n(native.begin()+0x7a21a+16*slot,8,' ');std::fill_n(native.begin()+0x7a21a+16*slot,length,'A');native[0x7a21a+16*slot+15]=0;
        original=native;originalFrame();NativeSaveMenu menu(native,true);cpu(0x1b82a,sentinel,0,0x12a);beginSelector(menu,true);
        original[0x1bbd5]=0xc0;original[0x5b83c]=8;original[0x5b83d]=0;
        original[0x5983e]=std::uint8_t(original[0x5983e]+original[0x5b83e]);unsigned position=length;
        for(unsigned raw:{0x41u,0x20u,0x40u,0x33u,0x21u,0x41u}){
            const unsigned table=(unsigned(native[0x1b890])<<24)|(unsigned(native[0x1b891])<<16)|(unsigned(native[0x1b892])<<8)|native[0x1b893];
            const unsigned code=(raw<<8)|native[table+raw];
            setNativeRawKey(native,raw,true);(void)menu.update(0,0);setNativeRawKey(native,raw,false);
            textLoop=true;cpu(0x5b752,sentinel,code,position,std::uint8_t(0xc0+position));textLoop=false;
            position=std::uint8_t(m68k_get_reg(nullptr,M68K_REG_D1));
            compare(menu,"Name editing");for(unsigned a=0x1ed6b;a<0x1ed73;++a)compareByte(a,"Name editing");++editingCases;
        }
    }
}
void headerCommit(){
    for(unsigned writing=0;writing<2;++writing)for(unsigned slot=0;slot<30;++slot){
        native=baseline;for(unsigned i=0;i<512;++i)native[0x7a21a+i]=std::uint8_t(i*37+slot);
        native[0x62bb6]=7;native[0x62bb7]=19;
        std::array<std::uint8_t,8> name{};for(unsigned i=0;i<8;++i)name[i]=std::uint8_t('A'+(slot+i)%26);
        original=native;std::copy(name.begin(),name.end(),original.begin()+0x1ed6b);original[0x62bb4]=std::uint8_t(slot);
        if(writing)original[0x62bb7]=std::uint8_t((slot+1)%30);else original[0x62bb6]=std::uint8_t(slot);
        original[0x62d32]=0;original[0x62d33]=std::uint8_t(slot+23);cpu(0x62bbe,0x62c18);
        commitNativeSaveDirectory(native,slot,name,writing!=0);
        for(unsigned a=0x7a21a;a<0x7a41a;++a)compareByte(a,"Directory commit");++directoryCases;
    }
}
void invalidDirectories(){
    for(unsigned writing=0;writing<2;++writing){
        native=baseline;native[0x1ca31]=std::uint8_t(writing);std::fill_n(native.begin()+0x7a21a,512,0);
        original=native;originalFrame();NativeSaveMenu menu(native,writing!=0);cpu(0x1b82a,sentinel,0,0x12a);
        (void)menu.update(0,0);setNativeRawKey(native,0x44,true);
        require(menu.update(0,0).action==NativeSaveMenuAction::ReadDirectory,"Invalid directory prompt skipped");
        cpu(0x64c6e);cpu(0x59806,sentinel,15);cpu(0x625a8,sentinel,0,writing?0x16:0);
        (void)menu.directoryLoaded();original[0x62bbb]=128;original[0x62bb6]=original[0x62bb7]=0;
        if(writing){
            cpu(0x1b82a,sentinel,0,0xf0);compare(menu,"Format warning");++modalCases;
            setNativeRawKey(native,0x44,false);(void)menu.update(0,0);setNativeRawKey(native,0x44,true);
            require(menu.update(0,0).action==NativeSaveMenuAction::ReadDirectory,"Format warning did not repeat directory read");
            cpu(0x64c6e);original[0x1bb6e]=1;cpu(0x625a8,sentinel,0,0x16);original[0x1bb6e]=0;
            compare(menu,"Format retry title");++modalCases;(void)menu.directoryLoaded();cpu(0x1b82a,sentinel,0,0xf0);
        }
        cpu(0x62d34);original[0x1bbd3]=0;cpu(0x627ea,0x62852);cpu(0x62858,0x62862);
        compare(menu,"Empty directory");compareSlotState("Empty directory");++modalCases;
    }
}
void selectAndErrors(){
    static constexpr char values[][9]={"NORMAL  ","HALL abc","MP X    ","        "};
    for(unsigned writing=0;writing<2;++writing)for(unsigned multiplayer=0;multiplayer<2;++multiplayer)for(unsigned variant=0;variant<4;++variant){
        native=baseline;names(native,0);native[0x1ca31]=std::uint8_t(writing);native[0x5eb76]=std::uint8_t(multiplayer);
        native[0x62bb6]=native[0x62bb7]=native[0x7a224]=native[0x7a225]=0;
        std::copy_n(values[variant],8,native.begin()+0x7a21a);native[0x7a229]=0;
        original=native;originalFrame();NativeSaveMenu menu(native,writing!=0);cpu(0x1b82a,sentinel,0,0x12a);beginSelector(menu,writing!=0);
        const unsigned position=native[0x5b83e];
        if(writing){
            original[0x1bbd5]=0xc0;original[0x5b83c]=8;original[0x5b83d]=0;
            original[0x5983e]=std::uint8_t(original[0x5983e]+position);
        }
        setNativeRawKey(native,0x44,true);const auto selected=menu.update(0,0);
        if(variant==3&&!writing){require(selected.action==NativeSaveMenuAction::None,"Empty load slot was selected");continue;}
        if(writing){
            textLoop=true;cpu(0x5b752,sentinel,0x440d,position,std::uint8_t(0xc0+position));textLoop=false;cpu(0x64834);
            if(variant==3){cpu(0x628bc,0x5b74c);compare(menu,"Blank name retry");++modalCases;continue;}
            cpu(0x628bc,0x6294a);
        }
        const bool incompatible=writing&&((variant==0&&multiplayer)||(variant==2&&!multiplayer));
        if(incompatible){
            cpu(0x5f4b6,0x5f560);require(menu.phase()==NativeSaveMenuPhase::Incompatible,"Invalid save kind was accepted");
            compare(menu,"Incompatible name");++modalCases;
            setNativeRawKey(native,0x44,false);(void)menu.update(0,0);setNativeRawKey(native,0x44,true);(void)menu.update(0,0);
            cpu(0x5f566,0x5f580);cpu(0x627ea,0x62852);cpu(0x62858,0x62862);
            compare(menu,"Dismissed incompatible name");++modalCases;continue;
        }
        require(selected.action==NativeSaveMenuAction::Select,"Nonempty slot did not select");
        cpu(0x62954,0x6296e);compare(menu,"Confirmed selection");++selectionCases;
        require(selected.kind==(variant==1?0x40:variant==2?1:0),"Special-name classification mismatch");
        for(unsigned error:{0x22u,0x1cu,0x80u}){
            menu.operationFailed(std::uint8_t(error));cpu(0x1b82a,sentinel,0,error==0x22?0x2c:error==0x1c?0x3c:0xd4);
            compare(menu,"Storage error");++modalCases;
        }
        menu.invalidSaveData();cpu(0x1b82a,sentinel,0,0x68);compare(menu,"Incorrect save data");++modalCases;
    }
}
void inputWaits(){
    for(unsigned controls:{1u,2u,4u,8u,16u}){
        native=baseline;names(native,0);native[0x7a224]=native[0x7a225]=0;
        NativeSaveMenu menu(native,false);
        require(menu.update(std::uint8_t(controls),0).action==NativeSaveMenuAction::None,"Held input skipped initial release");
        require(menu.update(0,0).action==NativeSaveMenuAction::None,"Initial release accepted as press");
        require(menu.update(16,0).action==NativeSaveMenuAction::ReadDirectory,"Controller fire did not accept insert prompt");
        (void)menu.directoryLoaded();
        for(unsigned n=0;n<4;++n)require(menu.update(1,0).action==NativeSaveMenuAction::None,"Held direction skipped release");
        (void)menu.update(0,0);
        const unsigned expected=controls==1?29:controls==2?1:controls==8?10:0;
        if(controls==16){require(menu.update(16,0).action==NativeSaveMenuAction::Select,"Controller fire did not load selected slot");}
        else{
            require(menu.update(std::uint8_t(controls),0).action==NativeSaveMenuAction::Changed,"Controller direction did not move");
            require(menu.slot()==expected,"Controller slot priority mismatch");
        }
        ++inputCases;
    }
    for(unsigned writing=0;writing<2;++writing){
        native=baseline;names(native,0);native[0x7a224]=native[0x7a225]=0;NativeSaveMenu menu(native,writing!=0);
        (void)menu.update(0,0);(void)menu.update(16,0);(void)menu.directoryLoaded();(void)menu.update(0,0);
        const auto selected=menu.update(16,0);require(selected.action==NativeSaveMenuAction::Select,"Fire did not finish name/slot");
        menu.operationFailed(0x80);require(menu.update(0,0).action==NativeSaveMenuAction::None,"I/O retry accepted no input");
        require(menu.update(16,0).action==NativeSaveMenuAction::Changed,"I/O retry did not start delay");
        require(menu.update(0,0).action==NativeSaveMenuAction::None,"I/O retry omitted original busy delay");
        require(menu.update(0,60).action==NativeSaveMenuAction::Select,"I/O retry did not repeat selection");
        require(native[0x1bb1a]==0&&native[0x1bb1b]==0,"I/O retry did not finish delay counters");
        menu.operationFailed(0x22);setNativeRawKey(native,0x45,true);
        require(menu.update(0,0).action==NativeSaveMenuAction::Cancel,"I/O Escape did not abort");++inputCases;
    }
    native=baseline;std::fill_n(native.begin()+0x7a21a,512,0);NativeSaveMenu blank(native,true);
    (void)blank.update(0,0);(void)blank.update(16,0);(void)blank.directoryLoaded();(void)blank.update(0,0);
    setNativeRawKey(native,0x45,true);require(blank.update(0,0).action==NativeSaveMenuAction::Cancel,"Blank-directory Escape did not abort");++inputCases;
}
}
extern "C" {
unsigned int m68k_read_memory_8(unsigned a){return read(a);}
unsigned int m68k_read_memory_16(unsigned a){if(a&1)invalid=true;return (read(a)<<8)|read(a+1);}
unsigned int m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned a,unsigned v){write(a,v);}
void m68k_write_memory_16(unsigned a,unsigned v){if(a&1)invalid=true;write(a,v>>8);write(a+1,v);}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
}
int main(int argc,char** argv)try{
    if(argc<2||argc>3)throw std::invalid_argument("Usage: native_save_menu_tests original-payload.bin [screen.ppm]");
    std::ifstream file(argv[1],std::ios::binary);const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    require(payload.size()==408540,"Wrong original payload size");std::copy(payload.begin(),payload.end(),baseline.begin()+0xe700);
    initializeNativeAssets(baseline);initializeNativeGameData(baseline);
    baseline[0x1bbbc]=128;baseline[0x5d724]=0x41;baseline[0x1c9ce]=3;baseline[0x5924a]=baseline[0x1bb6d]=baseline[0x1bb6e]=0;
    baseline[0x59843]=baseline[0x59844]=baseline[0x5eb76]=0;
    putLong(baseline,0x6a584,frame);putLong(baseline,0x6a588,frame);putLong(baseline,0x6a58c,frame);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);
    pixelsAndNavigation(argc==3?argv[2]:nullptr);editNames();headerCommit();invalidDirectories();selectAndErrors();inputWaits();
    std::cout<<"{\"mismatches\":0,\"pixel_cases\":"<<cases<<",\"compared_pixels\":"<<comparedPixels<<
        ",\"compared_state_bytes\":"<<comparedBytes<<",\"navigation_cases\":"<<navigationCases<<
        ",\"editing_cases\":"<<editingCases<<",\"directory_cases\":"<<directoryCases<<
        ",\"modal_cases\":"<<modalCases<<",\"selection_cases\":"<<selectionCases<<
        ",\"input_wait_cases\":"<<inputCases<<
        ",\"original_instructions\":"<<totalInstructions<<",\"original_code_unmodified\":true}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
