#include "native_save_menu.h"
#include "native_cockpit_instruments.h"
#include "native_menu_flow.h"
#include "planar_image.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scr::viewer {
namespace {
constexpr unsigned screen=0x6a594;
void word(NativeMemory& m,unsigned a,unsigned v){m.at(a)=std::uint8_t(v>>8);m.at(a+1)=std::uint8_t(v);}
void longword(NativeMemory& m,unsigned a,unsigned v){word(m,a,v>>16);word(m,a+2,v);}
unsigned longword(const NativeMemory& m,unsigned a){return (unsigned(m.at(a))<<24)|(unsigned(m.at(a+1))<<16)|(unsigned(m.at(a+2))<<8)|m.at(a+3);}
std::uint8_t direction(std::uint16_t code){
    if(code&255)return 0;
    switch(code>>8){case 0x45:return 128;case 0x4c:return 32;case 0x4d:return 64;case 0x4f:return 16;case 0x4e:return 8;default:return 0;}
}
}
std::uint8_t nativeSaveNameKind(const std::array<std::uint8_t,8>& name)noexcept{
    if(name[0]=='H'&&name[1]=='A'&&name[2]=='L'&&name[3]=='L')return 0x40;
    if(name[0]=='M'&&name[1]=='P')return 1;
    return 0;
}
void commitNativeSaveDirectory(NativeMemory& m,unsigned slot,const std::array<std::uint8_t,8>& name,bool write){
    if(slot>=30)throw std::invalid_argument("Native save slot outside original range");
    std::copy(name.begin(),name.end(),m.begin()+0x7a21a+16*slot);m[0x7a21a+16*slot+15]=0;
    if(write)m[0x62bb7]=std::uint8_t((slot+1)%30);else m[0x62bb6]=std::uint8_t(slot);
    longword(m,0x7a234,0x47826653);m[0x7a224]=m[0x62bb6];m[0x7a225]=m[0x62bb7];
}
NativeSaveMenu::NativeSaveMenu(NativeMemory& memory,bool write,const NativeMenuImage* previousImage)
    :memory_(memory),drawing_(std::make_unique<NativeMemory>(memory)),write_(write){
    memory_[0x1ca31]=write?1:0;memory_[0x1bb92]=0;
    auto& d=*drawing_;d[0x5d724]=0x41;d[0x1bbbc]=128;d[0x5924a]=0;
    d[0x59843]=d[0x59844]=d[0x1bb6d]=d[0x1bb6e]=0;
    longword(d,0x6a584,screen);longword(d,0x6a588,screen);longword(d,0x6a58c,screen);
    const auto background=previousImage?*previousImage:makeNativeMenuBackground(memory_);
    std::fill_n(d.begin()+screen,32000,0);
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x)for(unsigned p=0;p<4;++p)
        if(background.indices[y*320+x]&(1u<<p))d[screen+p*8000+y*40+x/8]|=std::uint8_t(128u>>(x%8));
    frame();colouredMessage(0x12a);
}
void NativeSaveMenu::frame(){
    auto& d=*drawing_;
    auto rectangle=[&](unsigned left,unsigned top,unsigned width,unsigned height){
        for(unsigned y=top;y<top+height;++y)for(unsigned x=left;x<left+width;++x)for(unsigned p=0;p<4;++p){
            auto& value=d[screen+p*8000+y*40+x/8];const unsigned bit=128u>>(x%8);
            value=std::uint8_t((value&~bit)|(p<2?bit:0));
        }
    };
    rectangle(64,65,192,2);rectangle(32,67,224,125);
    ink(1);paper(3);d[0x1bbd3]=128;
}
void NativeSaveMenu::glyph(unsigned value){drawNativeCockpitCharacter(*drawing_,value);}
void NativeSaveMenu::paper(unsigned index){for(unsigned p=0;p<4;++p)word(*drawing_,0x5982e+p*2,(index&(1u<<p))?65535:0);}
void NativeSaveMenu::ink(unsigned index){for(unsigned p=0;p<4;++p)word(*drawing_,0x59836+p*2,(index&(1u<<p))?65535:0);}
void NativeSaveMenu::position(unsigned x,unsigned y){glyph(31);glyph(x);glyph(y);}
void NativeSaveMenu::message(unsigned offset){
    for(unsigned n=0;n<1024;++n){const auto value=(*drawing_).at(0x625ce+offset+n);if(value==255)return;glyph(value);}
    throw std::invalid_argument("Unterminated native save menu message");
}
void NativeSaveMenu::colouredMessage(unsigned offset){
    auto& d=*drawing_;const auto x=d[0x5983e],y=d[0x5983f],transparent=d[0x1bbd3];
    d[0x1bbd3]=0;ink(1);paper(3);message(0xd0);message(offset);
    d[0x5983e]=x;d[0x5983f]=y;d[0x1bbd3]=transparent;
}
void NativeSaveMenu::displaySlot(bool edited){
    auto& d=*drawing_;const unsigned selected=memory_[0x62bb4],shown=selected<30?selected:0;
    position(6+(shown/10)*10,11+shown%10);memory_[0x5b83e]=0;
    for(unsigned i=0;i<8;++i){
        unsigned c;
        if(edited)c=memory_[0x1ed6b+i];
        else{
            const auto a=0x7a21a+16*selected;
            if(memory_.at(a+15)||memory_[0x62bbb]||std::int8_t(memory_.at(a+i))<=32){
                c='_';memory_[0x1ed6b+i]=' ';ink(memory_[0x62bb9]);
            }else{c=memory_[a+i];memory_[0x1ed6b+i]=std::uint8_t(c);ink(memory_[0x62bb8]);++memory_[0x5b83e];}
        }
        glyph(c);
    }
    d[0x62bbc]=edited?128:0;
}
void NativeSaveMenu::allSlots(){
    (*drawing_)[0x1bbd3]=0;paper(3);memory_[0x62bb8]=15;memory_[0x62bb9]=11;
    for(unsigned slot=0;slot<30;++slot){memory_[0x62bb4]=std::uint8_t(slot);displaySlot();}
    (*drawing_)[0x1bbd3]=128;memory_[0x62bb4]=30;
}
void NativeSaveMenu::selectSlot(){
    auto& d=*drawing_;memory_[0x62bb4]=memory_[write_?0x62bb7:0x62bb6];
    memory_[0x62bb8]=memory_[0x62bb9]=0;paper(2);displaySlot();d[0x5983e]=std::uint8_t(d[0x5983e]-9);
    ink(0);glyph('>');memory_[0x62bba]=d[0x5983e];
    phase_=navigation_==NativeMenuNavigation::Responsive?(write_?NativeSaveMenuPhase::SaveSelection:NativeSaveMenuPhase::LoadSelection):NativeSaveMenuPhase::DirectionRelease;
}
void NativeSaveMenu::refreshSlot(){
    auto& d=*drawing_;paper(3);d[0x5983e]=std::uint8_t(memory_[0x62bba]-1);glyph(' ');
    memory_[0x62bb8]=15;memory_[0x62bb9]=11;displaySlot();d[0x5983e]=std::uint8_t(d[0x5983e]-9);
}
void NativeSaveMenu::startText(){
    ink(0);paper(2);text_={0xc0,8,memory_[0x5b83e],false};
    (*drawing_)[0x5983e]=std::uint8_t((*drawing_)[0x5983e]+text_.position);
    memory_[0x1bbd5]=0xc0;memory_[0x5b83c]=8;memory_[0x5b83d]=0;phase_=NativeSaveMenuPhase::SaveName;
}
NativeSaveMenuResult NativeSaveMenu::directoryLoaded(){
    if(phase_!=NativeSaveMenuPhase::Directory)throw std::logic_error("Native save directory was not requested");
    directoryReady_=true;
    const bool valid=longword(memory_,0x7a234)==0x47826653;
    memory_[0x62bbb]=valid?0:128;memory_[0x1bb92]=0;
    if(!valid&&write_){
        colouredMessage(0xf0);
        if(!invalidReads_++){phase_=NativeSaveMenuPhase::FormatWarning;release_=true;return result(NativeSaveMenuAction::Changed);}
    }
    memory_[0x62bb6]=valid?memory_[0x7a224]:0;memory_[0x62bb7]=valid?memory_[0x7a225]:0;
    allSlots();(*drawing_)[0x1bbd3]=0;memory_[0x62bb5]=128;selectSlot();return result(NativeSaveMenuAction::Changed);
}
void NativeSaveMenu::navigate(std::uint8_t bits){
    refreshSlot();int value=memory_[0x62bb4];
    if(bits&64)value=(value+1)%30;else if(bits&32)value=(value+29)%30;
    else if(bits&8){if(value+10<30)value+=10;}else if(value>=10)value-=10;
    memory_[write_?0x62bb7:0x62bb6]=std::uint8_t(value);selectSlot();
}
NativeSaveMenuResult NativeSaveMenu::selected(){
    auto r=result(NativeSaveMenuAction::Select);memory_[0x1bb6b]=r.kind;
    if(write_&&((r.kind==1&&!memory_[0x5eb76])||(r.kind==0&&memory_[0x5eb76]))){
        (*drawing_)[0x1bb6e]=4;position(6,22);
        for(unsigned n=0;n<128;++n){const auto c=memory_[0x5f641+0x57+n];if(c==255)break;glyph(c);}
        phase_=NativeSaveMenuPhase::Incompatible;release_=true;return result(NativeSaveMenuAction::Changed);
    }
    paper(2);ink(1);displaySlot(true);phase_=NativeSaveMenuPhase::Complete;pending_=NativeSaveMenuAction::Select;return r;
}
NativeSaveMenuResult NativeSaveMenu::result(NativeSaveMenuAction action)const{
    NativeSaveMenuResult r;r.action=action;r.slot=memory_[0x62bb4];
    std::copy_n(memory_.begin()+0x1ed6b,8,r.name.begin());r.kind=nativeSaveNameKind(r.name);return r;
}
void NativeSaveMenu::operationFailed(std::uint8_t error){
    memory_[0x1bb92]=0;colouredMessage(error==0x22?0x2c:error==0x1c?0x3c:0xd4);
    phase_=NativeSaveMenuPhase::Retry;release_=false;
}
void NativeSaveMenu::invalidSaveData(){
    colouredMessage(0x68);phase_=NativeSaveMenuPhase::InvalidData;release_=true;
}
void NativeSaveMenu::finish(){memory_[0x62bb5]=0;paper(3);phase_=NativeSaveMenuPhase::Complete;}
void NativeSaveMenu::complete(){finish();}
NativeSaveMenuResult NativeSaveMenu::update(std::uint8_t controls,double elapsed){
    if(!std::isfinite(elapsed)||elapsed<0)throw std::invalid_argument("Invalid native save elapsed time");
    if(navigation_==NativeMenuNavigation::Responsive)return updateResponsive(controls);
    controls&=31;
    if(phase_==NativeSaveMenuPhase::Complete||phase_==NativeSaveMenuPhase::Directory)return {};
    if(phase_==NativeSaveMenuPhase::RetryDelay){
        remaining_-=elapsed;if(remaining_>0)return {};
        memory_[0x1bb1a]=memory_[0x1bb1b]=0;
        if(pending_==NativeSaveMenuAction::ReadDirectory)phase_=NativeSaveMenuPhase::Directory;
        else phase_=NativeSaveMenuPhase::Complete;
        return result(pending_);
    }
    if(phase_==NativeSaveMenuPhase::DirectionRelease){
        if(controls&15)return {};
        for(unsigned raw:{0x4cu,0x4du,0x4eu,0x4fu})if(memory_[0xead6+raw]==0xb3)return {};
        if(write_)startText();else phase_=NativeSaveMenuPhase::LoadSelection;
        return {};
    }
    const auto scan=scanNativeNameInput(memory_,controls);
    if(phase_==NativeSaveMenuPhase::InsertDisc||phase_==NativeSaveMenuPhase::FormatWarning||
        phase_==NativeSaveMenuPhase::Incompatible||phase_==NativeSaveMenuPhase::InvalidData){
        if(release_){if(!scan.available)release_=false;return {};}
        if(!scan.available)return {};
        if(phase_==NativeSaveMenuPhase::InvalidData){finish();return result(NativeSaveMenuAction::Cancel);}
        if(phase_==NativeSaveMenuPhase::FormatWarning&&scan.code==0x4500){memory_[0x1bb92]=128;finish();return result(NativeSaveMenuAction::Cancel);}
        if(phase_==NativeSaveMenuPhase::Incompatible){
            for(unsigned i=0;i<25;++i)glyph(127);(*drawing_)[0x1bb6e]=0;selectSlot();return result(NativeSaveMenuAction::Changed);
        }
        const bool format=phase_==NativeSaveMenuPhase::FormatWarning;
        frame();if(format)(*drawing_)[0x1bb6e]=1;else ink(15);
        message(write_?0x16:0);(*drawing_)[0x1bb6e]=0;phase_=NativeSaveMenuPhase::Directory;
        pending_=NativeSaveMenuAction::ReadDirectory;return result(pending_);
    }
    if(!scan.available)return {};
    if(phase_==NativeSaveMenuPhase::Retry){
        if((scan.code>>8)==0x45){memory_[0x1bb92]=128;finish();return result(NativeSaveMenuAction::Cancel);}
        paper(3);colouredMessage(0x91);remaining_=nativeMenuDelaySeconds(50,memory_[0x1bb1b]);
        memory_[0x1bb1a]=20;
        phase_=NativeSaveMenuPhase::RetryDelay;return result(NativeSaveMenuAction::Changed);
    }
    if(phase_==NativeSaveMenuPhase::LoadSelection){
        const auto bits=direction(scan.code);memory_[0x5b83d]=bits;
        if(bits&128){memory_[0x1bb92]=128;finish();return result(NativeSaveMenuAction::Cancel);}
        if(bits){navigate(bits);return result(NativeSaveMenuAction::Changed);}
        if(std::uint8_t(scan.code)==13&&memory_[0x1ed6b]!=' ')return selected();
        return {};
    }
    if(phase_==NativeSaveMenuPhase::SaveName){
        const auto change=applyNativeTextInputCode(memory_,text_,scan.code);
        if(change.drewCharacter)glyph(change.character);
        if(!change.complete)return result(change.drewCharacter?NativeSaveMenuAction::Changed:NativeSaveMenuAction::None);
        colouredMessage(0x91);const auto bits=memory_[0x5b83d];
        if(bits&128){memory_[0x1bb92]=128;finish();return result(NativeSaveMenuAction::Cancel);}
        if(bits){navigate(bits);return result(NativeSaveMenuAction::Changed);}
        if(memory_[0x1ed6b]!=' ')return selected();
        colouredMessage(0x7f);memory_[0x5b83e]=0;startText();return result(NativeSaveMenuAction::Changed);
    }
    return {};
}
void NativeSaveMenu::setNavigation(NativeMenuNavigation mode){
    if(mode==navigation_)return;
    navigation_=mode;
    if(mode==NativeMenuNavigation::Responsive&&phase_==NativeSaveMenuPhase::DirectionRelease)
        phase_=write_?NativeSaveMenuPhase::SaveSelection:NativeSaveMenuPhase::LoadSelection;
    else if(mode==NativeMenuNavigation::Original&&phase_==NativeSaveMenuPhase::SaveSelection)startText();
}
void NativeSaveMenu::returnToSlots(){
    const auto slot=memory_[0x62bb4];
    frame();message(write_?0x16:0);allSlots();
    memory_[write_?0x62bb7:0x62bb6]=slot;selectSlot();
}
NativeSaveMenuResult NativeSaveMenu::back(){
    if(phase_==NativeSaveMenuPhase::SaveName){refreshSlot();selectSlot();return result(NativeSaveMenuAction::Changed);}
    if(directoryReady_&&(phase_==NativeSaveMenuPhase::Incompatible||phase_==NativeSaveMenuPhase::InvalidData||phase_==NativeSaveMenuPhase::Retry)){
        returnToSlots();return result(NativeSaveMenuAction::Changed);
    }
    memory_[0x1bb92]=128;finish();return result(NativeSaveMenuAction::Cancel);
}
NativeSaveMenuResult NativeSaveMenu::updateResponsive(std::uint8_t controls){
    if(phase_==NativeSaveMenuPhase::Complete||phase_==NativeSaveMenuPhase::Directory)return {};
    const auto scan=scanNativeNameInput(memory_,controls&31);
    if(!scan.available)return {};
    if(scan.code==0x4500)return back();
    if(phase_==NativeSaveMenuPhase::InsertDisc||phase_==NativeSaveMenuPhase::FormatWarning||
            phase_==NativeSaveMenuPhase::RetryDelay||(phase_==NativeSaveMenuPhase::Retry&&!directoryReady_)){
        frame();message(write_?0x16:0);phase_=NativeSaveMenuPhase::Directory;return result(NativeSaveMenuAction::ReadDirectory);
    }
    if(phase_==NativeSaveMenuPhase::Incompatible||phase_==NativeSaveMenuPhase::InvalidData){returnToSlots();return result(NativeSaveMenuAction::Changed);}
    if(phase_==NativeSaveMenuPhase::Retry)return selected();
    if(phase_==NativeSaveMenuPhase::LoadSelection||phase_==NativeSaveMenuPhase::SaveSelection||phase_==NativeSaveMenuPhase::DirectionRelease){
        const auto bits=direction(scan.code);
        if(bits){navigate(bits);return result(NativeSaveMenuAction::Changed);}
        if(std::uint8_t(scan.code)==13){
            if(write_){startText();return result(NativeSaveMenuAction::Changed);}
            if(memory_[0x1ed6b]!=' ')return selected();
        }
        return {};
    }
    if(phase_==NativeSaveMenuPhase::SaveName){
        const auto change=applyNativeTextInputCode(memory_,text_,scan.code);
        if(change.drewCharacter)glyph(change.character);
        if(!change.complete)return result(change.drewCharacter?NativeSaveMenuAction::Changed:NativeSaveMenuAction::None);
        if(const auto bits=memory_[0x5b83d]){navigate(bits);return result(NativeSaveMenuAction::Changed);}
        if(memory_[0x1ed6b]!=' ')return selected();
        colouredMessage(0x7f);memory_[0x5b83e]=0;startText();return result(NativeSaveMenuAction::Changed);
    }
    return {};
}
NativeMenuImage NativeSaveMenu::image()const{
    const auto& d=*drawing_;NativeMenuImage image;
    for(unsigned i=0;i<16;++i)image.palette[i]=originalPaletteRgb(std::uint16_t((unsigned(d[0x23c32+i*2])<<8)|d[0x23c33+i*2]));
    for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x)for(unsigned p=0;p<4;++p)
        if(d[screen+p*8000+y*40+x/8]&(128u>>(x%8)))image.indices[y*320+x]|=std::uint8_t(1u<<p);
    return image;
}
} // namespace scr::viewer
