#pragma once
#include "assets/native_startup.h"
#include "menus/native_menu_flow.h"
#include "presentation/native_track_preview.h"
#include "race/race_scene.h"
#include "input/drive_input.h"
#include "presentation/native_palette_fade.h"
#include "menus/native_opening_flow.h"
#include "presentation/native_opening_render.h"
#include "menus/native_player_name.h"
#include "input/native_keyboard_input.h"
#include "menus/native_save_menu.h"
#include "persistence/native_save_state.h"
#include "persistence/native_save_store.h"
#include "persistence/native_career_checkpoint.h"
#include <optional>

namespace scr::runtime {
struct NativeGameInput {
    NativeMenuInput menu;
    unsigned driveControls{};
    bool quit{};
    std::vector<NativeRawKeyEvent> rawKeys;
    DriveControlPreset drivePreset{DriveControlPreset::Original};
    // Host Game menu command; independent of the original keyboard quit gates.
    bool leaveRace{};
};
enum class NativeGameUnavailable { none,menuRoute };
enum class NativeGameStart { OriginalOpening,EstablishedMenu };
struct NativeGameUpdate {
    bool raceStarted{},raceExited{};
    NativeGameUnavailable unavailable{};
    std::vector<GameAudioFrame> audio;
};
// Original opening -> names -> roster -> menu -> selection -> race -> menu.
// The memory belongs to the session; leaving a race retains the recovered RNG,
// player and track-selection state instead of constructing another saved game.
class NativeGameSession {
public:
    NativeGameSession(const std::filesystem::path& pack,const std::filesystem::path& recordings,
        NativeGameStart start=NativeGameStart::OriginalOpening,const std::filesystem::path& saveFile={});
    NativeGameUpdate update(double elapsed,const NativeGameInput&);
    void enableCareers(const std::filesystem::path& dataRoot);
    bool requestClose();
    bool closeReady()const noexcept{return closeReady_;}
    bool closePending()const noexcept{return departure_==Departure::Close;}
    bool multiplayer()const noexcept{return (*data_.memory)[0x5eb76]!=0;}
    bool competitive()const noexcept{return menu_&&menu_->competitive();}
    std::string saveStatus()const;
    bool careerHome()const noexcept{return careerPanel_==CareerPanel::Home;}
    bool hasSuspendedSeason()const noexcept{return !suspendedSeason_.empty();}
    const std::optional<NativeCareerInfo>& activeCareer()const noexcept{return activeCareer_;}
    void setNavigation(NativeMenuNavigation);
    const std::vector<std::uint32_t>& render(bool interpolate=false,bool paused=false,NativeView view=NativeView::Original);
    NativeMenuPage page()const noexcept{
        if(careerPanel_!=CareerPanel::None)return NativeMenuPage::Opening;
        if(finishPending_)return NativeMenuPage::Race;
        if(openingActive())return NativeMenuPage::Opening;
        if(saveReturn_==SaveReturn::Roster)return NativeMenuPage::Roster;
        if(saveReturn_==SaveReturn::Standings)return NativeMenuPage::Standings;
        return menu_->page();
    }
    NativeMenuRenderState menuState()const{
        if(careerPanel_!=CareerPanel::None){
            NativeMenuRenderState state;state.selected=careerPanel_==CareerPanel::List?listSelection_%3:careerSelection_;state.division=(*data_.memory)[0x1c9ce];
            if(careerPanel_==CareerPanel::Notice)state.count=0;
            else if(careerPanel_==CareerPanel::Home)state.count=unsigned(homeActions_.size()-1);
            else if(careerPanel_==CareerPanel::Delete||careerPanel_==CareerPanel::SaveError||careerPanel_==CareerPanel::CreateError)state.count=1;
            else if(careerPanel_==CareerPanel::List){const auto first=(listSelection_/3)*3;state.count=careerList_.empty()?0:unsigned(std::min<std::size_t>(3,careerList_.size()-first))-1;}
            else state.count=2;
            return state;
        }
        return openingActive()?opening_->renderState():menu_->renderState();
    }
    bool openingActive()const noexcept{return careerPanel_==CareerPanel::None&&opening_&&!opening_->complete();}
    NativeOpeningPage openingPage()const noexcept{return opening_?opening_->page():NativeOpeningPage::Complete;}
    bool enteringName()const noexcept{return openingActive()&&opening_->page()==NativeOpeningPage::Name;}
    bool usingTextInput()const noexcept{return enteringName()||bool(saveMenu_);}
    std::optional<NativeSaveMenuPhase> savePhase()const noexcept{return saveMenu_?std::optional{saveMenu_->phase()}:std::nullopt;}
    const NativeMemory& memory()const noexcept{return *data_.memory;}
    const RaceScene& race()const noexcept{return race_;}
    const PersonalBestLaps& personalBestLaps()const noexcept{return personalBestLaps_;}
    void setPracticeGhostVisible(bool visible)noexcept{practiceGhostVisible_=visible;}
    bool practiceGhostActive()const noexcept;
    bool canClearPracticeGhost()const;
    std::string practiceGhostTime()const;
    bool clearPracticeGhost();
    std::string ghostFeedback()const;
    bool ghostFeedbackWarning()const;
    bool raceEnded()const noexcept{return page()==NativeMenuPage::Race&&(finishPending_||race_.liveTerminal());}
    bool transitioning()const noexcept{return transition_!=Transition::None;}
    void releaseInput(const NativeMenuInput& held={})noexcept;
private:
    enum class CareerPanel {None,Home,List,Actions,Delete,Notice,SaveError,CreateError,HallRecovery,Departure};
    enum class Departure {None,Close,NewCareer,LoadCareer,Multiplayer};
    enum class HomeAction {Continue,NewCareer,Browse};
    std::unique_ptr<NativeMemory> careerBaseline_;
    std::unique_ptr<NativeMemory> careerDraft_;
    unsigned draftNameLength_{};
    std::unique_ptr<NativeCareerStore> careerStore_;
    NativeHallBanks hall_{};
    bool hallWritable_{true},creatingCareer_{},finishPending_{},closeReady_{},errorAcknowledged_{},multiplayerSetup_{};
    CareerPanel careerPanel_{CareerPanel::None},returnPanel_{CareerPanel::None},departureReturnPanel_{CareerPanel::None};
    Departure departure_{Departure::None};
    unsigned careerSelection_{},listSelection_{};
    std::vector<NativeCareerInfo> careerList_;
    std::optional<NativeCareerInfo> activeCareer_,newCareer_;
    CareerBytes checkpoint_,newCheckpoint_,suspendedSeason_;
    std::vector<HomeAction> homeActions_;
    std::string recentCareerName_;
    std::vector<std::uint32_t> finishPixels_;
    NativeMenuInputClock careerClock_;
    std::string lastCareer_,notice_,loadCareer_;
    void showCareerPanel(CareerPanel);
    void showCareerHome();
    void refreshCareerHome();
    void openSinglePlayerMenu(bool captureSeason=true);
    void resumeSeason();
    NativeMenuImage careerImage()const;
    bool updateCareers(double,const NativeGameInput&,NativeGameUpdate&);
    void observeCareerState(NativeMenuPage previous);
    void saveCheckpoint(bool terminal=false);
    void restoreCheckpoint(const CareerBytes&);
    bool loadCareer(const std::string&);
    void beginCareer();
    void registerCareer();
    void completeCareerCreation();
    void cancelCareerCreation();
    void mergeHallInto(NativeMemory&)const;
    void observeHall();
    bool depart(Departure,const std::string& id={});
    void completeDeparture();
    void checkSaveFailure();
    NativeStartupData data_;
    std::unique_ptr<NativeMenuFlow> menu_;
    std::unique_ptr<NativeOpeningFlow> opening_;
    NativePlayerNameEntry nameEntry_;
    std::unique_ptr<NativeOpeningNameScreen> nameScreen_;
    NativeSaveStore saveStore_;
    std::unique_ptr<NativeSaveMenu> saveMenu_;
    std::optional<NativeSavePayload> pendingSave_;
    enum class SaveReturn {None,Roster,Standings};
    SaveReturn saveReturn_{SaveReturn::None};
    NativeFireWait saveReturnFire_;
    bool saveLoaded_{};
    bool saveWriting_{};
    NativeTrackPreview preview_;
    PersonalBestLaps personalBestLaps_;
    std::optional<GhostLapKey> lapKey_;
    CareerFileState ghostFileState_{CareerFileState::Missing};
    unsigned lapImprovements_{},lapOverflows_{};
    bool practiceGhostVisible_{true},ghostConditionsDiffer_{};
    double ghostNoticeSeconds_{};
    std::string ghostNotice_;
    void observePersonalBest();
    RaceScene race_;
    FixedDriveClock driveClock_;
    DriveInputLatch driveLatch_;
    std::filesystem::path recordings_;
    std::vector<std::uint32_t> pixels_;
    NativeFireWait endFireWait_;
    bool quitPending_{};
    NativeView presentationView_{NativeView::Original};
    NativeMenuNavigation navigation_{NativeMenuNavigation::Original};
    NativeMenuInputClock navigationClock_;
    std::array<unsigned,8> nameLengths_{};
    bool previewPrepared_{};
    unsigned preparedTrack_{},preparedLeague_{};
    bool preparedCompetitive_{};
    enum class Transition {None,InitialMenu,OutPreview,InPreview,OutRace,RaceDelay,InRace,OutMenu,InMenu,OutCompetition,InCompetition,OutOpening,InOpening,OutHall,InHall,OutHallMenu,InHallMenu,OutSaveRoster,InSaveRoster,OutReplayFrame,InReplayFrame,OutRestartOpening,SaveDelay};
    Transition transition_{Transition::InitialMenu};
    NativePaletteFade fade_;
    std::vector<std::uint16_t> transitionIndices_;
    double raceDelay_{};
    void advanceTransition(double,NativeGameUpdate&);
    void preparePreview();
    void startRace();
    void leaveRace();
    NativeMenuImage menuImage()const;
    void updateOpening(const NativeGameInput&,double,NativeGameUpdate&);
    void handleOpeningAction(NativeOpeningAction,const NativeMenuImage&,NativeGameUpdate&);
    void handleSaveAction(const NativeSaveMenuResult&);
    void finishSave(bool cancelled=false);
    void showSaveReturn(const NativeMenuImage&);
    void replayGame(const NativeMenuImage&);
    NativeGameUpdate updateImpl(double,const NativeGameInput&);
    void finishResponsiveTransitions(NativeGameUpdate&);
    unsigned navigationScreen()const noexcept;
};
} // namespace scr::runtime
