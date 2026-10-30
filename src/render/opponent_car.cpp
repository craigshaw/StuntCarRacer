#include "render/opponent_car.h"
#include "physics/motion_transforms.h"
#include <algorithm>
#include <stdexcept>

namespace scr {
namespace {
int sw(unsigned v) noexcept {v&=65535;return v<32768?int(v):int(v)-65536;}
std::uint16_t w(int v) noexcept {return std::uint16_t(v);}
int asr(int v,unsigned n) noexcept {const int d=1<<n;return v/d-(v%d<0);}
int add(int a,int b) noexcept {return sw(unsigned(a+b));}
struct Polar {std::uint16_t angle{},distance{};};
Polar polar(std::uint16_t x,std::uint16_t z,const RenderProjectionTables& t){
    const auto ax=w(x&0x8000?-int(x):x),az=w(z&0x8000?-int(z):z);
    if(ax==0x8000||az==0x8000)throw std::out_of_range("Opponent coordinate outside original magnitude domain");
    std::uint16_t angle{},ratio{};
    if(ax==az){ratio=0xffff;angle=0x2000;}
    else if(az<ax){ratio=std::uint16_t((std::uint32_t(az)<<16)/ax);angle=t.angleTable[ratio>>5];
        if(!((x^z)&0x8000))angle=w(-int(angle));angle=w(angle+((x&0x8000)?0xc000:0x4000));
        return {angle,w(int((std::uint32_t(t.magnitudeTable[ratio>>5])*az)>>16)+ax)};
    }else{ratio=std::uint16_t((std::uint32_t(ax)<<16)/az);angle=t.angleTable[ratio>>5];}
    if((x^z)&0x8000)angle=w(-int(angle));if(z&0x8000)angle=w(angle+0x8000);
    return {angle,w(int((std::uint32_t(t.magnitudeTable[ratio>>5])*ax)>>16)+az)};
}
NearTrackPoint project(const OpponentCarInputs& in,const RenderProjectionTables& t,unsigned i,std::uint16_t height){
    auto x=in.relativeX[i],z=in.relativeZ[i];std::uint16_t divisor{};
    if(in.scaledPerspective&0x80){x=w(asr(sw(x),1));z=w(asr(sw(z),1));divisor=w(int(z)+0x3100);z=w(asr(sw(z),1)+0x4900);}
    const auto horizontal=polar(x,z,t);if(!(in.scaledPerspective&0x80))divisor=horizontal.distance;
    const auto side=w(asr(sw(w(int(horizontal.angle)-in.camera.yawResidual)),3));
    auto heightDown=w(asr(sw(w(int(in.camera.heightBaseline)-height)),2));
    if(in.scaledPerspective&0x80)heightDown=multiplyMatrixWord(heightDown,0x4c1b);
    const auto down=w(asr(sw(w(int(polar(heightDown,divisor,t).angle)-in.camera.viewportCenterY)),3));
    // Original0x6250a leaves the angular pair unrotated for negative source
    // heights. Keep that guard even though normal road support is nonnegative.
    if(height&0x8000)return {sw(side),sw(down)};
    const auto rx=w(int(multiplyMatrixWord(side,in.camera.cosRoll))+multiplyMatrixWord(down,in.camera.sinRoll));
    const auto ry=w(int(multiplyMatrixWord(down,in.camera.cosRoll))-multiplyMatrixWord(side,in.camera.sinRoll));
    return {asr(sw(rx),2)+128,asr(sw(ry),2)+64};
}
struct Steps {int ox{},sx{},oy{},sy{},hx{},wx{},hy{},wy{},qx{},ex{},qy{},ey{},tx{},ty{};};
int halfMagnitude(int v,unsigned shift){const auto a=w(v<0?-v:v);return (v<0?-1:1)*int(a>>shift);}
Steps steps(NearTrackPoint a,NearTrackPoint b){
    const int dx=asr(add(b.x,-a.x),1),dy=asr(add(b.y,-a.y),1);
    const int ax=std::min(255,int(w(dx<0?-dx:dx))),ay=std::min(255,int(w(dy<0?-dy:dy)));
    Steps s{};s.ox=dx<0?-ax:ax;s.sy=asr(dx<0?ax:-ax,1);s.oy=dy<0?-ay:ay;s.sx=asr(dy<0?-ay:ay,1);
    s.hx=halfMagnitude(s.ox,1);s.qx=halfMagnitude(s.ox,2);s.ex=halfMagnitude(s.ox,3);
    s.wx=halfMagnitude(s.sx,1);s.hy=halfMagnitude(s.oy,1);s.qy=halfMagnitude(s.oy,2);s.ey=halfMagnitude(s.oy,3);
    s.wy=halfMagnitude(s.sy,1);s.tx=add(a.x,dx);s.ty=add(a.y,dy);return s;
}
NearTrackPoint translated(const Steps& s,int x,int y,unsigned bias){return {add(add(x,s.tx),bias&1?1:0),add(add(y,s.ty),bias&2?1:0)};}
NearTrackEdge allocateEdge(OpponentCarFrame& f,NearTrackPoint a,NearTrackPoint b){
    if(f.lineBytes>=trackPresentationLineLimit(f.horizontalPadding,f.topPadding,f.bottomPadding)){NearTrackEdge e{};e.clipFlags=0x80;return e;}
    auto e=makeNearTrackEdge(a,b,f.horizontalPadding,f.topPadding,f.bottomPadding);e.originQueueOffset=0xffff;f.lineBytes=std::uint16_t(f.lineBytes+e.storageBytes);return e;
}
void quad(OpponentCarFrame& f,const Steps& s,int x,int y,unsigned offset){
    f.vertices[0]=translated(s,add(x,-s.sx),add(y,-s.sy),2);
    f.vertices[1]=translated(s,x,y,0);
    f.vertices[2]=translated(s,add(x,s.hx),add(y,s.hy),1);
    f.vertices[3]=translated(s,add(add(x,-s.sx),s.hx),add(add(y,-s.sy),s.hy),3);
    for(unsigned n=0;n<4;++n)f.edges[(offset/4)+n]=allocateEdge(f,f.vertices[n],f.vertices[(n+1)%4]);
}
void edge(OpponentCarFrame& f,unsigned offset,unsigned a,unsigned b){f.edges[offset/4]=allocateEdge(f,f.vertices[a],f.vertices[b]);}
void command(OpponentCarFrame& f,unsigned color,std::array<unsigned,4> offsets){
    unsigned clip=15;for(auto n:offsets){const auto& e=f.edges[n/4];if(!e.present)return;clip&=e.clipFlags;}if(clip)return;
    NearTrackDrawCommand c{};c.kind=NearTrackCommandKind::fill;c.surface=NearTrackSurface::opponent;c.color=std::uint8_t(color);
    c.originQueueOffset=c.firstQueueOffset=0x5e0;for(auto n:offsets)c.edges.push_back(f.edges[n/4]);f.commands.push_back(std::move(c));
}
} // namespace
OpponentCarFrame buildOpponentCarFrame(const OpponentCarInputs& in,const RenderProjectionTables& tables){
    OpponentCarFrame f{};f.horizontalPadding=in.horizontalPadding;f.topPadding=in.topPadding;f.bottomPadding=in.bottomPadding;f.lineBytes=in.lineBytes;if((in.aheadFlag&0x80)||in.distance<10||sw(in.distance)>=0xc80)return f;f.admitted=true;
    const auto clamp=[&](unsigned i,int bound){f.clampedHeights[i]=w(std::min(sw(in.footprint[i]),sw(w(bound+0x50))));};
    clamp(0,in.bounds[0]);clamp(2,in.bounds[1]);const int slope=asr(add(in.bounds[1],-int(in.bounds[0])),1);
    clamp(1,add(in.bounds[2],-slope));clamp(3,add(in.bounds[2],slope));
    for(unsigned n=0;n<4;++n){f.footprintPoints[n]=project(in,tables,n,in.footprint[n]);f.clampedPoints[n]=project(in,tables,n,f.clampedHeights[n]);f.vertices[12+n]=f.clampedPoints[n];}
    auto s=steps(f.footprintPoints[0],f.footprintPoints[2]);
    quad(f,s,-s.ox,-s.oy,0x40);quad(f,s,add(s.ox,-s.hx),add(s.oy,-s.hy),0x50);
    f.vertices[4]=translated(s,add(-s.hx,-s.wx),add(-s.hy,-s.wy),2);
    f.vertices[5]=translated(s,add(add(s.sx,-s.qx),-s.ex),add(add(s.sy,-s.qy),-s.ey),0);
    f.vertices[6]=translated(s,add(add(s.sx,s.qx),s.ex),add(add(s.sy,s.qy),s.ey),1);
    f.vertices[7]=translated(s,add(s.hx,-s.wx),add(s.hy,-s.wy),3);
    for(unsigned n=0;n<4;++n)edge(f,0x10+n*4,4+n,4+(n+1)%4);
    for(unsigned n:{12u,14u}){f.vertices[n].x=add(f.vertices[n].x,-s.sx);f.vertices[n].y=add(f.vertices[n].y,-add(s.sy,-1));}
    s=steps(f.footprintPoints[1],f.footprintPoints[3]);
    f.vertices[8]=translated(s,add(-s.hx,-s.wx),add(-s.hy,-s.wy),2);
    f.vertices[9]=translated(s,-s.hx,-s.hy,0);f.vertices[10]=translated(s,s.hx,s.hy,1);
    f.vertices[11]=translated(s,add(s.hx,-s.wx),add(s.hy,-s.wy),3);
    for(unsigned n=0;n<4;++n)edge(f,0x20+n*4,8+n,8+(n+1)%4);
    edge(f,0x30,4,8);edge(f,0x34,7,11);edge(f,0x38,5,9);edge(f,0x3c,6,10);
    quad(f,s,-s.ox,-s.oy,0x60);quad(f,s,add(s.ox,-s.hx),add(s.oy,-s.hy),0x70);
    for(unsigned n:{13u,15u}){f.vertices[n].x=add(f.vertices[n].x,-s.sx);f.vertices[n].y=add(f.vertices[n].y,-s.sy);}
    edge(f,0x80,12,14);edge(f,0x84,12,13);edge(f,0x88,13,15);edge(f,0x8c,14,15);
    if(!in.curveSmoothing&&sw(in.roadOffset)>=0x1c&&sw(in.roadOffset)<=0xe4)command(f,5,{0x80,0x84,0x88,0x8c});
    command(f,0,{0x60,0x64,0x68,0x6c});command(f,0,{0x70,0x74,0x78,0x7c});
    command(f,12,{0x20,0x38,0x10,0x30});command(f,12,{0x18,0x3c,0x28,0x34});
    command(f,10,{0x10,0x14,0x18,0x1c});command(f,15,{0x38,0x24,0x3c,0x14});command(f,9,{0x30,0x1c,0x34,0x2c});
    command(f,0,{0x40,0x44,0x48,0x4c});command(f,0,{0x50,0x54,0x58,0x5c});return f;
}
bool consumeOpponentCarPainterSlot(OpponentCarPainterSlot slot,std::uint8_t relative,std::uint16_t current,std::uint16_t& marker) noexcept{
    const bool match=slot==OpponentCarPainterSlot::ahead?relative&&!(relative&0x80):slot==OpponentCarPainterSlot::behind?(relative&0x80)!=0:relative==0;
    if(!match||sw(current)>sw(marker))return false;marker=0xffff;return true;
}
} // namespace scr
