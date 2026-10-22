#include "replay_render_assets.h"
#include <algorithm>
#include <stdexcept>

namespace scr::viewer {
namespace {
constexpr unsigned base=0xe700,background=0x201a4,palette=0x20184;
constexpr unsigned objectTable=0x6a16c,objectPointers=0x6a4ac;
std::uint16_t word(const std::uint8_t* r,std::size_t size,std::size_t a) {
    if(a>=size||size-a<2)throw std::invalid_argument("Image word outside supplied bytes");
    return static_cast<std::uint16_t>((unsigned(r[a])<<8)|r[a+1]);
}
std::uint32_t longword(const std::uint8_t* r,std::size_t size,std::size_t a) {
    return (std::uint32_t(word(r,size,a))<<16)|word(r,size,a+2);
}
void frameSize(const std::vector<std::uint32_t>& pixels) {
    if(pixels.size()!=replayWidth*replayHeight)throw std::invalid_argument("Expected320x200 replay canvas");
}
void chipExtent(const std::uint8_t* r,std::size_t size) {
    if(!r||size!=0x80000)throw std::invalid_argument("Expected512KiB initialized Chip RAM image");
}
}
ReplayRenderAssets decodeReplayRenderAssets(const std::uint8_t* payload,std::size_t payloadSize,
        const std::uint8_t* chipRam,std::size_t chipSize) {
    chipExtent(chipRam,chipSize);
    if(!payload||payloadSize!=408540)throw std::invalid_argument("Expected original408540-byte payload");
    ReplayRenderAssets assets;
    const auto decoded=decodePlanarRle(payload+background-base,payloadSize-(background-base),320,200,4);
    assets.background=decodePlanarImage(decoded.planes.data(),decoded.planes.size(),320,200,4);
    assets.backgroundCompressedBytes=decoded.consumed;
    for(unsigned i=0;i<16;++i)assets.palette[i]=originalPaletteRgb(word(payload,payloadSize,palette-base+2*i));
    // Original startup patches six font bytes (0x1feed,0x1fef7/8,
    // 0x2007a,0x20080/1). Read the font after native/captured initialization.
    std::copy_n(chipRam+0x1fe82,assets.font.size(),assets.font.begin());
    std::array<unsigned,52> pointers{};
    for(unsigned id=0;id<pointers.size();++id)pointers[id]=longword(chipRam,chipSize,objectPointers+id*4);
    for(unsigned id=0;id<assets.objects.size();++id) {
        // IDs37..48 are two-plane hardware sprites, not masked software data.
        if(id>=37&&id<=48)continue;
        const auto a=objectTable+id*16;
        auto& object=assets.objects[id];
        object.widthWords=unsigned(word(chipRam,chipSize,a+4))+1;
        const unsigned rows=unsigned(word(chipRam,chipSize,a+6))+1;
        if(!object.widthWords||object.widthWords>20||!rows||rows>200)
            throw std::invalid_argument("Software object dimensions outside original canvas");
        unsigned end=pointers[id]+object.widthWords*rows*10;
        // Wheel coordinates clip the rendered height dynamically. Retain their
        // full58-row initialized image, bounded by the next immutable pointer.
        if(id<6) {
            end=static_cast<unsigned>(chipSize);
            for(auto p:pointers)if(p>pointers[id])end=std::min(end,p);
        }
        if(pointers[id]<0x1000||end>chipSize||end<=pointers[id]||
           (end-pointers[id])%(object.widthWords*10))
            throw std::invalid_argument("Software object source extent is invalid");
        object.sourceRows=(end-pointers[id])/(object.widthWords*10);
        if(object.sourceRows>200)throw std::invalid_argument("Software object has too many rows");
        object.words.resize(std::size_t(object.widthWords)*object.sourceRows);
        for(std::size_t group=0;group<object.words.size();++group) {
            auto& value=object.words[group];const auto source=pointers[id]+group*10;
            value.preserve=word(chipRam,chipSize,source);
            for(unsigned p=0;p<4;++p)value.planes[p]=word(chipRam,chipSize,source+2+2*p);
        }
    }
    return assets;
}
CockpitFrame captureCockpitFrame(const std::uint8_t* chipRam,std::size_t chipSize) {
    chipExtent(chipRam,chipSize);CockpitFrame frame;
    for(unsigned id=0;id<frame.objects.size();++id) {
        const auto a=objectTable+id*16;auto& o=frame.objects[id];
        o.x=static_cast<std::uint16_t>((word(chipRam,chipSize,a+8)&255)*16);
        o.y=word(chipRam,chipSize,a+10)&255;
        o.widthWords=static_cast<std::uint16_t>(word(chipRam,chipSize,a+4)+1);
        o.height=static_cast<std::uint16_t>(word(chipRam,chipSize,a+6)+1);
    }
    frame.wheelRotation=chipRam[0x1bbdb];frame.boostActive=chipRam[0x1bb62];frame.flameFrame=chipRam[0x1bb69];
    const auto start=longword(chipRam,chipSize,0x6a584);
    if(start>chipSize-32000)throw std::invalid_argument("Captured damage framebuffer outside Chip RAM");
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<256;++x) {
        std::uint8_t index=0;
        for(unsigned p=0;p<4;++p)index=static_cast<std::uint8_t>(index|(((chipRam[start+p*8000+y*40+4+x/8]>>(7-x%8))&1u)<<p));
        frame.damageStrip[y*256+x]=index;
    }
    return frame;
}
void advanceReplayFlames(std::uint8_t boostActive,std::uint8_t& flameFrame) {
    if(flameFrame>2)throw std::invalid_argument("Flame frame outside original0..2 range");
    if(boostActive)flameFrame=std::uint8_t(flameFrame==2?0:flameFrame+1);
}
void makeReplayBackground(const ReplayRenderAssets& assets,std::vector<std::uint32_t>& pixels) {
    if(assets.background.width!=320||assets.background.height!=200||assets.background.pixels.size()!=320*200)
        throw std::invalid_argument("Original background dimensions are invalid");
    pixels.resize(320*200);
    for(unsigned i=0;i<pixels.size();++i)pixels[i]=assets.palette.at(assets.background.pixels[i]);
}
void compositeMaskedObject(const ReplayRenderAssets& assets,unsigned id,
        const ReplayObjectPlacement& o,std::vector<std::uint32_t>& pixels) {
    frameSize(pixels);const auto& object=assets.objects.at(id);
    if(o.widthWords!=object.widthWords||o.height>object.sourceRows)
        throw std::invalid_argument("Object placement exceeds immutable source image");
    for(unsigned y=0;y<o.height;++y)for(unsigned group=0;group<o.widthWords;++group) {
        const auto& value=object.words[y*object.widthWords+group];
        for(unsigned bit=0;bit<16;++bit) {
            const unsigned x=o.x+group*16+bit,yy=o.y+y;
            if(x>=320||yy>=200)continue;
            const unsigned mask=1u<<(15-bit);std::uint8_t index=0;
            for(unsigned p=0;p<4;++p)if(value.planes[p]&mask)index=static_cast<std::uint8_t>(index|(1u<<p));
            // Initialized masked objects have zero source bits wherever their
            // preserve mask is one. Reject malformed input rather than replace
            // the original per-plane OR with an approximate RGB blend.
            if(value.preserve&mask) {
                if(index)throw std::invalid_argument("Preserved object pixel has nonzero source planes");
            } else pixels[yy*320+x]=assets.palette[index];
        }
    }
}
void composeReplayCockpit(const ReplayRenderAssets& assets,const CockpitFrame& frame,
        std::vector<std::uint32_t>& pixels) {
    frameSize(pixels);
    if(frame.wheelRotation>2)throw std::invalid_argument("Wheel rotation frame outside original0..2 range");
    for(unsigned id:{13u,14u,unsigned(frame.wheelRotation),5u-frame.wheelRotation,10u,11u,12u})
        compositeMaskedObject(assets,id,frame.objects[id],pixels);
    composeReplayFlames(assets,frame,pixels);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<256;++x)pixels[y*320+32+x]=assets.palette[frame.damageStrip[y*256+x]];
}
void composeReplayFlames(const ReplayRenderAssets& assets,const CockpitFrame& frame,
        std::vector<std::uint32_t>& pixels) {
    frameSize(pixels);
    if(frame.boostActive) {
        if(frame.flameFrame>2)throw std::invalid_argument("Flame frame outside original0..2 range");
        const unsigned left=frame.flameFrame==2 ? 49 : frame.flameFrame+6;
        const unsigned right=frame.flameFrame==2 ? 50 : frame.flameFrame+8;
        compositeMaskedObject(assets,left,frame.objects[left],pixels);
        compositeMaskedObject(assets,right,frame.objects[right],pixels);
    }
}
void drawReplayText(const ReplayRenderAssets& assets,std::vector<std::uint32_t>& pixels,
        int x,int y,std::string_view text,std::uint8_t colour) {
    frameSize(pixels);const auto rgb=assets.palette.at(colour);
    for(unsigned char character:text) {
        if(character<32||character>=128)continue;
        for(unsigned row=0;row<8;++row)for(unsigned column=0;column<7;++column) {
            const int xx=x+int(column),yy=y+int(row);
            if(xx>=0&&xx<320&&yy>=0&&yy<200&&(assets.font[(character-32)*8+row]&(0x80u>>column)))
                pixels[unsigned(yy)*320+unsigned(xx)]=rgb;
        }
        x+=7;
    }
}
} // namespace scr::viewer
