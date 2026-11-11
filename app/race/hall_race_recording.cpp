#include "race/hall_race_recording.h"
#include "platform/windows/shared_hall_storage.h"
#include <algorithm>
#include <limits>

namespace scr::runtime {
HallRaceRecording::HallRaceRecording(HallAttemptId run,HallBoardKey board,HallCatalog catalog,bool competitive)
    :run_(run),board_(board),catalog_(catalog),competitive_(competitive){
    (void)HallOfFame(catalog_);(void)hallBoardIndex(board_);
    if(board_.kind!=HallRecordKind::Lap||std::none_of(run_.begin(),run_.end(),[](auto b){return b!=0;}))throw std::invalid_argument("Invalid Hall race recording context");
}
void HallRaceRecording::complete(LapCompletion lap,const state::HumanRaceRecord& human){
    if(crossing_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Hall crossing sequence exhausted");
    ++crossing_;
    const auto name=normaliseHallName(std::string(human.name.begin(),human.name.end())).display;
    if(lap.enhanced){
        const EnhancedCatalog catalog{catalog_.trackContent,catalog_.rules,enhancedTimingMethodVersion};
        if(lap.enhanced->lap.rankable()){
            EnhancedHallAttempt a{enhancedCompletionId(run_,crossing_,HallRecordKind::Lap),board_,name,lap.enhanced->lap,{}};
            std::shared_ptr<const EnhancedHallGhost> recording;
            if(lap.enhancedRecording){auto g=std::make_shared<EnhancedHallGhost>();g->catalog=catalog;g->attempt=a.id;g->board=board_;g->recording=*lap.enhancedRecording;recording=std::move(g);}
            enhancedCompleted_.push_back(captureEnhancedSubmission(std::move(a),std::move(recording)));
        }
        if(competitive_&&lap.enhanced->race&&lap.enhanced->race->enhanced.rankable()){
            auto raceBoard=board_;raceBoard.kind=HallRecordKind::Race;
            EnhancedHallAttempt a{enhancedCompletionId(run_,crossing_,HallRecordKind::Race),raceBoard,name,lap.enhanced->race->enhanced,{}};
            enhancedCompleted_.push_back(captureEnhancedSubmission(std::move(a),{}));
        }
    }
    HallAttempt attempt{hallCompletionId(run_,crossing_,HallRecordKind::Lap),board_,name,lap.time,{}};
    std::shared_ptr<const HallGhost> recording;
    if(lap.recording){auto g=std::make_shared<HallGhost>();g->catalog=catalog_;g->attempt=attempt.id;g->board=board_;g->time=lap.time;
        auto& data=*lap.recording;g->durationMicroseconds=data.durationMicroseconds;g->start=data.start;g->finish=data.finish;
        g->samples=std::move(data.samples);g->bridges=std::move(data.bridges);recording=std::move(g);}
    completed_.push_back(captureHallSubmission(std::move(attempt),std::move(recording)));
    if(!player_)player_=human.player;
    raceEligible_=raceEligible_&&*player_==human.player&&lap.counter==fullLaps_+2;
    ++fullLaps_;
    if(competitive_&&raceEligible_&&fullLaps_==3&&lap.counter==4){
        auto raceBoard=board_;raceBoard.kind=HallRecordKind::Race;
        HallAttempt race{hallCompletionId(run_,crossing_,HallRecordKind::Race),raceBoard,name,human.total,{}};
        completed_.push_back(captureHallSubmission(std::move(race),{}));
    }
}
std::vector<HallSubmission> HallRaceRecording::take(){auto result=std::move(completed_);completed_.clear();return result;}
std::vector<EnhancedSubmission> HallRaceRecording::takeEnhanced(){auto result=std::move(enhancedCompleted_);enhancedCompleted_.clear();return result;}
} // namespace scr::runtime
