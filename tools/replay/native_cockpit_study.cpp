#include "native_startup.h"
#include "replay_scene.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Artwork review tool only. It adds no selectable view or gameplay behaviour.
// A 432x200 working canvas gives 56 unchanged bitmap pixels' worth of space on
// either side. Presentation at the existing pixel proportions, with a small
// symmetric outer crop, gives an exact 16:9 preview without rescaling the car.
namespace {
using namespace scr::viewer;
constexpr unsigned width=432,height=200,margin=(width-320)/2;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void raw(const std::filesystem::path& path,const std::vector<std::uint32_t>& pixels){
    std::ofstream file(path,std::ios::binary);
    file.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()*4));
    require(bool(file),"Cannot write cockpit study pixels");
}
double braceSlope(const ReplayRenderAssets& assets,bool right){
    // Fit the pale highlight's centreline in the existing brace. Translating
    // an edge column by 1:1 changes its angle at the join and makes it look
    // thicker. All bands must follow the original axis with a fixed profile.
    double sumX=0,sumY=0,sumXX=0,sumXY=0;
    constexpr unsigned columns=14;
    for(unsigned x=0;x<columns;++x){
        unsigned first=height,last=0;
        for(unsigned y=85;y<145;++y)if(assets.background.pixels[y*320+(right?319-x:x)]==12){
            first=std::min(first,y);last=y;
        }
        require(first<=last,"Missing original brace highlight");
        const double centre=(first+last)*0.5;
        sumX+=x;sumY+=centre;sumXX+=x*x;sumXY+=x*centre;
    }
    const double slope=-(columns*sumXY-sumX*sumY)/(columns*sumXX-sumX*sumX);
    require(slope>1.3&&slope<1.4,"Unexpected original brace direction");
    return slope;
}
unsigned blueTop(const ReplayRenderAssets& assets,bool right,unsigned x){
    for(unsigned y=100;y<160;++y){
        const auto index=assets.background.pixels[y*320+(right?319-x:x)];
        if(index==7||index==8)return y;
    }
    throw std::runtime_error("Missing original side interior");
}
std::uint32_t sidePixel(const ReplayRenderAssets& assets,bool right,int x,unsigned y,double slope){
    // Continue the slimmer profile at column 14 along one straight axis. The
    // old outer 14 columns of brace must follow this line too: preserving the
    // original taper there would leave a kink at the old bitmap boundary.
    constexpr int profileColumn=14,profileTop=85,profileBottom=118;
    const unsigned column=right?319-profileColumn:profileColumn;
    const int shift=int(std::lround((profileColumn-x)*slope));
    if(int(y)<profileTop+shift)return 0;
    if(int(y)<profileBottom+shift)
        return 0xff000000|assets.palette[assets.background.pixels[unsigned(int(y)-shift)*320+column]];
    const unsigned blue=x>=10||int(y)>=194-2*x?8:7;
    return 0xff000000|assets.palette[blue];
}
}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("Usage: native_cockpit_study assets output");
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
    auto seed=startNativeRace(argv[1],6);
    const auto assets=decodeReplayRenderAssets(seed.payload.data(),seed.payload.size(),seed.memory->data(),seed.memory->size());
    const double leftSlope=braceSlope(assets,false),rightSlope=braceSlope(assets,true);
    require(leftSlope==rightSlope,"Original brace axes are not symmetric");
    const auto wheels=decodeNativeWheelSprites(seed.memory->data(),seed.memory->size());
    ReplayScene scene;scene.loadNativeState(*seed.memory,seed.payload,{});
    for(unsigned tick=0;tick<=128;++tick){
        const unsigned controls=1|(tick%40<8?16:0)|(tick>100&&tick%40<4?4:0);
        require(scene.stepLive(controls),"Study race ended before its reference frame");
    }
    const auto pixels=scene.renderLive(0,false,false,NativeView::Extended);
    const auto& frame=scene.frames().back();
    std::vector<std::uint32_t> foreground(320*height,replayTransparent);
    composeReplayFrame(assets,foreground);
    composeNativeChains(assets,frame.chainDraws,foreground);
    composeReplayCockpit(assets,frame.cockpit,foreground,true);
    for(unsigned n=0;n<frame.instrumentPixels.size();++n)
        foreground[320*cockpitInstrumentTop+n]=assets.palette[frame.instrumentPixels[n]];
    composeNativeWheelSprites(wheels,frame.wheelSprites,frame.wheelTimeline[0],foreground);
    // Use precisely the native scene's colour at every retained foreground
    // pixel, including the completed top mask and post/brace seam repairs.
    for(unsigned at=0;at<foreground.size();++at)
        foreground[at]=foreground[at]==replayTransparent?0:0xff000000|pixels[at];
    const auto originalForeground=foreground;
    unsigned reworked=0;
    for(unsigned x=0;x<14;++x)for(bool right:{false,true}){
        const unsigned column=right?319-x:x;
        const unsigned end=blueTop(assets,right,x);
        for(unsigned y=0;y<end;++y){const auto at=y*320+column;
            const auto revised=sidePixel(assets,right,int(x),y,right?rightSlope:leftSlope);
            reworked+=revised!=foreground[at];foreground[at]=revised;
        }
    }
    std::vector<std::uint32_t> wide(width*height,0),context(width*height,0);
    unsigned added=0;
    for(unsigned y=0;y<height;++y){
        for(unsigned x=0;x<320;++x){
            wide[y*width+margin+x]=foreground[y*320+x];
            context[y*width+margin+x]=foreground[y*320+x]!=originalForeground[y*320+x]?
                foreground[y*320+x]:0xff000000|pixels[y*320+x];
        }
        for(unsigned distance=1;distance<=margin;++distance){
            const auto left=sidePixel(assets,false,-int(distance),y,leftSlope),right=sidePixel(assets,true,-int(distance),y,rightSlope);
            require(left==right,"The derived side frame/interior is not symmetric");
            wide[y*width+margin-distance]=context[y*width+margin-distance]=left;
            wide[y*width+margin+319+distance]=context[y*width+margin+319+distance]=right;
            if(left)added+=2;
        }
    }
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<320;++x){
        require(wide[y*width+margin+x]==foreground[y*320+x],"Study altered an original foreground pixel");
        if(x>=14&&x<306){
            require(foreground[y*320+x]==originalForeground[y*320+x],"Study altered the main arch or original car artwork");
            require(context[y*width+margin+x]==(0xff000000|pixels[y*320+x]),"Study altered the protected native scene framing");
        }else if(foreground[y*320+x]!=originalForeground[y*320+x])
            require(y<blueTop(assets,x>=306,x>=306?319-x:x),"Study changed artwork outside the side brace strips");
    }
    // Include the native image and alpha layer so display previews can be
    // reproduced independently, without loading a capture or emulated state.
    raw(output/"native-scene-320x200.bgra",pixels);
    raw(output/"native-foreground-320x200.bgra",originalForeground);
    raw(output/"cockpit-layer-432x200.bgra",wide);
    raw(output/"cockpit-context-432x200.bgra",context);
    std::cout<<"{\"status\":\"passed\",\"track\":6,\"updates\":129,\"width\":432,\"height\":200,\"centre_offset\":56,\"added_opaque_pixels\":"
        <<added<<",\"brace_slope\":"<<leftSlope<<",\"extension_profile_height\":33,\"reworked_side_frame_pixels\":"<<reworked
        <<",\"original_car_and_arch_identical\":true,\"protected_scene_identical\":true,\"symmetric_extensions\":true,\"artwork_only\":true}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
