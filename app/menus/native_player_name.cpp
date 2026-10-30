#include "menus/native_player_name.h"
#include <stdexcept>

namespace scr::runtime {
namespace {
unsigned longword(const NativeMemory& m,unsigned a){
    return (unsigned(m.at(a))<<24)|(unsigned(m.at(a+1))<<16)|
        (unsigned(m.at(a+2))<<8)|m.at(a+3);
}
std::uint8_t directionalBits(std::uint16_t code){
    if(code&255)return 0;
    switch(code>>8){
    case 0x45:return 0x80;
    case 0x4c:return 0x20;
    case 0x4d:return 0x40;
    case 0x4f:return 0x10;
    case 0x4e:return 0x08;
    default:return 0;
    }
}
}
void setNativeRawKey(NativeMemory& memory,unsigned rawCode,bool pressed){
    if(rawCode>=128)throw std::out_of_range("Amiga raw key code exceeds 127");
    memory[0xead6+rawCode]=pressed?0xb3:0;
}
NativeInputScan scanNativeNameInput(NativeMemory& memory,std::uint8_t controls){
    memory[0x1b88e]=std::uint8_t(~controls);
    for(int key=127;key>=0;--key){
        if(memory[0xead6+key]!=0xb3||key==0x60||key==0x61)continue;
        memory[0xead6+key]=0;
        const bool shifted=memory[0xead6+0x60]!=0||memory[0xead6+0x61]!=0;
        const auto table=longword(memory,shifted?0x1b894:0x1b890);
        return {true,std::uint16_t((unsigned(key)<<8)|memory.at(table+unsigned(key)))};
    }
    constexpr std::uint16_t codes[]{0x4c00,0x4d00,0x4f00,0x4e00,0x440d};
    for(unsigned bit=0;bit<5;++bit)if(controls&(1u<<bit))return {true,codes[bit]};
    return {};
}
NativeTextInputUpdate applyNativeTextInputCode(NativeMemory& memory,NativeTextInputState& state,std::uint16_t code){
    NativeTextInputUpdate result{};
    if(state.complete)return result;
    memory[0x1bbd5]=state.offset;memory[0x5b83c]=state.maximumLength;
    if(memory[0x62bb5]&0x80)memory[0x5b83d]=directionalBits(code);
    const auto character=std::uint8_t(code);
    if(((memory[0x62bb5]&0x80)&&memory[0x5b83d])||character==13){
        while(static_cast<std::int8_t>(state.position)<static_cast<std::int8_t>(state.maximumLength)){
            memory.at(0x1ecab+std::uint8_t(state.offset+state.position))=' ';
            ++state.position;
        }
        state.complete=result.complete=true;
        return result;
    }
    if(character==8){
        if(state.position){--state.position;result.drewCharacter=true;result.character=127;}
        return result;
    }
    const bool allowed=(character==' '&&state.position!=0)||(character>=0x2e&&character<0x3b)||
        (character>='A'&&character<='Z')||(character>='a'&&character<='z');
    if(!allowed||static_cast<std::int8_t>(state.position)>=static_cast<std::int8_t>(state.maximumLength))return result;
    result.character=(state.offset==0xc0&&character>=0x60)?std::uint8_t(character-0x20):character;
    memory.at(0x1ecab+std::uint8_t(state.offset+state.position))=result.character;
    ++state.position;result.drewCharacter=true;
    return result;
}
NativePlayerNameUpdate NativePlayerNameEntry::begin(){
    state_={};phase_=Phase::Release;
    return {true,false,false,{}};
}
NativePlayerNameUpdate NativePlayerNameEntry::beginResponsive(unsigned length){
    if(length>12)throw std::invalid_argument("Player name exceeds original limit");
    startTextInput();state_.position=std::uint8_t(length);
    return {true,false,false,{}};
}
void NativePlayerNameEntry::startTextInput(){
    state_.offset=std::uint8_t(std::uint8_t(11-memory_[0x5eb76])<<4);
    state_.maximumLength=12;state_.position=0;state_.complete=false;
    memory_[0x5b83e]=0;memory_[0x1bbd5]=state_.offset;
    memory_[0x5b83c]=12;memory_[0x5b83d]=0;phase_=Phase::Input;
}
NativePlayerNameUpdate NativePlayerNameEntry::update(std::uint8_t controls){
    NativePlayerNameUpdate result{};
    if(phase_==Phase::Done)return result;
    const auto scan=scanNativeNameInput(memory_,controls);
    if(phase_==Phase::Release){
        if(!scan.available)startTextInput();
        return result;
    }
    if(!scan.available)return result;
    const auto change=applyNativeTextInputCode(memory_,state_,scan.code);
    if(change.drewCharacter)result.characters.push_back(change.character);
    if(change.complete){
        // Original examines byte one of the leading-space name record. It
        // retries an empty name rather than substituting an invented default.
        const auto offset=unsigned(std::uint8_t(11-memory_[0x5eb76]))*16;
        if(memory_.at(0x1ecab+offset)==' '){
            state_={};phase_=Phase::Release;
            result.beganAttempt=result.retriedBlank=true;
        }else{phase_=Phase::Done;result.complete=true;}
    }
    return result;
}
} // namespace scr::runtime
