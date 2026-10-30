#include "render/mountain_horizon.h"
#include "physics/motion_transforms.h"
#include <stdexcept>
#include <utility>

namespace scr {
namespace {
int signedWord(std::uint16_t w) noexcept {return w<0x8000?int(w):int(w)-65536;}
std::uint16_t word(int v) noexcept {return static_cast<std::uint16_t>(v);}
int shift(int v,unsigned n) noexcept {const int d=1<<n;return v/d-(v%d<0);}
struct Payload {
    const std::uint8_t* bytes;std::size_t size;
    std::uint8_t byte(std::uint32_t a) const {
        if(a<0xe700||std::size_t(a-0xe700)>=size)throw std::out_of_range("Mountain payload address outside mapped image");
        return bytes[a-0xe700];
    }
    std::uint16_t readWord(std::uint32_t a) const {return std::uint16_t(unsigned(byte(a))*256+byte(a+1));}
    std::uint32_t readLong(std::uint32_t a) const {return (std::uint32_t(readWord(a))<<16)|readWord(a+2);}
};
void recoverBoundary(MountainFace& face,const MountainShape& shape) {
    const auto first=shape.edges[face.edges[0]];face.vertices[0]=first[0];auto next=first[1];
    std::array<bool,4> used{};used[0]=true;
    for(unsigned n=1;n<face.vertexCount;++n){
        face.vertices[n]=next;bool found=false;
        for(unsigned e=1;e<face.vertexCount;++e)if(!used[e]){
            const auto edge=shape.edges[face.edges[e]];
            if(edge[0]==next||edge[1]==next){next=edge[0]==next?edge[1]:edge[0];used[e]=true;found=true;break;}
        }
        if(!found)throw std::invalid_argument("Mountain face edges do not form a closed boundary");
    }
    if(next!=first[0])throw std::invalid_argument("Mountain face boundary is open");
}
} // namespace
MountainHorizonData decodeMountainHorizonData(const std::uint8_t* bytes,std::size_t size) {
    if(!bytes)throw std::invalid_argument("Missing mountain payload");
    const Payload payload{bytes,size};MountainHorizonData result{};
    for(unsigned i=0;i<result.shapes.size();++i){
        auto& shape=result.shapes[i];
        shape.templateAddress=payload.readLong(0x699b8+8*i);shape.variationAddress=payload.readLong(0x699bc+8*i);
        auto cursor=shape.templateAddress,variation=shape.variationAddress;
        const unsigned count=payload.readWord(cursor);cursor+=2;
        if(!count||count>64)throw std::invalid_argument("Invalid original mountain vertex count");
        for(unsigned n=0;n<count;++n){
            std::array<int,2> point{};
            for(unsigned axis=0;axis<2;++axis){
                auto value=payload.readWord(cursor);cursor+=2;
                if(value&0x8000){value=payload.readWord(variation);variation+=2;}
                point[axis]=signedWord(value);
            }
            shape.vertices.push_back({point[0],point[1]});
        }
        const unsigned lines=payload.byte(cursor++);
        if(!lines||lines>64)throw std::invalid_argument("Invalid original mountain line count");
        for(unsigned n=0;n<lines;++n){
            const auto a=payload.byte(cursor++),b=payload.byte(cursor++);
            if((a|b)&1||unsigned(a)/2>=count||unsigned(b)/2>=count)throw std::invalid_argument("Invalid original mountain line vertex");
            shape.edges.push_back({std::uint8_t(a/2),std::uint8_t(b/2)});
        }
        const unsigned faces=payload.byte(cursor++);
        if(!faces||faces>32)throw std::invalid_argument("Invalid original mountain face count");
        for(unsigned n=0;n<faces;++n){
            MountainFace face{};face.color=payload.byte(cursor++);face.vertexCount=payload.byte(cursor++);
            if(face.color>=16||(face.vertexCount!=3&&face.vertexCount!=4))throw std::invalid_argument("Invalid original mountain face");
            for(unsigned e=0;e<face.vertexCount;++e){
                const auto index=payload.byte(cursor++);
                if(index&3||unsigned(index)/4>=lines)throw std::invalid_argument("Invalid original mountain face line");
                face.edges[e]=std::uint8_t(index/4);
            }
            recoverBoundary(face,shape);shape.faces.push_back(face);
        }
    }
    result.placementAddress=payload.readLong(0x69a80);auto cursor=result.placementAddress;
    const unsigned count=payload.byte(cursor++);
    if(!count||count>48)throw std::invalid_argument("Invalid original mountain placement count");
    for(unsigned n=0;n<count;++n){
        MountainPlacement p{payload.byte(cursor),payload.byte(cursor+1)};cursor+=2;
        if(p.shapeIndex>=result.shapes.size())throw std::invalid_argument("Invalid original mountain shape index");
        result.placements.push_back(p);
    }
    return result;
}
MountainHorizonFrame projectMountainHorizon(const MountainHorizonData& data,
    std::uint16_t yaw,const RenderProjectionCamera& camera,int padding) {
    MountainHorizonFrame result{};
    const int extra=(padding+7)/8;
    const auto left=std::uint8_t((yaw>>8)-28-extra);
    const auto horizon=word(-shift(signedWord(camera.viewportCenterY),3));
    for(std::size_t i=data.placements.size();i>0;--i){
        const auto& placement=data.placements[i-1];
        const auto delta=std::uint8_t(placement.horizontalAngle-left);if(delta>=44+2*extra)continue;
        const auto bearing=word((int(delta)-28-extra)*256);
        const auto offset=word(shift(signedWord(word(int(bearing)-(yaw&0xfe))),3));
        const auto& shape=data.shapes.at(placement.shapeIndex);
        MountainInstance instance{std::uint8_t(i-1),placement.shapeIndex,{}};
        for(const auto& point:shape.vertices){
            const auto side=word(point.x+offset),down=word(int(horizon)-point.y);
            const auto x=word(int(multiplyMatrixWord(side,camera.cosRoll))+multiplyMatrixWord(down,camera.sinRoll));
            const auto y=word(int(multiplyMatrixWord(down,camera.cosRoll))-multiplyMatrixWord(side,camera.sinRoll));
            instance.vertices.push_back({shift(signedWord(x),2)+128,shift(signedWord(y),2)+64});
        }
        for(const auto& face:shape.faces){
            MountainScreenPolygon polygon{};polygon.vertexCount=face.vertexCount;polygon.color=face.color;
            polygon.placementIndex=instance.placementIndex;polygon.shapeIndex=instance.shapeIndex;
            for(unsigned n=0;n<face.vertexCount;++n)polygon.vertices[n]=instance.vertices.at(face.vertices[n]);
            result.polygons.push_back(polygon);
        }
        result.instances.push_back(std::move(instance));
    }
    return result;
}
} // namespace scr
