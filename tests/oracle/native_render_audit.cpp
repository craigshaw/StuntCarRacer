#include "assets/native_startup.h"
#include "race/native_race_initialization.h"
#include "menus/native_menu_flow.h"
#include "race/race_scene.h"
#include "presentation/camera_view.h"
#include "track/track_initialization.h"
#include "state/track_mesh_adapter.h"
#include "driving_replay_io.h"
#include "m68k.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
using namespace scr;
using namespace scr::runtime;
NativeMemory ram{};
std::array<std::uint8_t,4096> stack{};
constexpr unsigned stackBase=0x8f000,stackTop=0x90000,sentinel=0xa0000;
unsigned pc{},instructions{};bool stopped{},invalid{};
std::uint64_t horizonInstructions{},horizonCases{},horizonPixels{},coverageCases{},coveragePixels{},layoutCases{};
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
unsigned read(unsigned a){if(a<ram.size())return ram[a];if(a>=stackBase&&a<stackTop)return stack[a-stackBase];invalid=true;return 0;}
void write(unsigned a,unsigned v){if(a<ram.size()){ram[a]=std::uint8_t(v);return;}if(a>=stackBase&&a<stackTop){stack[a-stackBase]=std::uint8_t(v);return;}invalid=true;}
void hook(unsigned a){pc=a;if(a==0x678d0){stopped=true;m68k_end_timeslice();return;}
    if(++instructions>10000||a<0xe700||a>=0x724dc||(a&1)){invalid=true;m68k_end_timeslice();}}
