// Native coverage regression. Read the original local geometry instead of
// embedding mountain assets. This test does not claim original pixel equality.
#include "software_renderer.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

int main(int argc,char** argv)try {
    if(argc!=2)throw std::runtime_error("Usage: software_renderer_tests payload.bin");
    std::ifstream input(argv[1],std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read original payload");
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    auto data=scr::decodeMountainHorizonData(payload.data(),payload.size());
    data.placements={{0,1}};
    scr::RenderProjectionCamera camera{};camera.cosRoll=0x7fff;
    auto mountains=scr::projectMountainHorizon(data,0,camera);
    if(mountains.polygons.size()!=1||mountains.polygons[0].vertexCount!=4)
        throw std::runtime_error("Original concave mountain fixture changed");
    const auto color=mountains.polygons[0].color;
    std::array<std::uint32_t,16> palette{};
    for(unsigned n=0;n<palette.size();++n)palette[n]=0x123400+n;
    const scr::TrackMesh road{};const std::vector<scr::TrackSideFace> sides{};
    scr::RenderProjectionTables tables{};scr::SoftwareReplayRenderer renderer;
    std::vector<std::uint32_t> pixels(320*200),background(320*200);
    renderer.drawFrame(road,sides,{},camera,tables,palette,background);
    renderer.drawFrame(road,sides,mountains,camera,tables,palette,pixels);
    const auto pixel=[](unsigned x,unsigned y){return (y+16)*320+x+32;};
    // This pixel center lies in the notch. A vertex0 triangle fan incorrectly
    // paints it; a point lower and to the right is inside the same silhouette.
    if(pixels[pixel(149,60)]!=background[pixel(149,60)])
        throw std::runtime_error("Concave mountain notch was filled");
    if(pixels[pixel(170,60)]!=palette[color])
        throw std::runtime_error("Mountain interior was not filled");
    const auto forward=pixels;
    std::reverse(mountains.polygons[0].vertices.begin(),mountains.polygons[0].vertices.end());
    renderer.drawFrame(road,sides,mountains,camera,tables,palette,pixels);
    if(pixels!=forward)throw std::runtime_error("Mountain coverage depends on polygon winding");
    const auto word=[&](unsigned address){const auto offset=address-0xe700;
        return std::uint16_t((unsigned(payload.at(offset))<<8)|payload.at(offset+1));};
    for(unsigned n=0;n<2048;++n){tables.angleTable[n]=word(0x1cc46+2*n);tables.magnitudeTable[n]=word(0x1dc46+2*n);}
    camera={};camera.cosRoll=camera.cosYaw=0x7fff;
    camera.worldPosition={512u<<16,40u<<16,512u<<16};
    camera.rotatedXUnits=camera.rotatedZUnits=512*16;camera.heightBaseline=43*32;
    scr::TrackMesh flat;
    flat.vertices={{500,40,500},{500,40,600},{524,40,600},{524,40,500}};
    flat.faces.push_back({{0,1,2,3},0,0});
    renderer.drawFrame(flat,sides,{},camera,tables,palette,pixels);
    // A flat road crossing the camera must cover the lower view. Projecting
    // only the near-plane corner pair wrongly leaves these pixels as ground.
    if(pixels[pixel(128,127)]!=palette[1]||pixels[pixel(1,127)]!=palette[1])
        throw std::runtime_error("Angular near-plane projection left a road gap");
    if(pixels[pixel(128,32)]!=palette[7])throw std::runtime_error("Road coverage escaped above the horizon");
    std::cout<<"{\"total_cases\":6,\"mismatches\":0,\"concave_notch_preserved\":true,\"winding_independent\":true,\"near_road_gap_absent\":true,\"original_pixel_equality_claimed\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
