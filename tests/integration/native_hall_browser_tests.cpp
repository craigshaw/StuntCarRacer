#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "session/native_game_session.h"
#include "presentation/hall_browser_render.h"
#include "presentation/native_hall_of_fame.h"
#include "platform/windows/shared_hall_storage.h"
#include "hall_ghost_fixture.h"
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
using namespace scr;using namespace scr::runtime;namespace fs=std::filesystem;namespace fixture=scr::test::hall;
unsigned checks{};
void require(bool v,const char* m){++checks;if(!v)throw std::runtime_error(m);}
void update(NativeGameSession& g,double elapsed=0,unsigned controls=0,unsigned numbers=0,bool back=false){NativeGameInput in;in.menu={std::uint8_t(controls),std::uint8_t(numbers),back};g.update(elapsed,in);}
void tap(NativeGameSession& g,unsigned controls=16,unsigned numbers=0,bool back=false){update(g);update(g,0,controls,numbers,back);update(g);}
void settle(NativeGameSession& g){for(unsigned n=0;n<12;++n)update(g,1);}
void choose(NativeGameSession& g,unsigned row){settle(g);tap(g,0,1u<<row);settle(g);}
void confirm(NativeGameSession& g){tap(g);settle(g);}
std::unique_ptr<NativeGameSession> career(const fs::path& pack,const fs::path& root,const fs::path& shared){
    auto g=std::make_unique<NativeGameSession>(pack,fs::path{});g->enableCareers(root,shared);g->setNavigation(NativeMenuNavigation::Responsive);update(*g);
    tap(*g);tap(*g);for(unsigned char c:std::string("BROWSER")){NativeGameInput in;in.rawKeys.push_back({*nativeAmigaRawKey(c,0,false),true});g->update(0,in);in.rawKeys[0].pressed=false;g->update(0,in);}
    tap(*g);tap(*g);require(g->activeCareer()&&g->page()==NativeMenuPage::Main,"Browser career did not open");return g;
}
void loaded(NativeGameSession& g){
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do{update(g);if(g.hallBrowser()&&!g.hallBrowser()->busy()&&!g.transitioning())return;Sleep(1);}while(std::chrono::steady_clock::now()<until);
    throw std::runtime_error("Browser read did not finish");
}
void open(NativeGameSession& g){choose(g,0);confirm(g);require(g.page()==NativeMenuPage::HallOfFame&&g.hallBrowser(),"Career did not open shared Hall");loaded(g);}
void image(NativeGameSession& g,const fs::path& path){std::ofstream out(path,std::ios::binary);out<<"P6\n320 200\n255\n";
    for(auto pixel:g.render())for(unsigned shift:{16u,8u,0u})out.put(char(pixel>>shift));require(bool(out),"Cannot save browser preview");}
