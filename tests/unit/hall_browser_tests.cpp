#include "menus/hall_browser.h"
#include "hall_fixture.h"
#include <iostream>

namespace {
using namespace scr::runtime;using namespace scr::test::hall;
unsigned checks{};
void require(bool v,const char* m){++checks;if(!v)throw std::runtime_error(m);}
HallBrowserRead view(unsigned count=20){
    HallOfFame hall(catalog);for(unsigned n=0;n<count;++n){auto a=attempt(n+1,4000+(n==10?9:n)*10);hall.offer(a);}
    HallBrowserRead result;result.archive.hall=std::make_shared<const HallOfFame>(std::move(hall));return result;
}
HallBrowserAction tap(HallBrowser& b,unsigned controls=0,unsigned numbers=0,bool back=false){b.update(0,{});const auto result=b.update(0,{std::uint8_t(controls),std::uint8_t(numbers),back});b.update(0,{});return result;}
void navigation(){
    HallBrowser b;b.open("Craig",{});b.apply(view());require(b.entries().size()==20&&b.pages()==2,"Top-20 browser dimensions differ");
    require(b.rank(9)==10&&b.rank(10)==10&&b.rank(11)==12,"Ranks changed across page boundary");
    tap(b,2);tap(b,2);tap(b,2);require(b.focus()==HallBrowserFocus::Records&&b.selected()==0,"Selectors did not reach record rows");
    for(unsigned n=0;n<9;++n)tap(b,2);require(b.selected()==9&&b.page()==0,"First page selection differs");
    tap(b,8);require(b.page()==1&&b.selected()==19,"Record paging lost relative row");
    tap(b,2);require(b.focus()==HallBrowserFocus::Page,"Last row did not reach page control");
    tap(b,4);require(b.page()==0,"Page control did not change page");
    tap(b,2);require(b.focus()==HallBrowserFocus::Page,"Navigation went beyond the bottom page control");
    require(tap(b,0,8)==HallBrowserAction::None&&b.focus()==HallBrowserFocus::Page,"Removed reload shortcut still active");
    require(tap(b,16)==HallBrowserAction::None&&b.page()==1,"Page confirmation did not change page");
    require(tap(b,0,0,true)==HallBrowserAction::Back,"Escape/controller Back did not close browser");
}
void filtersAndMemory(){
    HallBrowser b;b.open("A",{});b.apply(view());require(tap(b,4)==HallBrowserAction::Load&&b.board().track==5&&b.busy(),"Track wrap did not load previous board");
    require(b.entries().empty(),"Stale table appeared under new track label");tap(b,0,2);tap(b,8);require(b.board().league==HallLeague::Super,"Super filter failed");
    tap(b,0,4);tap(b,16);require(b.board().kind==HallRecordKind::Race,"Race category failed");
    b.open("Other",{2,HallLeague::Normal,HallRecordKind::Lap});require(b.driver()=="Other"&&b.board().track==5&&b.board().league==HallLeague::Super&&b.board().kind==HallRecordKind::Race,"Reopen lost filters or active driver");
    b.apply(view());require(b.busy(),"Out-of-order board response was accepted");
    auto empty=view(0);empty.board=b.board();b.apply(empty);require(b.entries().empty()&&b.pages()==1&&b.page()==0&&!b.unavailable(),"Empty board treated as error");
}
void divisionOrder(){
    const unsigned expected[]{0,2,1,3,6,7,4,5};
    for(unsigned league=0;league<2;++league){
        HallBrowser b;b.open("A",{0,HallLeague(league),HallRecordKind::Lap});
        for(unsigned n=0;n<8;++n){
            require(b.board().track==expected[n],"Track selector does not follow Division 4 through Division 1");
            require(tap(b,8)==HallBrowserAction::Load,"Track change did not request the actual track board");
        }
        require(b.board().track==0,"Forward track cycle did not wrap");
        for(unsigned n=8;n>0;--n){tap(b,4);require(b.board().track==expected[n-1],"Reverse track cycle differs from league order");}
    }
    HallBrowser b;b.open("A",{6,HallLeague::Super,HallRecordKind::Lap});tap(b,8);
    require(b.board().track==7,"Non-first initial track was treated as a display ordinal");
}
void stableIdentity(){
    HallBrowser b;b.open("A",{});auto initial=view();b.apply(initial);tap(b,2);tap(b,2);tap(b,2);for(unsigned n=0;n<9;++n)tap(b,2);
    const auto selected=b.entries()[b.selected()].attempt.id;
    HallOfFame newer=*initial.archive.hall;newer.offer(attempt(90,1));auto next=initial;next.archive.hall=std::make_shared<const HallOfFame>(newer);b.apply(next);
    require(b.selected()==10&&b.page()==1&&b.entries()[10].attempt.id==selected,"Refresh changed selected record identity");
    for(unsigned n=0;n<20;++n)newer.offer(attempt(100+n,100+n));next.archive.hall=std::make_shared<const HallOfFame>(newer);b.apply(next);
    require(b.selected()==10&&b.entries()[10].attempt.id!=selected,"Evicted row did not clamp to nearest position");
    HallBrowserRead failed;failed.error="access denied";b.apply(failed);require(b.unavailable()&&b.entries().empty()&&b.page()==0,"Read failure became an empty healthy table or invalid page");
    b.apply(next);require(!b.unavailable()&&b.selected()==10,"Retry did not recover prior selection");
}
void heldInput(){
    HallBrowser b;b.open("A",{}, {16,0,false});b.apply(view());
    require(b.update(2,{16,0,false})==HallBrowserAction::None&&b.board().track==0,"Held entry confirmation changed filter");
    require(tap(b,16)==HallBrowserAction::Load&&b.board().track==2,"Fresh confirmation did not change selector");
    b.block({0,0,true});require(b.update(1,{0,0,true})==HallBrowserAction::None,"Held Back crossed screen boundary");
    require(tap(b,0,0,true)==HallBrowserAction::Back,"Fresh Back failed after release");
}
void challenge(){
    HallBrowser b;b.open("A",{});auto snapshot=view();snapshot.ghosts[0]=true;b.apply(snapshot);
    tap(b,2);tap(b,2);tap(b,2);require(b.canChallenge()&&tap(b,16)==HallBrowserAction::Challenge,"Available lap row did not request its ghost");
    b.loading();require(!b.canChallenge()&&tap(b,16)==HallBrowserAction::None,"Loading row dispatched another challenge");
    b.apply(snapshot);tap(b,2);require(!b.canChallenge()&&tap(b,16)==HallBrowserAction::None,"Time-only row requested a challenge");
    tap(b,0,4);tap(b,16);snapshot.board=b.board();snapshot.ghosts.fill(true);b.apply(snapshot);tap(b,2);
    require(!b.canChallenge()&&tap(b,16)==HallBrowserAction::None,"Race result offered a ghost challenge");
}
}
int main()try{navigation();filtersAndMemory();divisionOrder();stableIdentity();heldInput();challenge();std::cout<<"Hall browser: "<<checks<<" checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
