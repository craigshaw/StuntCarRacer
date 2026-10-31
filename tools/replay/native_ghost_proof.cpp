#include "race/race_scene.h"
#include "state/replay_io.h"
#include "state/opponent_driving_adapter.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace scr;
using namespace scr::runtime;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::uint64_t hashBytes(const std::uint8_t* bytes,std::size_t size){
    std::uint64_t hash=14695981039346656037ULL;
    for(std::size_t n=0;n<size;++n){hash^=bytes[n];hash*=1099511628211ULL;}return hash;
}
std::uint64_t framesHash(const std::vector<RaceFrame>& frames){
    std::uint64_t hash=0;
    for(const auto& frame:frames){
        for(auto p:frame.pose.worldPosition)hash=hash*1099511628211ULL+p;
        for(auto p:{frame.pose.pitch,frame.pose.yaw,frame.pose.roll})hash=hash*1099511628211ULL+p;
        for(const auto& command:frame.nearTrack.commands){
            hash=hash*1099511628211ULL+command.color;
            for(const auto& edge:command.edges){
                for(int p:{edge.from.x,edge.from.y,edge.to.x,edge.to.y})hash=hash*1099511628211ULL+std::uint64_t(p);
                for(double d:{edge.from.inverseDepth,edge.to.inverseDepth}){std::uint64_t bits;std::memcpy(&bits,&d,sizeof(bits));hash=hash*1099511628211ULL+bits;}
            }
        }
    }return hash;
}
GhostCarPose sample(const std::vector<RaceFrame>& frames,double position,const TrigTable& trig){
    position=std::clamp(position,0.0,double(frames.size()-1));
    const auto index=unsigned(position);const auto& a=frames[index];
    if(index+1==frames.size())return makeGhostCarPose(a.pose);
    const auto& b=frames[index+1];
    if(!canInterpolatePresentation(a.restarts,b.restarts,a.action,b.action))return makeGhostCarPose(a.pose);
    return makeGhostCarPose(interpolatePresentation(a,b,position-index,trig).pose);
}
std::string viewName(NativeView view){
    switch(view){case NativeView::Original:return "original";case NativeView::Extended:return "extended";
        case NativeView::Widescreen:return "wide";default:return "ultrawide";}
}
void prefixReference(const std::filesystem::path& input,const std::filesystem::path& output,unsigned rows){
    std::ifstream in(input);std::ofstream out(output);std::string line;
    require(bool(in)&&bool(out),"Cannot open bounded reference trace");
    for(unsigned n=0;n<=rows;++n){require(bool(std::getline(in,line)),"Reference trace ends early");out<<line<<'\n';}
}
double roadHeight(const TrackMesh& mesh,const RenderWorldPoint& point){
    double best=point.y,error=1e9;
    for(const auto& face:mesh.faces)for(unsigned n=1;n<3;++n){
        const auto& a=mesh.vertices[face.indices[0]];const auto& b=mesh.vertices[face.indices[n]];const auto& c=mesh.vertices[face.indices[n+1]];
        const auto denominator=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);if(std::abs(denominator)<1e-8)continue;
        const auto wa=((b.z-c.z)*(point.x-c.x)+(c.x-b.x)*(point.z-c.z))/denominator;
        const auto wb=((c.z-a.z)*(point.x-c.x)+(a.x-c.x)*(point.z-c.z))/denominator,wc=1-wa-wb;
        if(std::min({wa,wb,wc})<-0.00001)continue;
        const auto y=wa*a.y+wb*b.y+wc*c.y;
        if(std::abs(y-point.y)<error){best=y;error=std::abs(y-point.y);}
    }return best;
}
struct PixelBounds {
    int left=10000,right=-1,top=10000,bottom=-1;
    void add(int x,int y){left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}
    int width()const{return right<left?0:right-left+1;}
    int height()const{return bottom<top?0:bottom-top+1;}
};
void ppm(const std::filesystem::path& path,const std::vector<std::uint32_t>& pixels){
    std::ofstream out(path,std::ios::binary);out<<"P6\n320 200\n255\n";
    for(auto p:pixels)for(unsigned shift:{16u,8u,0u})out.put(char((p>>shift)&255));
    require(bool(out),"Cannot write proportion comparison");
}
struct ProportionStatistics {unsigned cases{},maximumWidthDifference{},maximumRearHeightDifference{};};
ProportionStatistics proportions(const std::filesystem::path& output,const RenderProjectionTables& tables,
    std::array<std::uint32_t,16> palette,double width,double wheelbase){
    ProportionStatistics stats;
    const auto directory=output/"proportions";std::filesystem::create_directories(directory);
    std::ofstream csv(directory/"measurements.csv");
    csv<<"angle,distance,reference_width,ghost_width,reference_height,ghost_height,reference_body_width,ghost_body_width\n";
    RenderProjectionCamera camera;camera.worldPosition={512u<<16,47u<<16,512u<<16};
    camera.rotatedXUnits=camera.rotatedZUnits=512*16;camera.heightBaseline=47*32;camera.cosRoll=camera.cosYaw=0x7fff;
    constexpr std::uint32_t background=0x203040;palette[7]=palette[13]=background;
    for(unsigned angle:{0u,0x1555u,0x2000u})for(unsigned distance:{32u,48u,64u,96u}){
        const auto radians=double(angle)*6.28318530717958647692/65536;
        const auto sine=std::sin(radians),cosine=std::cos(radians);
        OpponentCarInputs reference;reference.camera=camera;reference.distance=std::uint16_t(distance*16);
        reference.roadOffset=128;reference.curveSmoothing=0x80;reference.bounds.fill(40*32);reference.footprint.fill(40*32+0x50);
        // Recovered footprint order: front-left, rear-left, front-right, rear-right.
        for(unsigned n=0;n<4;++n){const auto side=n<2?-width/2:width/2,forward=n%2?-wheelbase/2:wheelbase/2;
            reference.relativeX[n]=std::uint16_t(int(std::lround((side*cosine+forward*sine)*16)));
            reference.relativeZ[n]=std::uint16_t(int(std::lround((distance+forward*cosine-side*sine)*16)));
        }
        const auto original=buildOpponentCarFrame(reference,tables);require(original.admitted,"Reference car not admitted");
        NearTrackPresentation commands;commands.coveredSegments.fill(true);commands.commands=original.commands;
        SoftwareRenderer referenceRenderer;std::vector<std::uint32_t> referencePixels(320*200,background);
        referenceRenderer.drawFrame({}, {}, {},camera,tables,palette,referencePixels,false,&commands);
        GhostCarRenderer ghostRenderer;GhostCarStyle style;style.opacity=1;style.occlusion=false;style.fadeNear=0;style.fadeFar=1;
        std::vector<std::uint32_t> ghostPixels(320*200,background);std::vector<std::uint8_t> coverage(256*128,0);
        ghostRenderer.draw({{512,38.75,512.0+distance},0,std::uint16_t(angle),0},style,{}, {},camera,tables,ghostPixels,coverage);
        PixelBounds refBounds,ghostBounds,refBody,ghostBody;
        for(int y=0;y<128;++y)for(int x=0;x<256;++x){const auto at=std::size_t((y+16)*320+x+32);
            if(referenceRenderer.worldCoverage()[y*256+x]==4){refBounds.add(x,y);if(referencePixels[at]!=palette[0])refBody.add(x,y);}
            if(ghostPixels[at]!=background){ghostBounds.add(x,y);if(ghostPixels[at]!=0x286779)ghostBody.add(x,y);}
        }
        csv<<angle<<','<<distance<<','<<refBounds.width()<<','<<ghostBounds.width()<<','<<refBounds.height()<<','<<ghostBounds.height()<<','<<refBody.width()<<','<<ghostBody.width()<<'\n';
        ++stats.cases;stats.maximumWidthDifference=std::max(stats.maximumWidthDifference,unsigned(std::abs(ghostBounds.width()-refBounds.width())));
        if(!angle)stats.maximumRearHeightDifference=std::max(stats.maximumRearHeightDifference,unsigned(std::abs(ghostBounds.height()-refBounds.height())));
        const auto name=std::to_string(angle)+"-"+std::to_string(distance);
        ppm(directory/(name+"-reference.ppm"),referencePixels);ppm(directory/(name+"-ghost.ppm"),ghostPixels);
        require(std::abs(ghostBounds.width()-refBounds.width())<=3,"Ghost width differs from recovered reference bounds");
        if(!angle)require(std::abs(ghostBounds.height()-refBounds.height())<=4,"Ghost rear height differs from recovered reference bounds");
    }return stats;
}
}
int main(int argc,char** argv)try{
    if(argc<6||argc>7)throw std::runtime_error("Usage: native_ghost_proof chip payload inputs original-trace output [--preview]");
    const bool preview=argc==7&&std::string(argv[6])=="--preview";
    if(argc==7&&!preview)throw std::runtime_error("Unknown proof option");
    const std::filesystem::path output=argv[5];std::filesystem::create_directories(output);
    auto seed=scr::state::replay::loadCapture(argv[1]);
    require((*seed)[0x1ca33]!=5,"First ghost proof requires a static circuit");
    const auto controls=scr::state::replay::loadInputs(argv[3]);
    require(controls.size()>=100,"Proof needs at least 100 recorded updates");
    prefixReference(argv[4],output/"bounded-original.csv",unsigned(controls.size()));
    auto scene=std::make_unique<RaceScene>();
    scene->load(argv[1],argv[2],argv[3],output/"bounded-original.csv",output/"native.csv");
    std::ifstream payloadFile(argv[2],std::ios::binary);
    const std::vector<std::uint8_t> payload{std::istreambuf_iterator<char>(payloadFile),std::istreambuf_iterator<char>()};
    const auto physics=std::make_unique<PhysicsData>(scr::state::physics::readData(*seed));
    const auto& trig=physics->trig;const auto& frames=scene->frames();
    ProportionStatistics proportionStats;
    {
        // A flat-support synthetic observation of the recovered opponent's
        // footprint generation. It uses the supplied track descriptors/tables
        // on private typed state and never replaces the live race state.
        auto data=std::make_unique<OpponentDrivingData>(scr::state::opponent::readData(*seed));
        auto car=scr::state::opponent::readState(*seed);car.wheelPosition.fill(1280);car.actionTimer=0;car.segmentOffsets.fill(0);
        setupOpponentTrackGeometry(*data,scr::state::opponent::readInputs(*seed),car);
        const auto signedWord=[](unsigned v){v&=65535;return v<32768?int(v):int(v)-65536;};
        const auto span=[&](unsigned a,unsigned b){return std::hypot(double(signedWord(unsigned(car.coordinatesX[a]-car.coordinatesX[b]))),double(signedWord(unsigned(car.coordinatesZ[a]-car.coordinatesZ[b]))))/16;};
        std::ofstream dimensions(output/"opponent-footprint.json");
        dimensions<<"{\"source\":\"setupOpponentTrackGeometry, flat wheel supports, retained source track\",\"width_world\":"<<span(5,7)<<",\"footprint_length_world\":"<<span(5,8)<<",\"distance_factor\":"<<unsigned(car.distanceFactor)<<"}\n";
        const auto assets=decodeRaceRenderAssets(payload.data(),payload.size(),seed->data(),seed->size());
        RenderProjectionTables tables;tables.angleTable=data->track.angleTable;tables.magnitudeTable=data->track.magnitudeTable;
        proportionStats=proportions(output,tables,assets.palette,span(5,7),span(5,8));
    }
    const auto before=framesHash(frames);
    std::ofstream trajectory(output/"trajectory.csv");
    trajectory<<"tick,x,y,z,pitch,yaw,roll,restarts,road_y,clearance\n";
    double lowY=1e9,highY=-1e9;unsigned highYIndex=0;
    for(unsigned n=0;n<frames.size();++n){const auto p=makeGhostCarPose(frames[n].pose);
        const auto height=roadHeight(scene->mesh(),p.position);
        trajectory<<n<<','<<p.position.x<<','<<p.position.y<<','<<p.position.z<<','<<p.pitch<<','<<p.yaw<<','<<p.roll<<','<<frames[n].restarts<<','<<height<<','<<p.position.y-height<<'\n';
        lowY=std::min(lowY,p.position.y);if(p.position.y>highY){highY=p.position.y;highYIndex=n;}
    }
    trajectory.close();
    unsigned queries=0,visibleFrames=0,partialFrames=0,hiddenFrames=0,endpointChecks=0;
    std::uint64_t visiblePixels=0,hiddenPixels=0,missingDepth=0,ghostMissingDepth=0;
    double bestPosition=24;unsigned bestPixels=0;
    double renderMilliseconds=0,maxRenderMilliseconds=0;
    const auto views={NativeView::Original,NativeView::Extended,NativeView::Widescreen,NativeView::Ultrawide};
    GhostCarStyle style,disabled;disabled.opacity=0;
    std::ofstream samples(output/"samples.csv");
    samples<<"view,position,ghost_position,silhouette,visible,occluded,track_pixels_without_depth,ghost_pixels_without_track_depth\n";
    for(auto view:views){
        CameraViewSettings settings;settings.view=view;
        for(unsigned tick=24;tick+13<frames.size();tick+=24)for(double fraction:{0.0,0.5}){
            const double position=tick+fraction;const auto ghost=sample(frames,position+12,trig);
            const auto baseline=scene->renderView(position,settings);
            require(scene->renderGhostView(position,settings,ghost,disabled)==baseline,"Zero-opacity query changed baseline pixels");++endpointChecks;
            const auto started=std::chrono::steady_clock::now();
            const auto pixels=scene->renderGhostView(position,settings,ghost,style);const auto stats=scene->ghostStatistics();++queries;
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            renderMilliseconds+=elapsed;maxRenderMilliseconds=std::max(maxRenderMilliseconds,elapsed);
            visibleFrames+=stats.visiblePixels>0;partialFrames+=stats.visiblePixels>0&&stats.occludedPixels>0;hiddenFrames+=stats.silhouettePixels>0&&!stats.visiblePixels;
            visiblePixels+=stats.visiblePixels;hiddenPixels+=stats.occludedPixels;missingDepth+=stats.trackPixelsWithoutDepth;
            ghostMissingDepth+=stats.ghostPixelsWithoutTrackDepth;
            samples<<viewName(view)<<','<<position<<','<<position+12<<','<<stats.silhouettePixels<<','<<stats.visiblePixels<<','<<stats.occludedPixels<<','<<stats.trackPixelsWithoutDepth<<','<<stats.ghostPixelsWithoutTrackDepth<<'\n';
            if(view==NativeView::Widescreen&&fraction==0.5&&stats.visiblePixels>bestPixels&&stats.visiblePixels<6000){bestPixels=stats.visiblePixels;bestPosition=position;}
            require(scene->renderGhostView(position,settings,ghost,style)==pixels,"Repeated ghost query changed pixels");
            require(scene->renderView(position,settings)==baseline,"Ghost query changed later baseline render");
            if(fraction==0.5&&(tick%48==0||tick==120||tick==360)){
                const auto stem=viewName(view)+"-"+std::to_string(tick);
                scene->renderView(position,settings);scene->saveBitmap(output/(stem+"-baseline.bmp"));
                scene->renderGhostView(position,settings,ghost,style);scene->saveBitmap(output/(stem+"-ghost.bmp"));
                auto unmasked=style;unmasked.occlusion=false;
                scene->renderGhostView(position,settings,ghost,unmasked);scene->saveBitmap(output/(stem+"-unmasked.bmp"));
            }
        }
    }
    require(visibleFrames>0&&partialFrames>0,"Trajectory did not exercise visible and partially occluded ghost frames");
    // Exercise every retained update and quarter-update presentation position,
    // including initial history and discontinuities, in every aperture.
    unsigned coverageQueries=0;std::uint64_t sweepMissing=0;
    std::ofstream gaps(output/"depth-gaps.csv");gaps<<"view,position,track_missing,ghost_missing\n";
    for(auto view:views){CameraViewSettings settings;settings.view=view;
        for(unsigned tick=0;tick<frames.size();++tick)for(double fraction:{0.0,0.25,0.5,0.75}){
            const auto position=std::min(tick+fraction,double(frames.size()-1));
            scene->renderGhostView(position,settings,sample(frames,position+12,trig));const auto stats=scene->ghostStatistics();++coverageQueries;
            sweepMissing+=stats.trackPixelsWithoutDepth;
            if(stats.trackPixelsWithoutDepth||stats.ghostPixelsWithoutTrackDepth)
                gaps<<viewName(view)<<','<<position<<','<<stats.trackPixelsWithoutDepth<<','<<stats.ghostPixelsWithoutTrackDepth<<'\n';
        }
    }
    gaps.close();require(sweepMissing==0,"Painted-track depth coverage gap; inspect depth-gaps.csv");
    for(auto view:views){CameraViewSettings settings;settings.view=view;
        const auto ghost=sample(frames,bestPosition+12,trig);
        scene->renderGhostView(bestPosition,settings,ghost);scene->saveBitmap(output/("hero-"+viewName(view)+".bmp"));
        scene->renderView(bestPosition,settings);scene->saveBitmap(output/("hero-"+viewName(view)+"-baseline.bmp"));
        auto unmasked=style;unmasked.occlusion=false;
        scene->renderGhostView(bestPosition,settings,ghost,unmasked);scene->saveBitmap(output/("hero-"+viewName(view)+"-unmasked.bmp"));
    }
    // Query retained frames in reverse order as well as forwards.
    for(unsigned tick=unsigned(frames.size())-16;tick>24;tick-=24){
        CameraViewSettings settings;settings.view=NativeView::Widescreen;
        const auto ghost=sample(frames,tick+12.5,trig);
        const auto pixels=scene->renderGhostView(tick+0.5,settings,ghost);
        scene->renderInterpolated(0);
        require(scene->renderGhostView(tick+0.5,settings,ghost)==pixels,"Reverse seek changed ghost pixels");++queries;
    }
    require(framesHash(frames)==before,"Ghost rendering changed retained poses or scene commands");
    // Independently evolve identical live states; only one receives ghost/view
    // queries. Comparing all 512 KiB after further updates includes RNG and
    // catches changes which a current-frame pixel comparison would miss.
    auto untouched=std::make_unique<RaceScene>(),queried=std::make_unique<RaceScene>();
    untouched->loadNativeState(*seed,payload,{});queried->loadNativeState(*seed,payload,{});
    unsigned liveChecks=0;
    for(unsigned tick=0;tick<std::min(240u,unsigned(controls.size()));++tick){
        require(untouched->stepLive(controls[tick].controls)==queried->stepLive(controls[tick].controls),"Ghost queries changed race lifetime");
        if(tick%12==0)for(auto view:views){CameraViewSettings settings;settings.view=view;
            const auto ghost=sample(frames,tick+12.5,trig);
            const auto baseline=untouched->renderView(0.5,settings);
            require(queried->renderView(0.5,settings)==baseline,"Ghost queries changed future live history");
            queried->renderGhostView(0.5,settings,ghost);++liveChecks;
        }
    }
    untouched->quitLive();queried->quitLive();
    auto a=std::make_unique<scr::state::replay::Ram>(),b=std::make_unique<scr::state::replay::Ram>();
    untouched->copyLiveExit(*a);queried->copyLiveExit(*b);
    require(*a==*b,"Ghost rendering changed simulation memory or RNG");
    unsigned previewFrames=0;
    if(preview){
        CameraViewSettings settings;settings.view=NativeView::Widescreen;
        for(unsigned clip=0;clip<2;++clip){
            const auto directory=output/(clip?"preview-jump":"preview");std::filesystem::create_directories(directory);
            const double start=std::clamp(clip?double(highYIndex)-40:bestPosition-12,0.0,double(frames.size()-73));
            std::ofstream timeline(directory/"timeline.csv");timeline<<"frame,player_tick,ghost_tick,visible,occluded\n";
            for(unsigned n=0;n<120;++n){const auto position=start+n*0.5;
                scene->renderGhostView(position,settings,sample(frames,position+12,trig));
                auto name=std::to_string(1000+n).substr(1);scene->saveBitmap(directory/(name+".bmp"));++previewFrames;
                const auto stats=scene->ghostStatistics();
                timeline<<n<<','<<position<<','<<position+12<<','<<stats.visiblePixels<<','<<stats.occludedPixels<<'\n';
            }
        }
    }
    require(framesHash(frames)==before,"Preview changed retained presentation history");
    std::ofstream report(output/"results.json");
    report<<"{\n  \"status\":\"passed\",\n  \"track\":"<<unsigned((*seed)[0x1ca33])<<",\n  \"recorded_frames\":"<<frames.size()
        <<",\n  \"source_kind\":\"authored native controls from retained captured entry state\",\n  \"ghost_time_offset_ticks\":12,\n  \"render_queries\":"<<queries
        <<",\n  \"visible_frames\":"<<visibleFrames<<",\n  \"partially_occluded_frames\":"<<partialFrames<<",\n  \"fully_occluded_frames\":"<<hiddenFrames
        <<",\n  \"visible_pixels\":"<<visiblePixels<<",\n  \"occluded_pixels\":"<<hiddenPixels<<",\n  \"track_pixels_without_depth\":"<<missingDepth
        <<",\n  \"ghost_pixels_without_track_depth\":"<<ghostMissingDepth
        <<",\n  \"complete_depth_coverage_queries\":"<<coverageQueries<<",\n  \"complete_depth_coverage_missing_pixels\":"<<sweepMissing
        <<",\n  \"static_depth_boundary_gate_passed\":true,\n  \"proportion_comparisons\":"<<proportionStats.cases
        <<",\n  \"maximum_reference_width_difference_pixels\":"<<proportionStats.maximumWidthDifference
        <<",\n  \"maximum_reference_rear_height_difference_pixels\":"<<proportionStats.maximumRearHeightDifference
        <<",\n  \"depth_source\":\"original ordered painter spans and projected endpoint depth\",\n  \"production_occlusion_accepted\":false"
        <<",\n  \"average_masked_render_ms\":"<<renderMilliseconds/endpointChecks<<",\n  \"maximum_masked_render_ms\":"<<maxRenderMilliseconds
        <<",\n  \"zero_opacity_baseline_checks\":"<<endpointChecks<<",\n  \"live_isolation_queries\":"<<liveChecks<<",\n  \"full_simulation_bytes_compared\":"<<a->size()
        <<",\n  \"simulation_memory_hash\":"<<hashBytes(a->data(),a->size())<<",\n  \"simulation_and_rng_unchanged\":true,\n  \"retained_poses_and_commands_unchanged\":true"
        <<",\n  \"recorded_height_min\":"<<lowY<<",\n  \"recorded_height_max\":"<<highY<<",\n  \"hero_position\":"<<bestPosition<<",\n  \"preview_frames\":"<<previewFrames
        <<",\n  \"limitations\":[\"Static track proof; no Draw Bridge policy implemented\",\"Endpoint depth interpolation follows the original painter rather than asserting exact physical visibility\",\"Same recorded trajectory sampled 12 ticks ahead for visibility, not a separate faster lap\",\"Native exterior calibrated to recovered opponent silhouettes; not a recovered 3D player car\"]\n}\n";
    require(bool(report),"Cannot write proof report");std::cout<<"Ghost proof passed; report: "<<(output/"results.json").string()<<'\n';
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