std::shared_ptr<const HallSubmission> submission(HallCatalog catalog,unsigned board,unsigned row){
    const auto key=hallBoardKey(board);const auto ordinal=board*100+row+1;
    const auto time=key.kind==HallRecordKind::Lap?4000+(row==10?9:row)*12:row==19?179997:171000+row*400;
    auto a=fixture::attempt(ordinal,time,key,"PLAYER"+std::to_string(100000+row));
    auto ghost=fixture::ghost(a);ghost.catalog=catalog;
    return std::make_shared<const HallSubmission>(makeHallSubmission(a,key.kind==HallRecordKind::Lap&&row%3!=1?&ghost:nullptr));
}
void seed(SharedHallArchive& archive,HallCatalog catalog){
    std::vector<std::shared_ptr<const HallSubmission>> records;for(unsigned board=0;board<32;++board)for(unsigned row=0;row<20;++row)records.push_back(submission(catalog,board,row));
    archive.commit(records);
    for(unsigned board=0;board<32;board+=2){fs::remove(archive.root()/"ghosts"/hallObjectName(submission(catalog,board,2)->attempt.id));
        std::ofstream(archive.root()/"ghosts"/hallObjectName(submission(catalog,board,5)->attempt.id),std::ios::trunc)<<"damaged";}
}
void boards(const fs::path& pack,const fs::path& root,NativeMenuNavigation policy){
    const auto shared=root/"shared";auto g=career(pack,root/"private",shared);auto startup=startNativeGame(pack);const HallCatalog catalog{ghostTrackContentIdentity(startup.payload),1};
    SharedHallArchive archive(shared,catalog);seed(archive,catalog);g->setNavigation(policy);
    // Keep a pending real season while viewing every track and both leagues.
    choose(*g,2);confirm(*g);require(g->page()==NativeMenuPage::Matchup,"Season continuation fixture missing");
    const auto rng=std::vector<std::uint8_t>(g->memory().begin()+0x625a2,g->memory().begin()+0x625a7);
    tap(*g,0,0,true);settle(*g);require(g->hasSuspendedSeason()&&g->page()==NativeMenuPage::Main,"Season was not suspended");open(*g);
    auto unchanged=std::make_unique<NativeMemory>(g->memory());
    const auto old=makeNativeHallOfFameBackground(g->memory()),now=makeHallBrowserImage(g->memory(),*g->hallBrowser());
    require(old.palette==now.palette&&std::equal(old.indices.begin(),old.indices.begin()+320*54,now.indices.begin()),"Browser changed original Hall title artwork or palette");
    image(*g,root/"lap-page-1.ppm");
    for(unsigned track=0;track<8;++track)for(unsigned league=0;league<2;++league)for(unsigned kind=0;kind<2;++kind){
        const auto* b=g->hallBrowser();require(b&&b->board().track==track&&unsigned(b->board().league)==league&&unsigned(b->board().kind)==kind,"Board selectors display wrong records");
        require(b->entries().size()==20&&b->pages()==2,"Populated board did not retain two pages");
        if(!kind)require(b->ghost(0)&&!b->ghost(1)&&!b->ghost(2)&&!b->ghost(5),"Ghost availability trusted missing/corrupt references");
        tap(*g,0,4);tap(*g,2);require(g->hallBrowser()->focus()==HallBrowserFocus::Records,"Rows inaccessible from category selector");
        if(!kind)require(g->hallBrowser()->rank(9)==10&&g->hallBrowser()->rank(10)==10&&g->hallBrowser()->rank(11)==12,"Tie ranks changed at page boundary");
        tap(*g,8);require(g->hallBrowser()->page()==1&&g->hallBrowser()->selected()==10,"Second page inaccessible");
        tap(*g,16);require(g->page()==NativeMenuPage::HallOfFame&&!g->transitioning(),"Browse-only row confirmation launched a race");
        for(auto view:{NativeView::Original,NativeView::Extended,NativeView::Widescreen,NativeView::Ultrawide})require(g->render(true,false,view)==g->render(false,false,NativeView::Original),"Hall presentation changed with world aspect/smoothing");
        if(!track&&!league)image(*g,root/(kind?"race-page-2.ppm":"lap-page-2.ppm"));
        tap(*g,4);tap(*g,0,4);tap(*g);loaded(*g); // Cycle category back after each pair.
        if(kind){tap(*g,0,2);tap(*g);loaded(*g);if(league){tap(*g,0,1);tap(*g);loaded(*g);}}
    }
    require(g->memory()==*unchanged,"Browsing/read/availability queries changed native memory or RNG");
    // Refresh preserves an ID when a new insertion moves it onto another page.
    tap(*g,0,4);tap(*g,2);for(unsigned n=0;n<9;++n)tap(*g,2);const auto selected=g->hallBrowser()->entries()[9].attempt.id;
    auto faster=fixture::attempt(99999,1);archive.commit({std::make_shared<const HallSubmission>(makeHallSubmission(faster))});
    tap(*g,0,8);loaded(*g);require(g->hallBrowser()->selected()==10&&g->hallBrowser()->entries()[10].attempt.id==selected,"Shared refresh selected a different record");
    const auto remembered=g->hallBrowser()->board();const auto selectedRow=g->hallBrowser()->selected();
    {SharedHallLock lock(shared);const auto started=std::chrono::steady_clock::now();tap(*g,0,8);g->render();require(std::chrono::steady_clock::now()-started<std::chrono::seconds(1),"Browser performed blocking file I/O on UI thread");}
    loaded(*g);tap(*g,0,0,true);settle(*g);require(g->page()==NativeMenuPage::Main&&g->hasSuspendedSeason(),"Hall Back did not return to its career");
    open(*g);require(hallBoardIndex(g->hallBrowser()->board())==hallBoardIndex(remembered)&&g->hallBrowser()->selected()==selectedRow,"Reopen lost filters/selected record");
    tap(*g,0,0,true);settle(*g);choose(*g,2);confirm(*g);
    require(g->page()==NativeMenuPage::Matchup&&std::equal(rng.begin(),rng.end(),g->memory().begin()+0x625a2),"Viewing other leagues altered pending season RNG");require(g->requestClose(),"Browser career did not flush");
}
void states(const fs::path& pack,const fs::path& root){
    const auto shared=root/"shared";auto g=career(pack,root/"private",shared);open(*g);require(g->hallBrowser()->entries().empty()&&!g->hallBrowser()->unavailable(),"Empty archive was treated as broken");image(*g,root/"empty.ppm");
    std::ofstream(shared/"index.scr-hall")<<"unreadable";tap(*g,0,8);loaded(*g);
    require(g->hallBrowser()->unavailable(),"Corrupt shared archive silently appeared empty");image(*g,root/"unavailable.ppm");
    tap(*g,0,0,true);settle(*g);require(g->page()==NativeMenuPage::Main,"Unavailable Hall trapped the player");
    require(fs::file_size(shared/"index.scr-hall")==10,"Browser reset damaged data");
}
void multiplayer(const fs::path& pack,const fs::path& root){
    NativeGameSession g(pack,{});g.enableCareers(root);g.setNavigation(NativeMenuNavigation::Responsive);update(g);
    tap(g,0,2);tap(g);
    const auto name=[&](const std::string& value){for(unsigned char c:value){NativeGameInput in;in.rawKeys.push_back({*nativeAmigaRawKey(c,0,false),true});g.update(0,in);in.rawKeys[0].pressed=false;g.update(0,in);}};
    name("FIRST");tap(g);tap(g);name("SECOND");tap(g);tap(g,0,2);tap(g);tap(g);
    require(g.multiplayer()&&g.page()==NativeMenuPage::Main,"Local menu fixture missing");
    require(g.menuState().disabledRows&1,"Hall was enabled outside the career menu");
    tap(g,0,1);tap(g,1);require(g.menuState().selected!=0&&!g.hallBrowser(),"Disabled Hall could be selected in multiplayer");image(g,root/"multiplayer-main.ppm");require(g.requestClose(),"Local menu did not close");
}
}
int main(int argc,char** argv)try{
    require(argc==3,"Usage: native_hall_browser_tests asset-pack isolated-output");const fs::path root=argv[2];fs::create_directories(root);
    boards(argv[1],root/"responsive",NativeMenuNavigation::Responsive);boards(argv[1],root/"original",NativeMenuNavigation::Original);states(argv[1],root/"states");multiplayer(argv[1],root/"multiplayer");
    std::ofstream report(root/"results.json");report<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"boards\":32,\"navigation_policies\":2,\"original_header_and_palette\":true,\"full_memory_and_rng_isolation\":true,\"verified_ghost_availability\":true,\"world_view_mask\":15,\"synthetic_ui_records\":true,\"original_cpu_executed\":false}\n";
    std::cout<<"Native Hall browser passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
