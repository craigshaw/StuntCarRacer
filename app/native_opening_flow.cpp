#include "native_opening_flow.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scr::viewer {
namespace {
constexpr unsigned selected=0x1bbd6,maximum=0x1bb8d,highlight=0x1bbca;
void saveRandom(NativeMemory& m){
    // Original0x6368c writes the byte into the saved long's second byte.
    std::copy_n(m.begin()+0x625a2,4,m.begin()+0x636b8);m[0x636b9]=m[0x625a6];
}
}
void initializeNativeOpeningSession(NativeMemory& m){
    // Original0x5c890..0x5c8e0 and0x5cf46..0x5cf7e. The media checksum and
    // initializeGameData have already been ported in native startup.
    m[0x23c32]=m[0x23c33]=0;
    std::copy_n(m.begin()+0x1ecaa,256,m.begin()+0x7a91a);
    for(unsigned i=0;i<256;++i)m[0x7a61a+i]=m[0x7a71a+i]=m[0x5cf30+(i&15)];
    std::copy_n(m.begin()+0x7a91a,256,m.begin()+0x1ecaa);
    m[0x57c3c]=0;m[0x57c62]=0x40;
    if(!m[0x60fbc])m[0x60fbc]=0x80;
    // displayMenuScreen's persistent display state, without hardware registers.
    m[0x1bbbc]=0x80;m[0x5d724]=0x41;m[0x64530]=0;
}
void prepareNativeOpeningLocalStandings(NativeMemory& m){
    const unsigned additional=m[0x5eb76];
    if(!additional||additional>7||m[0x57c3c])
        throw std::invalid_argument("Opening standings require 2..8 local players");
    const unsigned begin=11-additional,end=12;
    m[0x1bc5b]=std::uint8_t(begin);m[0x1bc59]=std::uint8_t(end);
    m[0x1bbe8]=std::uint8_t(begin);
    // Original0x5acfe. Local ties are ordered by descending driver ID. The
    // single-player branch uses random draws and must not be substituted here.
    for(unsigned i=begin;i<end;++i){
        const auto id=m[0x1c9c2+i];
        if(id>=12)throw std::invalid_argument("Opening standings contain an invalid driver");
        m[0x1ca0e+i]=id;
        m[0x1ca02+id]=std::uint8_t(2*m[0x1c9de+id]+m[0x1c9ea+id]);
    }
    do{
        m[0x1bb19]=0;
        for(unsigned i=begin;i+1<end;++i){
            m[0x1bb18]=std::uint8_t(i);
            const auto first=m[0x1ca0e+i],second=m[0x1ca0e+i+1];
            const auto firstScore=std::int8_t(m[0x1ca02+first]),secondScore=std::int8_t(m[0x1ca02+second]);
            bool swap=firstScore<secondScore;
            if(firstScore==secondScore){
                const auto firstWins=std::int8_t(m[0x1c9de+first]),secondWins=std::int8_t(m[0x1c9de+second]);
                swap=firstWins<secondWins||(firstWins==secondWins&&first<second);
            }
            if(swap){
                m[0x1bb1a]=second;m[0x1ca0e+i]=second;m[0x1ca0e+i+1]=first;++m[0x1bb19];
            }
        }
    }while(m[0x1bb19]);
}

