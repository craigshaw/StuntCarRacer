#include "menus/native_menu_flow.h"
#include "race/native_race_initialization.h"
#include "session/native_multiplayer_state.h"
#include <algorithm>
#include <stdexcept>

namespace scr::runtime {
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

bool applyNativeMenuInput(NativeMemory& m,const NativeMenuInput& input,unsigned disabledRows){
    const auto controls=std::uint8_t(input.controls&0x1f);
    m[0x1bb47]=controls;m[highlight]=controls&0x10;
    const auto disabled=[&](unsigned row){return row<32&&(disabledRows&(1u<<row));};
    if(m[highlight]){if(disabled(m[selection])){m[highlight]=0;return false;}return true;}
    // Raw original table at0x60c8d is used at0x5b9d2. The enhanced upstream
    // equate points elsewhere; that provisional label is not used here.
    const unsigned last=unsigned(m[maxIndex])+1;
    if(last>3)throw std::invalid_argument("Unsupported original menu row count");
    for(unsigned row=last+1;row>0;){
        --row;const unsigned key=m[0x60c8d+row];
        if(!disabled(row)&&key>=1&&key<=4&&(input.numberedKeys&(1u<<(key-1)))){
            m[selection]=std::uint8_t(row);return true;
        }
    }
    if(!(controls&3))return false;
    unsigned selected=m[selection];
    if(controls&1){
        while(selected){--selected;if(!disabled(selected))break;}
    }else{
        while(selected<last){++selected;if(!disabled(selected))break;}
    }
    if(disabled(selected))return false;
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
NativeMenuFlow::NativeMenuFlow(NativeMemory& memory,const NativeMenuContinuation& state):memory_(memory){
    if(unsigned(state.page)>unsigned(NativeMenuPage::TrackResults)||state.division>3||(state.context!=0&&state.context!=11))throw std::invalid_argument("Invalid continuation phase");
    page_=state.page;competitive_=state.competitive;multiplayerTrackEnd_=state.multiplayerTrackEnd;
    competitionDivision_=state.division;competitionContext_=state.context;
    mainSelection_=state.mainSelection;divisionSelection_=state.divisionSelection;saveSelection_=state.saveSelection;trackSelections_=state.trackSelections;
    phase_=Phase::Input;previewReleaseMask_=0;
}
NativeMenuContinuation NativeMenuFlow::continuation()const noexcept{
    return {page_,competitive_,multiplayerTrackEnd_,competitionDivision_,competitionContext_,mainSelection_,divisionSelection_,saveSelection_,trackSelections_};
}

void NativeMenuFlow::startDelay(unsigned units,bool repaint){
    // Original renderTrackName leaves its 15-character loop counter here.
    // Its final Cancel row preserves that value. Other screens do not change
    // this counter, and completion of each delay clears it again.
    if(repaint&&page_==NativeMenuPage::Track)memory_[0x1bb1b]=15;
    remaining_=nativeMenuDelaySeconds(units,memory_[0x1bb1b]);memory_[0x1bb1a]=20;
}
void NativeMenuFlow::openPage(NativeMenuPage page,unsigned offset,unsigned count,unsigned selected,unsigned textMode){
    if(navigation_==NativeMenuNavigation::Responsive){
        const int remembered=page==NativeMenuPage::Main?mainSelection_:page==NativeMenuPage::Division?divisionSelection_:
            page==NativeMenuPage::LoadSave?saveSelection_:page==NativeMenuPage::Track?trackSelections_[memory_[0x5eb7c]&3]:-1;
        if(remembered>=0)selected=unsigned(remembered);
    }
    page_=page;memory_[0x1bb8b]=std::uint8_t(offset);memory_[maxIndex]=std::uint8_t(count-1);
    memory_[selection]=std::uint8_t(selected);memory_[highlight]=0;memory_[0x5eb75]=std::uint8_t(textMode);
    phase_=Phase::PaintDelay;startDelay(15,true);
}
void NativeMenuFlow::openMain(bool beginLoop){
    // Original menuSystemLoop and handleMainMenu,0x5cfb8 /0x5baea.
    if(beginLoop){memory_[0x1bb5b]&=0x7f;memory_[0x57c62]=0x10;}
    memory_[0x5b83d]=0;
    openPage(NativeMenuPage::Main,0,mainRowCount_-1,(memory_[0x1ca22]&0x80)?2:1,0);
    competitive_=false;
}
void NativeMenuFlow::setMainRowCount(unsigned count){
    if(count<3||count>4)throw std::invalid_argument("Invalid main menu row count");
    mainRowCount_=count;
    if(mainSelection_>=int(count))mainSelection_=int(count)-1;
    if(page_==NativeMenuPage::Main){memory_[maxIndex]=std::uint8_t(count-2);memory_[selection]=std::uint8_t(std::min(unsigned(memory_[selection]),count-1));}
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
    rememberSelection();
    const unsigned selected=memory_[selection];
    if(page_==NativeMenuPage::Main){
        if(selected==0){
            memory_[0x5d724]=0x42;page_=NativeMenuPage::HallOfFame;competitionFire_.reset();
            return NativeMenuAction::ShowHallOfFame;
        }
        if(selected==1){openPractice();return NativeMenuAction::Changed;}
        if(selected==2){
            memory_[0x1ca22]=0x80;memory_[0x5eb7d]=0;competitive_=true;
            multiplayerTrackEnd_=false;
            competitionDivision_=memory_[0x1c9ce];
            competitionContext_=memory_[0x1c9d0];
            if(multiplayer())initializeNativeMultiplayerSeason(memory_);
            else initializeNativeSeason(memory_);
            return openNextSeasonRace();
        }
        phase_=Phase::LoadSaveDelay;startDelay(20);return NativeMenuAction::None;
    }
    if(page_==NativeMenuPage::LoadSave){
        if(selected==3){phase_=Phase::MainDelay;startDelay(20);return NativeMenuAction::None;}
        page_=NativeMenuPage::SaveSlots;
        return selected==0?NativeMenuAction::LoadGame:selected==1?NativeMenuAction::SaveGame:NativeMenuAction::ReplayGame;
    }
    if(page_==NativeMenuPage::Division){
        memory_[0x5eb7c]=std::uint8_t(selected^3);
        openPage(NativeMenuPage::Track,0x1c,2,memory_[0x1ca22]&1,0x40);
        return NativeMenuAction::Changed;
    }
    if(page_==NativeMenuPage::Track){
        memory_[0x5eb75]=0;
        if(selected>=2){if(navigation_==NativeMenuNavigation::Responsive)return back();openMain();return NativeMenuAction::Changed;}
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
    const bool responsive=navigation_==NativeMenuNavigation::Responsive;
    if(responsive&&input.back)return back();
    const auto controls=std::uint8_t(input.controls&0x1f);
    if(page_==NativeMenuPage::Race)return NativeMenuAction::None;
    if(page_==NativeMenuPage::HallOfFame){
        memory_[0x1bb47]=controls;
        if(responsive?!(controls&16):!competitionFire_.update(controls,elapsed))return NativeMenuAction::None;
        rememberSelection();
        openMain(false);return NativeMenuAction::LeaveHallOfFame;
    }
    if(page_==NativeMenuPage::Matchup||page_==NativeMenuPage::Results||
            page_==NativeMenuPage::RoundResults||page_==NativeMenuPage::Standings||page_==NativeMenuPage::SeasonEnd||
            page_==NativeMenuPage::SeasonAward||page_==NativeMenuPage::Roster||page_==NativeMenuPage::Records||
            page_==NativeMenuPage::RaceResults||page_==NativeMenuPage::TrackResults){
        memory_[0x1bb47]=controls;
        if(responsive?!(controls&16):!competitionFire_.update(controls,elapsed))return NativeMenuAction::None;
        memory_[0x1bb6e]=0;
        if(page_==NativeMenuPage::Matchup){
            if(memory_[0xead6+0x50]){
                memory_[0x57c68]=0;
                if(multiplayer()){
                    forfeitNativeMultiplayerTrack(memory_);
                    awardNativeMultiplayerTrackBonuses(memory_);multiplayerTrackEnd_=true;
                    openCompetitionPage(NativeMenuPage::TrackResults,memory_[0x57c62]);
                }else{
                    forfeitNativeSeason(memory_);finishNativeSeason(memory_);
                    openCompetitionPage(NativeMenuPage::SeasonEnd,memory_[0x57c62]);
                }
                return NativeMenuAction::Changed;
            }
            // The original start check follows the released/new-fire wait.
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
            if(multiplayer()){
                if(multiplayerTrackEnd_){
                    multiplayerTrackEnd_=false;memory_[0x5eb7a]=0;
                    if(advanceNativeMultiplayerTrack(memory_)==NativeMultiplayerTrackAdvance::NextTrack)return openNextSeasonRace();
                    openMain();return NativeMenuAction::Changed;
                }
                openCompetitionPage(NativeMenuPage::RaceResults,3);return NativeMenuAction::Changed;
            }
            advanceNativeSeasonRound(memory_);return openNextSeasonRace();
        }
        if(page_==NativeMenuPage::RaceResults){
            if(advanceNativeMultiplayerRace(memory_))return openNextSeasonRace();
            awardNativeMultiplayerTrackBonuses(memory_);multiplayerTrackEnd_=true;
            openCompetitionPage(NativeMenuPage::TrackResults,3);return NativeMenuAction::Changed;
        }
        if(page_==NativeMenuPage::TrackResults){
            openCompetitionPage(NativeMenuPage::Standings,memory_[0x57c62]);return NativeMenuAction::Changed;
        }
        if(page_==NativeMenuPage::SeasonEnd){
            openCompetitionPage((memory_[0x593f6]&0x80)?NativeMenuPage::Roster:NativeMenuPage::SeasonAward,memory_[0x57c62]);
            return NativeMenuAction::ShowCompetitionPage;
        }
        if(page_==NativeMenuPage::SeasonAward){openCompetitionPage(NativeMenuPage::Roster,memory_[0x57c62]);return NativeMenuAction::ShowCompetitionPage;}
        initializeNativeGameTables(memory_);openMain();return NativeMenuAction::ShowCompetitionPage;
    }
    if(page_==NativeMenuPage::Preview){
        memory_[0x1bb47]=controls;previewReleaseMask_&=controls;memory_[0x5d9da]=previewReleaseMask_;
        if(!responsive&&previewReleaseMask_)return NativeMenuAction::None;
        if(controls&0x0c){
            memory_[0x1bb57]=std::uint8_t(memory_[0x1bb57]+((controls&4)?1:-1));
            previewReleaseMask_=0x1f;memory_[0x5d9da]=0x1f;return NativeMenuAction::RotatePreview;
        }
        if(controls&0x10){page_=NativeMenuPage::Race;memory_[0x57c62]=0;
            if(multiplayer())transferNativeMultiplayerControls(memory_,true);
            return competitive_?NativeMenuAction::StartRace:NativeMenuAction::StartPractice;}
        return NativeMenuAction::None;
    }
    if(phase_==Phase::PaintDelay||phase_==Phase::ReleaseDelay||phase_==Phase::ConfirmDelay||phase_==Phase::TrackDelay||phase_==Phase::LoadSaveDelay||phase_==Phase::MainDelay){
        remaining_=responsive?0:remaining_-elapsed;if(remaining_>0)return NativeMenuAction::None;
        memory_[0x1bb1a]=memory_[0x1bb1b]=0;
        if(phase_==Phase::LoadSaveDelay){openPage(NativeMenuPage::LoadSave,4,3,3,0);return NativeMenuAction::Changed;}
        if(phase_==Phase::MainDelay){openMain(false);return NativeMenuAction::Changed;}
        if(phase_==Phase::ConfirmDelay){
            const auto action=confirm();
            if(responsive&&action==NativeMenuAction::None)return update({},0);
            return action;
        }
        if(phase_==Phase::TrackDelay){
            page_=NativeMenuPage::Preview;memory_[0x57c62]=0x12;previewReleaseMask_=0x1f;memory_[0x5d9da]=0x1f;
            return NativeMenuAction::EnterPreview;
        }
        phase_=phase_==Phase::PaintDelay&&!responsive?Phase::Release:Phase::Input;
    }
    if(phase_==Phase::Release){
        if(responsive)phase_=Phase::Input;
        else{
        memory_[0x1bb47]=controls;
        if(!controls){phase_=Phase::ReleaseDelay;startDelay(2);}
        return NativeMenuAction::None;
        }
    }
    if(!applyNativeMenuInput(memory_,input))return NativeMenuAction::None;
    if(responsive){
        rememberSelection();
        if(memory_[highlight]){
            const auto action=confirm();
            if(action==NativeMenuAction::None)return update({},0);
            return action;
        }
        phase_=Phase::Input;return NativeMenuAction::Changed;
    }
    startDelay(15,true);
    phase_=memory_[highlight]?Phase::ConfirmDelay:Phase::PaintDelay;
    return NativeMenuAction::Changed;
}
void NativeMenuFlow::returnFromRace(){
    if(competitive_){
        transferNativeLapRecords(memory_,false);
        if(multiplayer()){
            transferNativeMultiplayerControls(memory_,false);scoreNativeMultiplayerRace(memory_);
            memory_[0x1bb5f]=std::uint8_t(competitionDivision_);configureNativeMultiplayerRace(memory_);
        }else{
            scoreNativeSeasonRound(memory_);
            memory_[0x1bb5f]=std::uint8_t(competitionDivision_);configureNativeSeasonMatch(memory_);
        }
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
void NativeMenuFlow::returnFromSave(bool cancelled){
    if(cancelled&&navigation_==NativeMenuNavigation::Responsive)openPage(NativeMenuPage::LoadSave,4,3,3,0);
    else openMain(false);
}
void NativeMenuFlow::rememberSelection(){
    const int row=memory_[selection];
    if(page_==NativeMenuPage::Main)mainSelection_=row;
    else if(page_==NativeMenuPage::Division)divisionSelection_=row;
    else if(page_==NativeMenuPage::Track)trackSelections_[memory_[0x5eb7c]&3]=row;
    else if(page_==NativeMenuPage::LoadSave)saveSelection_=row;
}
NativeMenuAction NativeMenuFlow::back(){
    if(navigation_!=NativeMenuNavigation::Responsive)return NativeMenuAction::None;
    rememberSelection();
    switch(page_){
    case NativeMenuPage::Division:memory_[0x5eb7d]=0;openMain(false);break;
    case NativeMenuPage::Track:
        memory_[0x5eb7d]=0;openPage(NativeMenuPage::Division,0x18,3,(memory_[0x1c9ce]^3)&3,0x80);break;
    case NativeMenuPage::Preview:
        if(competitive_)openCompetitionPage(NativeMenuPage::Matchup,0x11);
        else{memory_[0x57c62]=0x10;openPage(NativeMenuPage::Track,0x1c,2,memory_[0x1ca22]&1,0x40);}break;
    case NativeMenuPage::HallOfFame:openMain(false);return NativeMenuAction::LeaveHallOfFame;
    case NativeMenuPage::LoadSave:openMain(false);break;
    case NativeMenuPage::SaveSlots:openPage(NativeMenuPage::LoadSave,4,3,3,0);break;
    default:return NativeMenuAction::None;
    }
    return NativeMenuAction::Changed;
}
void NativeMenuFlow::openCompetitionPage(NativeMenuPage page,unsigned gameState){
    page_=page;memory_[0x57c62]=std::uint8_t(gameState);competitionFire_.reset();
    if(multiplayer()){
        memory_[0x1bb6e]=0;
        memory_[0x1bbcb]=(page==NativeMenuPage::RaceResults||page==NativeMenuPage::TrackResults)?0x80:0;
        memory_[0x5eb7a]=multiplayerTrackEnd_?0x80:0;
    }
}
NativeMenuAction NativeMenuFlow::openNextSeasonRace(){
    if(multiplayer()){
        prepareNativeMultiplayerRace(memory_);openCompetitionPage(NativeMenuPage::Matchup,0x11);
        return NativeMenuAction::Changed;
    }
    if(seekNativeSeasonRace(memory_)){
        openCompetitionPage(NativeMenuPage::Matchup,0x11);return NativeMenuAction::Changed;
    }
    finishNativeSeason(memory_);openCompetitionPage(NativeMenuPage::SeasonEnd,3);
    return NativeMenuAction::Changed;
}
} // namespace scr::runtime
