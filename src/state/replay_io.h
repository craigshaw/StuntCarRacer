#pragma once
#include "native_race.h"
#include <fstream>
#include <sstream>
#include <string>
#include <memory>

namespace scr::state::replay {
inline std::unique_ptr<Ram> loadCapture(const char* path) {
    auto r=std::make_unique<Ram>();std::ifstream f(path,std::ios::binary);f.read(reinterpret_cast<char*>(r->data()),r->size());
    if(!f||f.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Expected exactly 512 KiB of captured Chip RAM");return r;
}
struct ReplayInput {unsigned controls{};bool quit{};};
inline std::vector<ReplayInput> loadInputs(const char* path) {
    std::ifstream f(path);std::string line;
    if(!std::getline(f,line))throw std::runtime_error("Missing replay header");
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    const bool hasMode=line=="tick,controls,quit,hold_acceleration";
    const bool extended=hasMode||line=="tick,controls,quit";
    if(!extended&&line!="tick,controls")throw std::runtime_error("Expected tick,controls[,quit[,hold_acceleration]] CSV header");
    std::vector<ReplayInput> masks;
    while(std::getline(f,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(hasMode){
            const auto mode=line.rfind(',');
            if(mode==std::string::npos||mode+1==line.size())throw std::runtime_error("Missing throttle mode");
            const auto value=line.substr(mode+1);
            if(value=="1")throw std::runtime_error("Modern held-acceleration recordings cannot use original-code replay");
            if(value!="0")throw std::runtime_error("hold_acceleration must be 0 or 1");
            line.resize(mode);
        }
        const auto comma=line.find(','),last=line.find(',',comma+1);
        if(comma==std::string::npos||comma==0||comma+1==line.size()||
           (extended?(last==std::string::npos||last+1==line.size()||line.find(',',last+1)!=std::string::npos):(last!=std::string::npos)))
            throw std::runtime_error("Malformed replay row");
        for(unsigned i=0;i<line.size();++i)if(i!=comma&&i!=last&&(line[i]<'0'||line[i]>'9'))throw std::runtime_error("Replay values must be decimal integers");
        const auto tick=std::stoul(line.substr(0,comma)),mask=std::stoul(line.substr(comma+1,(extended?last:line.size())-comma-1));
        const auto quit=extended?std::stoul(line.substr(last+1)):0;
        if(tick!=masks.size()||mask>31||quit>1)throw std::runtime_error("Replay ticks must be contiguous; controls 0..31; quit 0..1");masks.push_back({unsigned(mask),quit!=0});
    }
    if(masks.empty())throw std::runtime_error("Empty replay");return masks;
}
inline void traceHeader(std::ostream& out) {
    out<<"tick,controls,world_x,world_y,world_z,pitch,yaw,roll,speed,segment,progress,lateral,off_track,boost_fuel,damage,original_instructions,lap_counter,crossing_armed,race_complete,outer_action,restarts,quit,opponent_segment,opponent_progress,opponent_speed,opponent_height,opponent_lateral,opponent_lap,opponent_ahead\n";
}
inline void traceRow(std::ostream& out,const Ram& r,unsigned tick,unsigned mask,unsigned instructions,unsigned action=0,unsigned restarts=0,bool quit=false) {
    out<<tick<<','<<mask;for(auto a:{0x1bcd8,0x1bcdc,0x1bce0})out<<','<<audio::longword(r.data(),a);
    for(auto a:{0x1bce4,0x1bce6,0x1bce8,0x1bd30})out<<','<<audio::word(r.data(),a);
    out<<','<<unsigned(r[0x1bb1c])<<','<<audio::word(r.data(),0x1bb0a)<<','<<audio::word(r.data(),0x1bc5c)<<','<<unsigned(r[0x1bb9c])<<','<<unsigned(r[0x1ca20])<<','<<unsigned(r[0x1bb55])<<','<<instructions<<','<<unsigned(r[0x1bb20])<<','<<unsigned(r[0x1bb1e])<<','<<unsigned(r[0x1bbaf])<<','<<action<<','<<restarts<<','<<unsigned(quit);
    out<<','<<unsigned(r[0x1bb1d])<<','<<audio::word(r.data(),0x1bb0c)<<','<<audio::word(r.data(),0x1bbee)<<','<<audio::word(r.data(),0x1bd66)<<','<<unsigned(r[0x1bbed])<<','<<unsigned(r[0x1bb21])<<','<<unsigned(r[0x1bbb8])<<'\n';
}
} // namespace scr::state::replay
