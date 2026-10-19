#include "planar_image.h"
#include <limits>
#include <stdexcept>

namespace scr {
namespace {
std::size_t imageBytes(unsigned width,unsigned height,unsigned planes) {
    if(!width||width%8||!height||!planes||planes>8)
        throw std::invalid_argument("Unsupported planar image dimensions");
    const auto bytes=std::uint64_t(width/8)*height*planes;
    if(bytes>std::numeric_limits<std::size_t>::max()||bytes>64u*1024u*1024u)
        throw std::invalid_argument("Planar image exceeds bounded decode domain");
    return static_cast<std::size_t>(bytes);
}
}
DecodedPlanarRle decodePlanarRle(const std::uint8_t* data,std::size_t size,
        unsigned width,unsigned height,unsigned planeCount) {
    const auto bytes=imageBytes(width,height,planeCount);
    if(!data&&size)throw std::invalid_argument("Missing RLE input");
    DecodedPlanarRle result{std::vector<std::uint8_t>(bytes),0};
    const auto stride=width/8,planeSize=stride*height;
    auto next=[&]() {
        if(result.consumed>=size)throw std::invalid_argument("Truncated planar RLE stream");
        return data[result.consumed++];
    };
    for(unsigned y=0;y<height;++y)for(unsigned plane=0;plane<planeCount;++plane) {
        unsigned column=0;
        while(column<stride) {
            const auto control=next();
            if(control==0x80)continue;
            const unsigned count=control<0x80 ? unsigned(control)+1 : 257u-control;
            if(count>stride-column)throw std::invalid_argument("Planar RLE run crosses row boundary");
            const auto value=control<0x80 ? 0 : next();
            for(unsigned i=0;i<count;++i)
                result.planes[std::size_t(plane)*planeSize+std::size_t(y)*stride+column++]=control<0x80 ? next() : static_cast<std::uint8_t>(value);
        }
    }
    return result;
}
IndexedImage decodePlanarImage(const std::uint8_t* data,std::size_t size,
        unsigned width,unsigned height,unsigned planeCount) {
    const auto bytes=imageBytes(width,height,planeCount);
    if(!data||size!=bytes)throw std::invalid_argument("Planar image byte extent mismatch");
    IndexedImage image{width,height,std::vector<std::uint8_t>(std::size_t(width)*height)};
    const auto stride=width/8,planeSize=stride*height;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        std::uint8_t index=0;
        for(unsigned plane=0;plane<planeCount;++plane)
            index=static_cast<std::uint8_t>(index|(((data[std::size_t(plane)*planeSize+std::size_t(y)*stride+x/8]>>(7-x%8))&1u)<<plane));
        image.pixels[std::size_t(y)*width+x]=index;
    }
    return image;
}
std::uint32_t originalPaletteRgb(std::uint16_t source) noexcept {
    const auto colour=static_cast<std::uint16_t>(source<<1);
    const auto hardware=static_cast<std::uint16_t>(colour|((colour&0xf)?1:0)|((colour&0xf0)?0x10:0)|((colour&0xf00)?0x100:0));
    return (std::uint32_t((hardware>>8)&15)*17u<<16)|
        (std::uint32_t((hardware>>4)&15)*17u<<8)|(std::uint32_t(hardware&15)*17u);
}
} // namespace scr
