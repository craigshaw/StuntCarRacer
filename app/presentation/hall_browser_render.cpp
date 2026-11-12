#include "presentation/hall_browser_render.h"
#include "presentation/native_hall_of_fame.h"
#include <algorithm>

namespace scr::runtime {
namespace {
void rect(NativeMenuImage& image,unsigned x,unsigned y,unsigned w,unsigned h,unsigned colour){
    for(unsigned row=y;row<std::min(200u,y+h);++row)for(unsigned col=x;col<std::min(320u,x+w);++col)image.indices[row*320+col]=std::uint8_t(colour);
}
void text(const NativeMemory& m,NativeMenuImage& image,const std::string& value,unsigned x,unsigned y,unsigned colour){
    NativeMenuTextCursor c;c.column=x/7;c.horizontalOffset=x%7;c.row=(y+8)/8;c.yOffset=(y+8)%8;c.colour=colour;
    for(unsigned char ch:value){
        const auto xGlyph=c.column*7+c.horizontalOffset,yGlyph=c.row*8+c.yOffset-8;
        // This font stores '!' in the ASCII '<' slot. Mirror its existing
        // seven-pixel right arrow instead of introducing a different font.
        drawNativeMenuCharacter(m,image,c,ch=='<'?'>':ch);
        if(ch=='<'&&xGlyph+7<=320&&yGlyph+8<=200)for(unsigned row=0;row<8;++row)for(unsigned col=0;col<3;++col)
            std::swap(image.indices[(yGlyph+row)*320+xGlyph+col],image.indices[(yGlyph+row)*320+xGlyph+6-col]);
    }
}
void control(const NativeMemory& m,NativeMenuImage& image,const HallBrowser& b,HallBrowserFocus focus,const std::string& value,unsigned x,unsigned y,unsigned width){
    const bool selected=b.focus()==focus;
    rect(image,x-2,y-1,width,10,selected?7:12);rect(image,x-1,y,width-2,8,1);
    text(m,image,value,x,y,selected?7:15);
}
}
NativeMenuImage makeHallBrowserImage(const NativeMemory& m,const HallBrowser& b){
    // Keep the original title/rays/artwork and its palette. The denser authored
    // table extends the original red name bands, grey fields and blue markers.
    auto image=makeNativeHallOfFameBackground(m);rect(image,0,54,320,146,0);
    rect(image,0,54,320,10,2);text(m,image,"All players",4,55,15);text(m,image,"Driving as: "+b.driver().substr(0,12),132,55,15);
    std::string track(m.begin()+0x1edaa+b.board().track*16,m.begin()+0x1edaa+b.board().track*16+15);
    while(!track.empty()&&track.back()==' ')track.pop_back();
    control(m,image,b,HallBrowserFocus::Track,"1.Track: < "+track+" >",4,65,316);
    control(m,image,b,HallBrowserFocus::League,b.board().league==HallLeague::Normal?"2.< Normal >":"2.< Super >",4,76,151);
    control(m,image,b,HallBrowserFocus::Timing,b.timingCategory()==TimingCategory::Enhanced?"4.< Enhanced >":"4.< Original >",161,76,157);
    control(m,image,b,HallBrowserFocus::Category,b.board().kind==HallRecordKind::Lap?"3.Type: < Lap >":"3.Type: < 3-lap race >",4,87,316);
    rect(image,0,98,320,9,12);text(m,image,"Rk",4,98,0);text(m,image,"Driver",28,98,0);text(m,image,"Time",195,98,0);text(m,image,"Ghost",266,98,0);
    if(b.busy()&&!b.rowCount())text(m,image,"Loading records...",90,138,15);
    else if(b.unavailable()){text(m,image,"Hall records unavailable",62,130,15);text(m,image,"Reopen Hall to try again",83,146,12);}
    else if(!b.rowCount()){text(m,image,"No records yet",111,130,15);
        text(m,image,b.board().kind==HallRecordKind::Lap?"Complete a lap to enter":"Finish a three-lap race",83,146,12);}
    else{
        const auto first=b.page()*10;
        for(unsigned n=first;n<b.rowCount()&&n<first+10;++n){const auto y=108+(n-first)*8;
            const bool selected=b.focus()==HallBrowserFocus::Records&&n==b.selected();
            rect(image,1,y,20,8,selected?7:4);rect(image,24,y,140,8,selected?10:9);rect(image,167,y,74,8,n%2?2:1);rect(image,244,y,74,8,n%2?2:1);
            text(m,image,std::to_string(b.rank(n)),4,y,selected?0:15);text(m,image,b.rowName(n),28,y,7);
            const auto time=b.rowTime(n);const auto pitch=std::min(7u,70u/unsigned(time.size()));
            if(pitch==7)text(m,image,time,236-unsigned(time.size())*7,y,15);
            else for(unsigned c=0;c<time.size();++c)for(unsigned row=0;row<8;++row)for(unsigned col=0;col<pitch;++col)
                if(m[0x1fe82+(unsigned(time[c])-32)*8+row]&(128u>>(col*7/pitch)))image.indices[(y+row)*320+236-unsigned(time.size())*pitch+c*pitch+col]=15;
            text(m,image,selected&&b.busy()?"Loading":b.board().kind==HallRecordKind::Race?"-":b.ghost(n)?"Ready":"None",266,y,b.ghost(n)?15:12);
        }
    }
    control(m,image,b,HallBrowserFocus::Page,"< Page "+std::to_string(b.page()+1)+"/"+std::to_string(b.pages())+" >",104,191,114);
    if(b.canChallenge())text(m,image,"Race ghost",4,191,7);
    return image;
}
} // namespace scr::runtime
