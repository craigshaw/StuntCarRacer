// A standalone headless native run. This executable does not link Musashi.
// Capture decoding is a test adapter, not the port's eventual asset format.
#include "driving_replay_io.h"
#include <iostream>
#include <memory>
int main(int argc,char** argv)try {
    if(argc!=4&&argc!=5)throw std::runtime_error("Usage: driving_replay_native chipram inputs.csv trace.csv [--full-loop]");
    const bool full=argc==5&&std::string(argv[4])=="--full-loop";
    if(argc==5&&!full)throw std::runtime_error("Unknown replay option");
    auto capture=scr::test::replay::loadCapture(argv[1]);
    auto state=std::make_unique<scr::test::replay::NativeReplay>(*capture);
    const auto masks=scr::test::replay::loadInputs(argv[2]);std::ofstream out(argv[3]);
    if(!full)for(const auto& input:masks)if(input.quit)throw std::runtime_error("Quit controls require --full-loop");
    if(!out)throw std::runtime_error("Cannot create native trace");scr::test::replay::traceHeader(out);
    for(unsigned tick=0;tick<masks.size();++tick){
        state->begin(masks[tick].controls);state->ram[0xead6+0x45]=masks[tick].quit?0xb3:0;
        state->controlsStage();state->physicsStage();state->engineStage();state->progressStage();
        state->groundStage();state->wheelsStage();state->damageStage();state->timingStage();
        if(full)state->outerStage();
        scr::test::replay::traceRow(out,state->ram,tick,masks[tick].controls,0,unsigned(state->outerAction),state->restarts,masks[tick].quit);
        if(full&&(state->outerAction==scr::RaceTransitionAction::raceEnd||state->outerAction==scr::RaceTransitionAction::quit)&&tick+1<masks.size())throw std::runtime_error("Inputs continue after terminal race transition");
    }
    std::cout<<"{\"ticks\":"<<masks.size()<<",\"cpu_emulation\":false,\"comparison_performed\":false,\"full_loop\":"<<(full?"true":"false")<<",\"restarts\":"<<state->restarts<<",\"frame_wait_vblanks\":"<<state->waitVblanks<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