NativeOpeningFlow::NativeOpeningFlow(NativeMemory& memory):memory_(memory){returnToMode();}
void NativeOpeningFlow::delay(unsigned units){
    remaining_=nativeMenuDelaySeconds(units,memory_[0x1bb1b]);memory_[0x1bb1a]=20;
}
void NativeOpeningFlow::openMenu(NativeOpeningPage page,unsigned offset,unsigned finalRow){
    const int remembered=page==NativeOpeningPage::Mode?modeSelection_:addSelection_;
    page_=page;memory_[0x1bb8b]=std::uint8_t(offset);
    // Original displayMenu takes the last row, decrements it, and still draws
    // maxMenuIndex+2 rows. NativeMenuRenderState::count means final row.
    memory_[maximum]=std::uint8_t(finalRow-1);memory_[selected]=memory_[highlight]=0;
    if(navigation_==NativeMenuNavigation::Responsive&&remembered>=0)memory_[selected]=std::uint8_t(remembered);
    phase_=Phase::PaintDelay;delay(15);
}
void NativeOpeningFlow::returnToMode(){
    multiplayer_=false;
    memory_[0x5eb75]=0x80;memory_[0x5eb76]=0;
    openMenu(NativeOpeningPage::Mode,0x10,2);
}
NativeMenuRenderState NativeOpeningFlow::renderState()const{
    NativeMenuRenderState state;
    state.menuOffset=memory_[0x1bb8b];state.count=unsigned(memory_[maximum])+1;
    state.selected=memory_[selected];state.division=memory_[0x1c9ce];
    state.selectedDivision=memory_[0x5eb7c];state.menuStrings=true;state.confirmed=memory_[highlight]!=0;
    return state;
}
NativeOpeningAction NativeOpeningFlow::showRoster(){
    if(navigation_==NativeMenuNavigation::Responsive)setupCheckpoint_=std::make_unique<NativeMemory>(memory_);
    memory_[0x5eb75]=0;
    if(memory_[0x5eb76]){
        prepareNativeOpeningLocalStandings(memory_);page_=NativeOpeningPage::LocalStandings;
    }else page_=NativeOpeningPage::Roster;
    fire_.reset();return NativeOpeningAction::ShowRoster;
}
NativeOpeningAction NativeOpeningFlow::nameCompleted(){
    if(page_!=NativeOpeningPage::Name)throw std::logic_error("Opening has no active name entry");
    completedPlayers_=std::max(completedPlayers_,unsigned(memory_[0x5eb76])+1);
    if(!multiplayer_)return showRoster();
    if(memory_[0x5eb76]>=7)return showRoster();
    openMenu(NativeOpeningPage::AddPlayer,0x14,1);return NativeOpeningAction::Changed;
}
NativeOpeningAction NativeOpeningFlow::confirm(){
    const auto choice=memory_[selected];
    if(page_==NativeOpeningPage::Mode){
        modeSelection_=choice;completedPlayers_=0;
        if(choice>=2){page_=NativeOpeningPage::ComputerLinkUnavailable;return NativeOpeningAction::UnsupportedComputerLink;}
        // Private native choice bit, retained in the object rather than game
        // RAM: single mode and multiplayer's first name both have count zero.
        multiplayer_=choice==1;
        page_=NativeOpeningPage::Name;return NativeOpeningAction::BeginName;
    }
    addSelection_=choice;
    if(choice==0){++memory_[0x5eb76];page_=NativeOpeningPage::Name;return NativeOpeningAction::BeginName;}
    if(!memory_[0x5eb76]){
        openMenu(NativeOpeningPage::AddPlayer,0x14,1);return NativeOpeningAction::Changed;
    }
    return showRoster();
}
NativeOpeningAction NativeOpeningFlow::update(const NativeMenuInput& input,double elapsed){
    if(!std::isfinite(elapsed)||elapsed<0)throw std::invalid_argument("Invalid opening elapsed time");
    const bool responsive=navigation_==NativeMenuNavigation::Responsive;
    if(responsive&&input.back)return back();
    const auto controls=std::uint8_t(input.controls&31);
    if(page_==NativeOpeningPage::Complete||page_==NativeOpeningPage::Name||
            page_==NativeOpeningPage::ComputerLinkUnavailable)return NativeOpeningAction::None;
    if(page_==NativeOpeningPage::Roster||page_==NativeOpeningPage::LocalStandings){
        memory_[0x1bb47]=controls;
        if(responsive?!(controls&16):!fire_.update(controls,elapsed))return NativeOpeningAction::None;
        memory_[0x1bb6e]=0;saveRandom(memory_);memory_[0x57c64]=0xf3;
        page_=NativeOpeningPage::Complete;return NativeOpeningAction::Complete;
    }
    if(phase_==Phase::PaintDelay||phase_==Phase::ReleaseDelay||phase_==Phase::ConfirmDelay){
        remaining_=responsive?0:remaining_-elapsed;if(remaining_>0)return NativeOpeningAction::None;
        memory_[0x1bb1a]=memory_[0x1bb1b]=0;
        if(phase_==Phase::ConfirmDelay)return confirm();
        phase_=phase_==Phase::PaintDelay&&!responsive?Phase::Release:Phase::Input;
    }
    if(phase_==Phase::Release){
        if(responsive)phase_=Phase::Input;
        else{
        memory_[0x1bb47]=controls;
        if(!controls){phase_=Phase::ReleaseDelay;delay(2);}
        return NativeOpeningAction::None;
        }
    }
    if(!applyNativeMenuInput(memory_,input))return NativeOpeningAction::None;
    if(responsive){
        if(page_==NativeOpeningPage::Mode)modeSelection_=memory_[selected];else addSelection_=memory_[selected];
        if(memory_[highlight])return confirm();
        phase_=Phase::Input;return NativeOpeningAction::Changed;
    }
    delay(15);phase_=memory_[highlight]?Phase::ConfirmDelay:Phase::PaintDelay;
    return NativeOpeningAction::Changed;
}
NativeOpeningAction NativeOpeningFlow::back(){
    if(navigation_!=NativeMenuNavigation::Responsive)return NativeOpeningAction::None;
    if(page_==NativeOpeningPage::Roster||page_==NativeOpeningPage::LocalStandings){
        if(setupCheckpoint_)memory_=*setupCheckpoint_;
        if(!multiplayer_||memory_[0x5eb76]==7){page_=NativeOpeningPage::Name;return NativeOpeningAction::BeginName;}
        openMenu(NativeOpeningPage::AddPlayer,0x14,1);return NativeOpeningAction::Changed;
    }
    if(page_==NativeOpeningPage::AddPlayer){
        addSelection_=memory_[selected];page_=NativeOpeningPage::Name;return NativeOpeningAction::BeginName;
    }
    if(page_==NativeOpeningPage::Name){
        if(!memory_[0x5eb76]){returnToMode();return NativeOpeningAction::Changed;}
        if(unsigned(memory_[0x5eb76])>=completedPlayers_)--memory_[0x5eb76];
        openMenu(NativeOpeningPage::AddPlayer,0x14,1);return NativeOpeningAction::Changed;
    }
    return NativeOpeningAction::None;
}
} // namespace scr::viewer
