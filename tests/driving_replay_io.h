#pragma once
#include "driving_replay_fixture.h"
#include <fstream>
#include <sstream>
#include <string>
#include <memory>

namespace scr::test::replay {
inline std::unique_ptr<Ram> loadCapture(const char* path) {
    auto r=std::make_unique<Ram>();std::ifstream f(path,std::ios::binary);f.read(reinterpret_cast<char*>(r->data()),r->size());
    if(!f||f.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Expected exactly 512 KiB of captured Chip RAM");return r;
}
inline std::vector<unsigned> loadInputs(const char* path) {
    std::ifstream f(path);std::string line;
    if(!std::getline(f,line)||line!="tick,controls")throw std::runtime_error("Expected ASCII tick,controls CSV header");
    std::vector<unsigned> masks;
    while(std::getline(f,line)) {
        const auto comma=line.find(',');
        if(comma==std::string::npos||comma==0||comma+1==line.size()||line.find(',',comma+1)!=std::string::npos)
            throw std::runtime_error("Malformed replay row");
        for(unsigned i=0;i<line.size();++i)if(i!=comma&&(line[i]<'0'||line[i]>'9'))throw std::runtime_error("Replay values must be decimal integers");
        const auto tick=std::stoul(line.substr(0,comma)),mask=std::stoul(line.substr(comma+1));
        if(tick!=masks.size()||mask>31)throw std::runtime_error("Replay ticks must be contiguous; controls must be 0..31");masks.push_back(unsigned(mask));
    }
    if(masks.empty())throw std::runtime_error("Empty replay");return masks;
}
inline void traceHeader(std::ostream& out) {
    out<<"tick,controls,world_x,world_y,world_z,pitch,yaw,roll,speed,segment,progress,lateral,off_track,boost_fuel,damage,original_instructions\n";
}
inline void traceRow(std::ostream& out,const Ram& r,unsigned tick,unsigned mask,unsigned instructions) {
    out<<tick<<','<<mask;for(auto a:{0x1bcd8,0x1bcdc,0x1bce0})out<<','<<audio::longword(r.data(),a);
    for(auto a:{0x1bce4,0x1bce6,0x1bce8,0x1bd30})out<<','<<audio::word(r.data(),a);
    out<<','<<unsigned(r[0x1bb1c])<<','<<audio::word(r.data(),0x1bb0a)<<','<<audio::word(r.data(),0x1bc5c)<<','<<unsigned(r[0x1bb9c])<<','<<unsigned(r[0x1ca20])<<','<<unsigned(r[0x1bb55])<<','<<instructions<<'\n';
}
} // namespace scr::test::replay