void originalHorizon(){
    stopped=invalid=false;instructions=0;stack.fill(0);m68k_pulse_reset();
    for(unsigned i=0;i<15;++i)m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0+i),0);
    m68k_set_reg(M68K_REG_SR,0x2700);m68k_set_reg(M68K_REG_SP,stackTop-4);m68k_set_reg(M68K_REG_PC,0x67888);
    m68k_execute(1000000);require(stopped&&!invalid,"Original horizon projection failed PC="+std::to_string(pc));horizonInstructions+=instructions;
}
int sw(unsigned value){value&=65535;return value<32768?int(value):int(value)-65536;}
unsigned word(unsigned a){return (unsigned(ram[a])<<8)|ram[a+1];}
void word(unsigned a,unsigned v){ram[a]=std::uint8_t(v>>8);ram[a+1]=std::uint8_t(v);}
struct Hash {
    std::uint64_t value=14695981039346656037ULL;
    void bytes(const void* source,std::size_t count){const auto* p=static_cast<const std::uint8_t*>(source);while(count--){value^=*p++;value*=1099511628211ULL;}}
    template<class T>void pod(const T& item){bytes(&item,sizeof(item));}
};
std::uint64_t pixelsHash(const std::vector<std::uint32_t>& pixels){Hash h;h.bytes(pixels.data(),pixels.size()*sizeof(pixels[0]));return h.value;}
void frameHash(Hash& h,const RaceFrame& frame){
    h.pod(frame.pose);h.pod(frame.camera);h.pod(frame.cockpit);h.pod(frame.forwardSpeed);h.pod(frame.tick);h.pod(frame.controls);h.pod(frame.restarts);h.pod(frame.action);
    h.pod(frame.nearInputs);h.pod(frame.nearSeed);h.pod(frame.progressInputs);h.pod(frame.opponent);h.pod(frame.opponentEnabled);
    h.pod(frame.instrumentPixels);h.pod(frame.wheelSprites);h.pod(frame.wheelTimeline);h.pod(frame.wheelVblanks);
    h.pod(frame.nearTrack.coveredSegments);h.pod(frame.nearTrack.queueEntries);h.pod(frame.nearTrack.opponentOffset);h.pod(frame.nearTrack.farStartSegment);h.pod(frame.nearTrack.horizonMode);
    for(const auto& command:frame.nearTrack.commands){h.pod(command.kind);h.pod(command.surface);h.pod(command.color);h.pod(command.originQueueOffset);h.pod(command.firstQueueOffset);
        for(const auto& edge:command.edges)h.pod(edge);}
    if(frame.movingTrack){const auto& track=*frame.movingTrack;h.pod(track.bridgePhase);h.pod(track.bridgeActive);h.bytes(track.progress.track.geometry.data(),track.progress.track.geometry.size());
        for(const auto& v:track.mesh.vertices)h.pod(v);for(const auto& f:track.mesh.faces)h.pod(f);for(const auto& side:track.sides)h.pod(side);}
}
std::uint64_t timelineHash(const RaceScene& scene){Hash h;for(const auto& frame:scene.frames())frameHash(h,frame);return h.value;}
void cameraHash(Hash& h,const RenderProjectionCamera& camera){
    h.pod(camera.worldPosition);h.pod(camera.quarterTurn);h.pod(camera.heightBaseline);h.pod(camera.yawResidual);h.pod(camera.viewportCenterY);
    h.pod(camera.sinRoll);h.pod(camera.cosRoll);h.pod(camera.sinYaw);h.pod(camera.cosYaw);h.pod(camera.rotatedXUnits);h.pod(camera.rotatedZUnits);
}
std::uint64_t semanticFrameHash(const RaceFrame& frame){
    Hash h;h.pod(frame.pose.worldPosition);h.pod(frame.pose.pitch);h.pod(frame.pose.yaw);h.pod(frame.pose.roll);h.pod(frame.pose.averageWheelVelocity);cameraHash(h,frame.camera);
    h.pod(frame.forwardSpeed);h.pod(frame.tick);h.pod(frame.controls);h.pod(frame.restarts);h.pod(frame.action);h.pod(frame.cameraYaw);
    h.pod(frame.lapCounter);h.pod(frame.raceComplete);h.pod(frame.boostFuel);h.pod(frame.damage);h.pod(frame.minutes);h.pod(frame.seconds);h.pod(frame.subseconds);
    for(const auto& object:frame.cockpit.objects){h.pod(object.x);h.pod(object.y);h.pod(object.widthWords);h.pod(object.height);}
    h.pod(frame.cockpit.damageStrip);h.pod(frame.cockpit.wheelRotation);h.pod(frame.cockpit.boostActive);h.pod(frame.cockpit.flameFrame);h.pod(frame.instrumentPixels);
    for(const auto& placement:frame.wheelSprites.placements){h.pod(placement.x);h.pod(placement.y);h.pod(placement.rows);}
    for(const auto& wheel:frame.wheelTimeline){h.pod(wheel.accumulator);h.pod(wheel.phase);h.pod(wheel.spritePointers);h.pod(wheel.framesToWait);h.pod(wheel.framesToWaitWhenFading);}h.pod(frame.wheelVblanks);
    const auto& seed=frame.nearSeed;cameraHash(h,seed.camera);
    for(const auto* vertices:{&seed.upper,&seed.lower})for(const auto& vertex:*vertices){h.pod(vertex.height);h.pod(vertex.x);h.pod(vertex.y);}
    h.pod(seed.flags);h.pod(seed.processedIndices);h.pod(seed.savedHeights);h.pod(seed.transformedCoordinates);h.pod(seed.startIndex);h.pod(seed.renderingLoopIndex);
    h.pod(seed.viewReverse);h.pod(seed.continuation);h.pod(seed.edgeVisibilityMode);h.pod(seed.perspectiveDepth);
    h.pod(frame.nearTrack.coveredSegments);h.pod(frame.nearTrack.queueEntries);h.pod(frame.nearTrack.opponentOffset);h.pod(frame.nearTrack.farStartSegment);h.pod(frame.nearTrack.horizonMode);
    h.pod(frame.nearTrack.commands.size());for(const auto& command:frame.nearTrack.commands){
        h.pod(command.kind);h.pod(command.surface);h.pod(command.color);h.pod(command.originQueueOffset);h.pod(command.firstQueueOffset);h.pod(command.edges.size());
        for(const auto& edge:command.edges){for(const auto* point:{&edge.from,&edge.to,&edge.clippedFrom,&edge.clippedTo}){h.pod(point->x);h.pod(point->y);}
            h.pod(edge.present);h.pod(edge.clipFlags);h.pod(edge.originQueueOffset);h.pod(edge.originEdge);h.pod(edge.storageBytes);}
    }
    h.pod(bool(frame.movingTrack));if(frame.movingTrack){const auto& track=*frame.movingTrack;h.pod(track.bridgePhase);h.pod(track.bridgeActive);
        h.bytes(track.progress.track.geometry.data(),track.progress.track.geometry.size());for(const auto& vertex:track.mesh.vertices){h.pod(vertex.x);h.pod(vertex.y);h.pod(vertex.z);}
        for(const auto& face:track.mesh.faces){h.pod(face.indices);h.pod(face.segment);h.pod(face.flags);}for(const auto& side:track.sides){
            for(const auto& vertex:side.vertices){h.pod(vertex.x);h.pod(vertex.y);h.pod(vertex.z);}h.pod(side.segment);h.pod(side.flags);h.pod(side.fillColor);h.pod(side.edgeColor);h.pod(side.side);}}
    return h.value;
}
void writeFile(const std::filesystem::path& path,const void* data,std::size_t count){std::ofstream out(path,std::ios::binary);out.write(static_cast<const char*>(data),std::streamsize(count));require(bool(out),"Cannot write audit artifact");}
void bitmap(const std::filesystem::path& path,const std::vector<std::uint32_t>& pixels){
    std::ofstream out(path,std::ios::binary);auto u16=[&](unsigned v){out.put(char(v));out.put(char(v>>8));};auto u32=[&](unsigned v){u16(v);u16(v>>16);};
    out.write("BM",2);u32(54+64000*4);u32(0);u32(54);u32(40);u32(320);u32(unsigned(-200));u16(1);u16(32);u32(0);u32(64000*4);u32(2835);u32(2835);u32(0);u32(0);
    for(auto pixel:pixels)u32(pixel);require(bool(out),"Cannot export audit comparison bitmap");
}
void horizonAudit(const NativeStartupData& source){
    SoftwareRenderer renderer;TrackMesh mesh;MountainHorizonFrame mountains;RenderProjectionTables tables;
    std::array<std::uint32_t,16> palette{};for(unsigned i=0;i<16;++i)palette[i]=0x010101u*i;
    TrigTable trig{};for(unsigned i=0;i<trig.size();++i)trig[i]=std::uint16_t((unsigned((*source.memory)[0x1ca42+2*i])<<8)|(*source.memory)[0x1ca43+2*i]);
    for(unsigned mode:{0u,0x7fu,0x80u,0xffu})for(int pitch:{-1536,-512,0,512,1536})for(int roll:{-0x1800,-0x400,0,0x400,0x1800}){
        ram=*source.memory;ram[0x1bb68]=std::uint8_t(mode);word(0x1bc42,unsigned(pitch));
        RenderProjectionCamera camera{};camera.viewportCenterY=std::uint16_t(pitch);camera.sinRoll=gameSine(std::uint16_t(roll),trig);camera.cosRoll=gameCosine(std::uint16_t(roll),trig);
        word(0x1c250,camera.sinRoll);word(0x1c252,camera.cosRoll);originalHorizon();
        const int ax=sw(word(0x1bfb4)),ay=sw(word(0x1c0f4)),bx=sw(word(0x1bfb6)),by=sw(word(0x1c0f6));
        NearTrackPresentation presentation;presentation.horizonMode=std::uint8_t(mode);
        std::vector<std::uint32_t> pixels(64000,0x123456);renderer.drawFrame(mesh,{},mountains,camera,tables,palette,pixels,false,&presentation);
        for(int y=0;y<128;++y)for(int x=0;x<256;++x){
            const auto side=(x-ax)*std::int64_t(by-ay)-(y-ay)*std::int64_t(bx-ax);
            const auto expected=palette[side>0?7:13];++horizonPixels;
            require(pixels[(y+16)*320+x+32]==expected,"Horizon differs from original projected half-plane mode="+std::to_string(mode));
        }
        for(unsigned y=0;y<200;++y)for(unsigned x=0;x<320;++x)if(x<32||x>=288||y<16||y>=144)
            require(pixels[y*320+x]==0x123456,"World renderer wrote outside its viewport");
        ++horizonCases;
    }
}
NearTrackDrawCommand polygon(std::initializer_list<NearTrackPoint> points,unsigned colour,bool reverse){
    std::vector<NearTrackPoint> vertices(points);if(reverse)std::reverse(vertices.begin(),vertices.end());NearTrackDrawCommand command;
    command.kind=NearTrackCommandKind::fill;command.surface=NearTrackSurface::road;command.color=std::uint8_t(colour);
    for(unsigned i=0;i<vertices.size();++i){NearTrackEdge edge;edge.present=true;edge.from=edge.clippedFrom=vertices[i];edge.to=edge.clippedTo=vertices[(i+1)%vertices.size()];command.edges.push_back(edge);}return command;
}
void coverageAudit(){
    SoftwareRenderer renderer;TrackMesh mesh;MountainHorizonFrame mountains;RenderProjectionCamera camera{};camera.cosRoll=0x7fff;RenderProjectionTables tables;
    std::array<std::uint32_t,16> palette{};for(unsigned i=0;i<16;++i)palette[i]=i+1;
    for(unsigned reverse=0;reverse<2;++reverse)for(unsigned triangles=0;triangles<2;++triangles){
        NearTrackPresentation commands;
        if(triangles){commands.commands.push_back(polygon({{-40,16},{300,16},{300,112}},4,reverse!=0));commands.commands.push_back(polygon({{-40,16},{300,112},{-40,112}},4,reverse!=0));}
        else{commands.commands.push_back(polygon({{-40,16},{128,16},{128,112},{-40,112}},4,reverse!=0));commands.commands.push_back(polygon({{128,16},{300,16},{300,112},{128,112}},4,reverse!=0));}
        std::vector<std::uint32_t> pixels(64000);renderer.drawFrame(mesh,{},mountains,camera,tables,palette,pixels,false,&commands);
        for(unsigned y=16;y<112;++y)for(unsigned x=0;x<256;++x){++coveragePixels;require(pixels[(y+16)*320+x+32]==palette[4],"Adjacent fill commands left a hole");}++coverageCases;
    }
}
void layoutAudit(){
    for(const auto source:std::array<std::array<int,2>,3>{{{320,200},{426,200},{512,256}}})
    for(const auto target:std::array<std::array<int,2>,4>{{{1920,1080},{1280,1024},{319,199},{800,500}}})for(bool integer:{false,true}){
        const auto rectangle=fitPresentationImage(source[0],source[1],target[0],target[1],integer);
        require(rectangle.width>0&&rectangle.height>0&&rectangle.x>=0&&rectangle.y>=0&&rectangle.x+rectangle.width<=target[0]&&rectangle.y+rectangle.height<=target[1],"Image layout escaped its host surface");
        require(std::abs(rectangle.width*source[1]-rectangle.height*source[0])<=std::max(source[0],source[1]),"Image layout changed its aspect ratio");
        require(rectangle.x==(target[0]-rectangle.width)/2&&rectangle.y==(target[1]-rectangle.height)/2,"Image layout is not centred");
        if(integer&&target[0]>=source[0]&&target[1]>=source[1])require(rectangle.width%source[0]==0&&rectangle.height%source[1]==0,"Integer image scale was lost");++layoutCases;
    }
}
NativeStartupData fresh(const std::filesystem::path& pack,unsigned track,unsigned context,bool competition){
    auto source=startNativeGame(pack);auto& m=*source.memory;initializeNativeMenuSession(m);m[0x1c9d0]=std::uint8_t(context);
    unsigned ordinal=0;while(ordinal<8&&m[0x648c2+ordinal]!=track)++ordinal;require(ordinal<8,"Track is absent from original division map");
    m[0x1c9ce]=m[0x1bb5f]=std::uint8_t(ordinal/2);m[0x1ca22]=competition?0x80:0;
    if(competition){const unsigned target=m[0x5ab42+ordinal/2]+2;std::swap(m[0x1c9c2+target],m[0x1c9c2+11]);initializeNativeSeason(m);
        require(seekNativeSeasonRace(m),"Could not prepare original competitive track");if(ordinal&1){advanceNativeSeasonRound(m);require(seekNativeSeasonRace(m),"Missing odd season track");}}
    m[0x57c68]=128;initializeNativePlayerConfiguration(m);initializeNativeTrack(m,track);
    if(scr::test::drawBridge::animate(m).geometryUpdated)rebuildNativeTrackGeometryCache(m);initializeNativeRace(m);
    if(track==5){auto placement=std::make_unique<scr::test::replay::NativeReplay>(m);placement->restartStage(0x31);m=placement->ram;}
    return source;
}
struct Counts {unsigned scenes{},ticks{},queries{},offtrack{},restarts{},bridgeChanges{},genericFaces{},cameraChanges{},cockpitChanges{},retained{},snapshotChecks{},stateChecks{},twinFrames{},comparisonCases{},changedComparisons{};std::uint64_t changedPixels{};};
void scenes(const std::filesystem::path& pack,const std::filesystem::path& output,Counts& counts){
    std::ofstream metrics(output/"scenes.csv");metrics<<"track,context,competitive,ticks,offtrack,restarts,near_commands,generic_faces,bridge_changes\n";
    for(unsigned context:{0u,11u})for(unsigned track=0;track<8;++track)for(unsigned competition=0;competition<2;++competition){
        auto source=fresh(pack,track,context,competition!=0);const auto name="track-"+std::to_string(track)+"-context-"+std::to_string(context)+"-mode-"+std::to_string(competition);
        const auto folder=output/name;std::filesystem::create_directories(folder);writeFile(folder/"seed.bin",source.memory->data(),source.memory->size());
        if(!counts.scenes)writeFile(output/"source-payload.bin",source.payload.data(),source.payload.size());
        RaceScene viewed,headless;viewed.loadNativeState(*source.memory,source.payload,folder/"viewed");headless.loadNativeState(*source.memory,source.payload,folder/"headless");
        const auto probe=std::make_unique<scr::test::replay::NativeReplay>(*source.memory);
        const auto assets=decodeRaceRenderAssets(source.payload.data(),source.payload.size(),source.memory->data(),source.memory->size());
        const auto wheelAssets=decodeNativeWheelSprites(source.memory->data(),source.memory->size());
        const auto mountains=decodeMountainHorizonData(source.payload.data(),source.payload.size());RenderProjectionTables tables;
        for(unsigned i=0;i<2048;++i){tables.angleTable[i]=scr_test::readWord(*source.memory,0x1cc46+2*i);tables.magnitudeTable[i]=scr_test::readWord(*source.memory,0x1dc46+2*i);}
        std::array<std::uint8_t,100> flags{};for(unsigned i=0;i<(*source.memory)[0x1ca1a];++i)flags[i]=(*source.memory)[scr_test::readLong(*source.memory,0x7aa1a+4*i)];
        const auto sides=buildTrackSides(viewed.mesh(),{(*source.memory)[0x691b3],(*source.memory)[0x691b2],false});
        SoftwareRenderer comparisonRenderer;unsigned maximumDifference=0;
        std::ofstream differences(folder/"native-comparison.csv");differences<<"tick,changed_viewport_pixels,old_native_faces,corrected_native_faces\n";
        std::ofstream controls(folder/"controls.csv");controls<<"tick,controls\n";
        unsigned offtrack=0,restarts=0,commands=0,generic=0,bridge=0;std::uint64_t previousBridge=0;
        for(unsigned tick=0;tick<384&&!viewed.liveTerminal();++tick){
            const unsigned mask=tick<80?17:tick<160?5:tick<224?9:17;controls<<tick<<','<<mask<<'\n';
            require(viewed.stepLive(mask)&&headless.stepLive(mask),"Audit race stopped before advancing");const auto& frame=viewed.frames().back();
            require(semanticFrameHash(frame)==semanticFrameHash(headless.frames().back()),"Presentation queries changed a later frame against the headless twin");++counts.twinFrames;
            offtrack+=frame.nearInputs.progress.offRoadSide!=0;restarts=frame.restarts;commands+=unsigned(frame.nearTrack.commands.size());
            if(frame.movingTrack){Hash h;const auto& bytes=frame.movingTrack->progress.track.geometry;h.bytes(bytes.data(),bytes.size());bridge+=previousBridge&&previousBridge!=h.value;previousBridge=h.value;}
            if(tick%16==0||tick==383){
                const unsigned current=unsigned(viewed.frames().size()-1);const auto before=timelineHash(viewed);const auto exact=pixelsHash(viewed.render(current));
                const CameraViewSettings defaults{};require(pixelsHash(viewed.renderView(double(current),defaults))==exact,"Default view differs from baseline");
                const auto middle=pixelsHash(viewed.renderLive(0.5));const auto again=pixelsHash(viewed.renderLive(0.5));require(middle==again,"Repeated interpolation changed its image");
                generic+=viewed.statistics().roadFaces+viewed.statistics().sideFaces;
                CameraViewSettings alternate;alternate.yawOffset=0x400;alternate.pitchOffset=0x100;
                const auto alt=pixelsHash(viewed.renderView(double(current),alternate));counts.cameraChanges+=alt!=exact;
                require(pixelsHash(viewed.renderView(double(current),alternate))==alt,"Alternate view is not deterministic");
                alternate.showCockpit=false;const auto hidden=pixelsHash(viewed.renderView(double(current),alternate));counts.cockpitChanges+=hidden!=alt;
                require(pixelsHash(viewed.renderView(double(current),alternate))==hidden,"Hidden cockpit view is not deterministic");
                CameraViewSettings extended;extended.view=NativeView::Extended;
                const auto originalPixels=viewed.render(current);
                const auto extendedPixels=viewed.renderView(double(current),extended);
                require(extendedPixels.size()==320*200,"Extended canvas has the wrong dimensions");
                for(unsigned y=168;y<200;++y)for(unsigned x=32;x<288;++x)
                    require(extendedPixels[y*320+x]==originalPixels[y*320+x],"Extended view changed the original instrument pixels");
                std::vector<std::uint32_t> carMask(320*200,transparentPixel);
                composeCockpit(assets,frame.cockpit,carMask);
                composeNativeWheelSprites(wheelAssets,frame.wheelSprites,frame.wheelTimeline[0],carMask);
                for(unsigned y=23;y<200;++y)for(unsigned x=0;x<320;++x)if(carMask[y*320+x]!=transparentPixel)
                    require(extendedPixels[y*320+x]==originalPixels[y*320+x],"Extended view changed the complete car assembly");
                require(pixelsHash(viewed.renderView(double(current),extended))==pixelsHash(extendedPixels),"Extended view is not deterministic");
                require(viewed.renderView(0.5,extended).size()==320*200,"Extended interpolated query failed");
                CameraViewSettings widescreen;widescreen.view=NativeView::Widescreen;
                const auto widePixels=viewed.renderView(double(current),widescreen);
                require(widePixels.size()==432*200,"Widescreen canvas has the wrong dimensions");
                for(unsigned y=23;y<200;++y)for(unsigned x=0;x<320;++x)if(carMask[y*320+x]!=transparentPixel)
                    require(widePixels[y*432+56+x]==originalPixels[y*320+x],"Widescreen changed the complete original car assembly");
                for(unsigned y=168;y<200;++y)for(unsigned x=32;x<288;++x)
                    require(widePixels[y*432+56+x]==originalPixels[y*320+x],"Widescreen changed the original instrument pixels");
                require(pixelsHash(viewed.renderView(double(current),widescreen))==pixelsHash(widePixels),"Widescreen view is not deterministic");
                require(viewed.renderView(0.5,widescreen).size()==432*200,"Widescreen interpolated query failed");
                CameraViewSettings ultrawide;ultrawide.view=NativeView::Ultrawide;
                const auto ultraPixels=viewed.renderView(double(current),ultrawide);
                require(ultraPixels.size()==864*200,"Ultrawide canvas has the wrong dimensions");
                for(unsigned y=23;y<200;++y)for(unsigned x=0;x<320;++x)if(carMask[y*320+x]!=transparentPixel)
                    require(ultraPixels[y*864+272+x]==originalPixels[y*320+x],"Ultrawide changed the complete original car assembly");
                for(unsigned y=168;y<200;++y)for(unsigned x=32;x<288;++x)
                    require(ultraPixels[y*864+272+x]==originalPixels[y*320+x],"Ultrawide changed the original instruments");
                require(pixelsHash(viewed.renderView(double(current),ultrawide))==pixelsHash(ultraPixels),"Ultrawide view is not deterministic");
                require(viewed.renderView(0.5,ultrawide).size()==864*200,"Ultrawide interpolated query failed");
                require(pixelsHash(viewed.render(current))==exact,"Alternate view leaked into default image");
                require(timelineHash(viewed)==before,"Rendering changed a retained frame or track snapshot");++counts.snapshotChecks;counts.queries+=19;
                if(!competition){
                    auto geometry=frame.nearSeed;const auto& data=frame.movingTrack?frame.movingTrack->progress:probe->progressData;
                    auto dense=buildNearTrackFrame(data,flags,frame.nearInputs,geometry);dense.presentation.horizonMode=frame.nearTrack.horizonMode;
                    const auto projected=projectMountainHorizon(mountains,frame.cameraYaw,frame.camera);
                    const auto& mesh=frame.movingTrack?frame.movingTrack->mesh:viewed.mesh();const auto& walls=frame.movingTrack?frame.movingTrack->sides:sides;
                    std::vector<std::uint32_t> old(64000),corrected(64000);const auto oldStats=comparisonRenderer.drawFrame(mesh,walls,projected,frame.camera,tables,assets.palette,old,false,&dense.presentation);
                    const auto newStats=comparisonRenderer.drawFrame(mesh,walls,projected,frame.camera,tables,assets.palette,corrected,false,&frame.nearTrack);
                    unsigned difference=0;std::vector<std::uint32_t> diff(64000);
                    for(unsigned y=16;y<144;++y)for(unsigned x=32;x<288;++x){const unsigned index=y*320+x;if(old[index]!=corrected[index]){++difference;diff[index]=0xffcc00;}}
                    differences<<tick<<','<<difference<<','<<oldStats.roadFaces+oldStats.sideFaces<<','<<newStats.roadFaces+newStats.sideFaces<<'\n';
                    ++counts.comparisonCases;counts.changedComparisons+=difference!=0;counts.changedPixels+=difference;
                    if(difference>maximumDifference){maximumDifference=difference;bitmap(folder/"before-native-mesh.bmp",old);bitmap(folder/"after-native-painter.bmp",corrected);bitmap(folder/"native-difference.bmp",diff);}
                }
                if(tick==128||tick==383){
                    viewed.saveBitmap(folder/("frame-"+std::to_string(tick)+".bmp"));
                    viewed.renderView(double(current),extended);viewed.saveBitmap(folder/("extended-"+std::to_string(tick)+".bmp"));viewed.render(current);
                }
            }
            ++counts.ticks;
        }
        const auto before=timelineHash(viewed);
        for(unsigned n=unsigned(viewed.frames().size());n>0;--n){
            const unsigned index=n-1;const auto expected=pixelsHash(viewed.render(index));(void)viewed.renderView(index,CameraViewSettings{0x800,0,0,false});
            require(pixelsHash(viewed.render(index))==expected,"Reverse seek changed baseline image");++counts.retained;}
        require(timelineHash(viewed)==before,"Reverse seeking modified snapshots");
        for(unsigned tick=0;tick<600&&!viewed.liveTerminal();++tick){
            require(viewed.stepLive(0,true)&&headless.stepLive(0,true),"Could not advance audit race quit");
            require(viewed.liveTerminal()==headless.liveTerminal(),"Presentation changed quit timing");
            require(semanticFrameHash(viewed.frames().back())==semanticFrameHash(headless.frames().back()),"Presentation changed a quitting frame");++counts.twinFrames;
        }
        require(viewed.liveTerminal()&&headless.liveTerminal(),"Audit race did not reach its original exit gate");
        auto actual=std::make_unique<NativeMemory>(),expected=std::make_unique<NativeMemory>();viewed.finishLiveExit(*actual);headless.finishLiveExit(*expected);
        require(*actual==*expected,"Presentation queries changed final simulation memory");++counts.stateChecks;
        metrics<<track<<','<<context<<','<<competition<<','<<viewed.liveTicks()<<','<<offtrack<<','<<restarts<<','<<commands<<','<<generic<<','<<bridge<<'\n';
        counts.offtrack+=offtrack;counts.restarts+=restarts;counts.bridgeChanges+=bridge;counts.genericFaces+=generic;++counts.scenes;
    }
    require(counts.scenes==32&&counts.offtrack>0&&counts.restarts>0&&counts.bridgeChanges>0,"Audit did not cover required race situations");
    require(!counts.genericFaces,"Faithful scenes still use generic road or side meshes");
    require(counts.cameraChanges>0&&counts.cockpitChanges==counts.snapshotChecks,"Camera offsets or cockpit visibility did not change presentation");
}
}
extern "C" {unsigned m68k_read_memory_8(unsigned a){return read(a);}unsigned m68k_read_memory_16(unsigned a){if(a&1)invalid=true;return(read(a)<<8)|read(a+1);}unsigned m68k_read_memory_32(unsigned a){return(m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}void m68k_write_memory_8(unsigned a,unsigned v){write(a,v);}void m68k_write_memory_16(unsigned a,unsigned v){if(a&1)invalid=true;write(a,v>>8);write(a+1,v);}void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}}
int main(int argc,char** argv)try{
    if(argc!=3)throw std::invalid_argument("Usage: native_render_audit local.scr-assets output-directory");
    const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);auto source=startNativeGame(argv[1]);
    m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);m68k_set_instr_hook_callback(hook);horizonAudit(source);coverageAudit();layoutAudit();
    Counts counts;scenes(argv[1],output,counts);
    std::cout<<"{\"status\":\"passed\",\"horizon_cases\":"<<horizonCases<<",\"horizon_geometry_mask_pixels\":"<<horizonPixels<<",\"horizon_original_instructions\":"<<horizonInstructions
        <<",\"coverage_cases\":"<<coverageCases<<",\"coverage_pixels\":"<<coveragePixels<<",\"layout_cases\":"<<layoutCases<<",\"scenes\":"<<counts.scenes<<",\"ticks\":"<<counts.ticks<<",\"render_queries\":"<<counts.queries
        <<",\"offtrack_ticks\":"<<counts.offtrack<<",\"restarts\":"<<counts.restarts<<",\"bridge_changes\":"<<counts.bridgeChanges<<",\"generic_faces\":"<<counts.genericFaces
        <<",\"alternate_angular_view_changes\":"<<counts.cameraChanges<<",\"cockpit_visibility_changes\":"<<counts.cockpitChanges<<",\"snapshot_checks\":"<<counts.snapshotChecks<<",\"last_pair_seek_queries\":"<<counts.retained<<",\"simulation_memory_checks\":"<<counts.stateChecks
        <<",\"headless_twin_frame_checks\":"<<counts.twinFrames
        <<",\"old_native_comparison_cases\":"<<counts.comparisonCases<<",\"old_native_changed_comparisons\":"<<counts.changedComparisons<<",\"old_native_changed_viewport_pixels\":"<<counts.changedPixels
        <<",\"original_world_pixel_identity_claimed\":false,\"mismatches\":0}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
