#include "native_menu_flow.h"
#include "native_race_initialization.h"
#include <algorithm>
#include <stdexcept>

namespace scr::viewer {
namespace {
constexpr unsigned selection=0x1bbd6,maxIndex=0x1bb8d,highlight=0x1bbca;
void copyBytes(NativeMemory& m,unsigned source,unsigned target,unsigned count){
    std::copy_n(m.begin()+source,count,m.begin()+target);
}
} // namespace

void initializeNativeMenuSession(NativeMemory& m){
    // Original 0x5c890..0x5c8e0. D1 and D2 wrap as bytes; the reversed loops
    // are equivalent to a 16-byte template repeated over each 256-byte table.
    m[0x23c32]=m[0x23c33]=0;
    copyBytes(m,0x1ecaa,0x7a91a,256);
    for(unsigned i=0;i<256;++i)m[0x7a61a+i]=m[0x7a71a+i]=m[0x5cf30+(i&15)];
    // We enter the established local main-menu session, after original mode /
    // driver entry. Keep the supplied local-player slot and its name unchanged.
    m[0x57c3c]=0;m[0x5eb76]=0;m[0x5eb75]=0;m[0x60fbc]=0x80;
    // Original saveRandomState at0x6368c has an overlapping byte destination.
    copyBytes(m,0x625a2,0x636b8,4);m[0x636b9]=m[0x625a6];
}

bool applyNativeMenuInput(NativeMemory& m,const NativeMenuInput& input){
    const auto controls=std::uint8_t(input.controls&0x1f);
    m[0x1bb47]=controls;m[highlight]=controls&0x10;
    if(m[highlight])return true;
    // Raw original table at0x60c8d is used at0x5b9d2. The enhanced upstream
    // equate points elsewhere; that provisional label is not used here.
    const unsigned last=unsigned(m[maxIndex])+1;
    if(last>3)throw std::invalid_argument("Unsupported original menu row count");
    for(unsigned row=last+1;row>0;){
        --row;const unsigned key=m[0x60c8d+row];
        if(key>=1&&key<=4&&(input.numberedKeys&(1u<<(key-1)))){
            m[selection]=std::uint8_t(row);return true;
        }
    }
    if(!(controls&3))return false;
    unsigned selected=m[selection];
    if(controls&1){if(selected) --selected;}
    else if(selected<=m[maxIndex])++selected;
    m[selection]=std::uint8_t(selected);return true;
}

double nativeMenuDelaySeconds(unsigned units,unsigned initialCounter){
    if(!units)return 0;
    // 0x5ba42: 20 inner passes, each 256 SUBQ.B absolute-long / BNE.W
    // iterations. 68000 timing is 20+10 clocks (last branch12); the outer
    // byte SUBQ/BNE plus reload overhead contribute the remaining clocks.
    // Measured by executing the original, independently checked in the oracle.
    const unsigned firstCounter=initialCounter&255;
    const unsigned firstPassReduction=firstCounter?(256-firstCounter)*30:0;
    return (double(units)*154280.0+18.0-firstPassReduction)/7093790.0;
}

bool NativeFireWait::update(std::uint8_t controls,double elapsed){
    if(elapsed<0)throw std::invalid_argument("Negative fire-wait elapsed time");
    const bool fire=(controls&16)!=0;
    if(phase_==Phase::Done)return true;
    if(phase_==Phase::Release){
        if(!fire){phase_=Phase::Delay;remaining_=nativeMenuDelaySeconds(5);}
        return false;
    }
    if(phase_==Phase::Delay){
        remaining_-=elapsed;if(remaining_>0)return false;
        phase_=Phase::Press;
    }
    if(fire){phase_=Phase::Done;return true;}
    return false;
}

NativeMenuFlow::NativeMenuFlow(NativeMemory& memory):memory_(memory){openMain();}

void NativeMenuFlow::startDelay(unsigned units,bool repaint){
    // Original renderTrackName leaves its 15-character loop counter here.
    // Its final Cancel row preserves that value. Other screens do not change
    // this counter, and completion of each delay clears it again.
    if(repaint&&page_==NativeMenuPage::Track)memory_[0x1bb1b]=15;
    remaining_=nativeMenuDelaySeconds(units,memory_[0x1bb1b]);memory_[0x1bb1a]=20;
}
void NativeMenuFlow::openPage(NativeMenuPage page,unsigned offset,unsigned count,unsigned selected,unsigned textMode){
    page_=page;memory_[0x1bb8b]=std::uint8_t(offset);memory_[maxIndex]=std::uint8_t(count-1);
    memory_[selection]=std::uint8_t(selected);memory_[highlight]=0;memory_[0x5eb75]=std::uint8_t(textMode);
    phase_=Phase::PaintDelay;startDelay(15,true);
}
void NativeMenuFlow::openMain(){
    // Original menuSystemLoop and handleMainMenu,0x5cfb8 /0x5baea.
    memory_[0x1bb5b]&=0x7f;memory_[0x57c62]=0x10;memory_[0x5b83d]=0;
    openPage(NativeMenuPage::Main,0,3,(memory_[0x1ca22]&0x80)?2:1,0);
    competitive_=false;
}
void NativeMenuFlow::openPractice(){
    const bool skipDivision=(memory_[0x5eb7d]&1)!=0;
    memory_[0x5eb7d]&=0xfe;
    if(skipDivision)openPage(NativeMenuPage::Track,0x1c,2,memory_[0x1ca22]&1,0x40);
    else openPage(NativeMenuPage::Division,0x18,3,(memory_[0x1c9ce]^3)&3,0x80);
}
NativeMenuRenderState NativeMenuFlow::renderState()const{
    NativeMenuRenderState state;
    state.menuOffset=memory_[0x1bb8b];state.count=unsigned(memory_[maxIndex])+1;
    state.selected=memory_[selection];state.division=memory_[0x1c9ce];state.selectedDivision=memory_[0x5eb7c];
    state.menuStrings=(memory_[0x5eb75]&0x80)!=0;state.confirmed=memory_[highlight]!=0;
    return state;
}
NativeMenuAction NativeMenuFlow::confirm(){
    const unsigned selected=memory_[selection];
    if(page_==NativeMenuPage::Main){
        if(selected==1){openPractice();return NativeMenuAction::Changed;}
        if(selected==2){
            if(memory_[0x5eb76]){
                unsupported_=selected;memory_[highlight]=0;
                phase_=Phase::PaintDelay;startDelay(15,true);
                return NativeMenuAction::UnsupportedOption;
            }
            memory_[0x1ca22]=0x80;memory_[0x5eb7d]=0;competitive_=true;
            competitionDivision_=memory_[0x1c9ce];
            competitionContext_=memory_[0x1c9d0];
            initializeNativeSeason(memory_);return openNextSeasonRace();
        }
        unsupported_=selected;memory_[highlight]=0;
        phase_=Phase::PaintDelay;startDelay(15,true);
        return NativeMenuAction::UnsupportedOption;
    }
    if(page_==NativeMenuPage::Division){
        memory_[0x5eb7c]=std::uint8_t(selected^3);
        openPage(NativeMenuPage::Track,0x1c,2,memory_[0x1ca22]&1,0x40);
        return NativeMenuAction::Changed;
    }
    if(page_==NativeMenuPage::Track){
        memory_[0x5eb75]=0;
        if(selected>=2){openMain();return NativeMenuAction::Changed;}
        memory_[0x1ca22]=std::uint8_t(selected);memory_[0x5eb7d]=1;
        const unsigned ordinal=unsigned(memory_[0x5eb7c])*2+selected;
        if(ordinal>=8)throw std::runtime_error("Invalid original Practice division");
        memory_[0x1ca33]=memory_[0x648c2+ordinal];
        memory_[0x1ca21]=memory_[0x648ca+(ordinal^(memory_[0x1c9d0]?1u:0u))];
        phase_=Phase::TrackDelay;startDelay(20);
        return NativeMenuAction::None;
    }
    return NativeMenuAction::None;
}

NativeMenuAction NativeMenuFlow::update(const NativeMenuInput& input,double elapsed){
    if(elapsed<0)throw std::invalid_argument("Negative menu elapsed time");
    const auto controls=std::uint8_t(input.controls&0x1f);
    if(page_==NativeMenuPage::Race)return NativeMenuAction::None;
    if(page_==NativeMenuPage::Matchup||page_==NativeMenuPage::Results||
            page_==NativeMenuPage::RoundResults||page_==NativeMenuPage::Standings||page_==NativeMenuPage::SeasonEnd||
            page_==NativeMenuPage::SeasonAward||page_==NativeMenuPage::Roster||page_==NativeMenuPage::Records){
        memory_[0x1bb47]=controls;
        if(!competitionFire_.update(controls,elapsed))return NativeMenuAction::None;
        if(page_==NativeMenuPage::Matchup){
            // Preserve the original normal single-player race-start flag
            // after the matchup's released/new-fire wait.
            memory_[0x57c68]=0x80;
            page_=NativeMenuPage::Preview;memory_[0x57c62]=0x12;
            previewReleaseMask_=0x1f;memory_[0x5d9da]=0x1f;
            return NativeMenuAction::EnterPreview;
        }
        if(page_==NativeMenuPage::Results){
            if(memory_[0x1bbb1]){openCompetitionPage(NativeMenuPage::Records,1);return NativeMenuAction::ShowCompetitionPage;}
        }
        if(page_==NativeMenuPage::Results||page_==NativeMenuPage::Records){
            const bool fromBitmap=page_==NativeMenuPage::Results;
            memory_[0x1bbb1]=0;
            openCompetitionPage(NativeMenuPage::RoundResults,2);
            return fromBitmap?NativeMenuAction::ShowCompetitionPage:NativeMenuAction::Changed;
        }
        if(page_==NativeMenuPage::RoundResults){
            openCompetitionPage(NativeMenuPage::Standings,3);return NativeMenuAction::Changed;
        }
        if(page_==NativeMenuPage::Standings){
            advanceNativeSeasonRound(memory_);return openNextSeasonRace();
        }
        if(page_==NativeMenuPage::SeasonEnd){
            openCompetitionPage((memory_[0x593f6]&0x80)?NativeMenuPage::Roster:NativeMenuPage::SeasonAward,3);
            return NativeMenuAction::ShowCompetitionPage;
        }
        if(page_==NativeMenuPage::SeasonAward){openCompetitionPage(NativeMenuPage::Roster,3);return NativeMenuAction::ShowCompetitionPage;}
        initializeNativeGameTables(memory_);openMain();return NativeMenuAction::ShowCompetitionPage;
    }
    if(page_==NativeMenuPage::Preview){
        memory_[0x1bb47]=controls;previewReleaseMask_&=controls;memory_[0x5d9da]=previewReleaseMask_;
        if(previewReleaseMask_)return NativeMenuAction::None;
        if(controls&0x0c){
            memory_[0x1bb57]=std::uint8_t(memory_[0x1bb57]+((controls&4)?1:-1));
            previewReleaseMask_=0x1f;memory_[0x5d9da]=0x1f;return NativeMenuAction::RotatePreview;
        }
        if(controls&0x10){page_=NativeMenuPage::Race;memory_[0x57c62]=0;
            return competitive_?NativeMenuAction::StartRace:NativeMenuAction::StartPractice;}
        return NativeMenuAction::None;
    }
    if(phase_==Phase::PaintDelay||phase_==Phase::ReleaseDelay||phase_==Phase::ConfirmDelay||phase_==Phase::TrackDelay){
        remaining_-=elapsed;if(remaining_>0)return NativeMenuAction::None;
        memory_[0x1bb1a]=memory_[0x1bb1b]=0;
        if(phase_==Phase::ConfirmDelay)return confirm();
        if(phase_==Phase::TrackDelay){
            page_=NativeMenuPage::Preview;memory_[0x57c62]=0x12;previewReleaseMask_=0x1f;memory_[0x5d9da]=0x1f;
            return NativeMenuAction::EnterPreview;
        }
        phase_=phase_==Phase::PaintDelay?Phase::Release:Phase::Input;
    }
    if(phase_==Phase::Release){
        memory_[0x1bb47]=controls;
        if(!controls){phase_=Phase::ReleaseDelay;startDelay(2);}
        return NativeMenuAction::None;
    }
    if(!applyNativeMenuInput(memory_,input))return NativeMenuAction::None;
    startDelay(15,true);
    phase_=memory_[highlight]?Phase::ConfirmDelay:Phase::PaintDelay;
    return NativeMenuAction::Changed;
}
void NativeMenuFlow::returnFromRace(){
    if(competitive_){
        transferNativeLapRecords(memory_,false);
        scoreNativeSeasonRound(memory_);
        memory_[0x1bb5f]=std::uint8_t(competitionDivision_);configureNativeSeasonMatch(memory_);
        openCompetitionPage(NativeMenuPage::Results,0x11);return;
    }
    openMain();
    // Original0x5baf2 immediately re-enters handleTrackSelection when the
    // previous successful Practice choice set divisionSelectedFlag.
    if(memory_[0x5eb7d])openPractice();
}
void NativeMenuFlow::cancelPreview(){
    memory_[0x5eb7d]=0;openMain();
}
void NativeMenuFlow::openCompetitionPage(NativeMenuPage page,unsigned gameState){
    page_=page;memory_[0x57c62]=std::uint8_t(gameState);competitionFire_.reset();
}
NativeMenuAction NativeMenuFlow::openNextSeasonRace(){
    if(seekNativeSeasonRace(memory_)){
        openCompetitionPage(NativeMenuPage::Matchup,0x11);return NativeMenuAction::Changed;
    }
    finishNativeSeason(memory_);openCompetitionPage(NativeMenuPage::SeasonEnd,3);
    return NativeMenuAction::Changed;
}
} // namespace scr::viewer
