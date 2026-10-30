#pragma once
#include "render/planar_image.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace scr::runtime {
constexpr unsigned raceCanvasWidth=320,raceCanvasHeight=200;
constexpr unsigned widescreenCanvasWidth=432,widescreenCanvasOffset=56;
constexpr unsigned ultrawideCanvasWidth=864,ultrawideCanvasOffset=272;
constexpr bool isRaceCanvasSize(std::size_t size){return size==320*200||size==widescreenCanvasWidth*200||size==ultrawideCanvasWidth*200;}
constexpr std::uint8_t transparentFrameIndex=255;
struct RaceViewport { unsigned x=32,y=16,width=256,height=128; };
struct MaskedPixelWord { std::uint16_t preserve{};std::array<std::uint16_t,4> planes{}; };
struct MaskedObject {
    unsigned widthWords{},sourceRows{};
    std::vector<MaskedPixelWord> words;
};
struct RaceRenderAssets {
    IndexedImage background;
    std::vector<std::uint8_t> exteriorMask;
    std::vector<std::uint8_t> widescreenFrame; // Indexed static foreground; 255 is transparent.
    std::vector<std::uint8_t> ultrawideFrame;
    std::array<std::uint32_t,16> palette{}; // Packed0x00RRGGBB.
    std::array<MaskedObject,52> objects;
    std::array<std::uint8_t,96*8> font{};
    RaceViewport viewport{};
    std::size_t backgroundCompressedBytes{};
};
struct SpritePlacement { std::uint16_t x{},y{},widthWords{},height{}; };
struct CockpitFrame {
    std::array<SpritePlacement,52> objects{};
    std::array<std::uint8_t,256*8> damageStrip{};
    std::uint8_t wheelRotation{},boostActive{},flameFrame{};
};
// Caller verifies payload/capture identities before supplying bytes. Payload
// starts at original runtime0xe700; generated software masks come from capture.
RaceRenderAssets decodeRaceRenderAssets(const std::uint8_t* payload,std::size_t payloadSize,
    const std::uint8_t* chipRam,std::size_t chipSize);
CockpitFrame captureCockpitFrame(const std::uint8_t* chipRam,std::size_t chipSize);
// Advance once at the original gameplay frame's flame-drawing stage. Releasing
// boost holds the phase; render/view queries only consume the completed phase.
void advanceBoostFlames(std::uint8_t boostActive,std::uint8_t& flameFrame);
void composeBoostFlames(const RaceRenderAssets&,const CockpitFrame&,
    std::vector<std::uint32_t>& pixels);
void makeRaceBackground(const RaceRenderAssets&,std::vector<std::uint32_t>& pixels);
// Outside-connected black corners are transparent in Extended, including the
// top strip. Its two post/brace seam pixels stay opaque in the derived frame.
std::vector<std::uint8_t> makeFrameExteriorMask(const IndexedImage&);
void composeCockpitFrame(const RaceRenderAssets&,std::vector<std::uint32_t>&);
std::vector<std::uint8_t> makeWidescreenFrame(const RaceRenderAssets&,unsigned width=widescreenCanvasWidth);
void composeWidescreenFrame(const RaceRenderAssets&,std::vector<std::uint32_t>&);
// Upper cage opacity is 4/16 through row 80, then reaches 16/16 at row 100,
// above the highest original wheel crop. Pair codes preserve both palette
// sources in temporary race-fade captures; they never enter saved images.
unsigned upperFrameOpacity(unsigned y)noexcept;
// Side braces use 52/80 (65%) opacity, reaching 80/80 at original column 14.
unsigned sideFrameOpacity(unsigned x,unsigned width)noexcept;
std::uint32_t presentationColor(const std::array<std::uint32_t,16>& palette,std::uint16_t index,unsigned x,unsigned y,unsigned width);
void compositeUpperFrame(const RaceRenderAssets&,const std::vector<std::uint32_t>& cage,std::vector<std::uint32_t>& world,bool indexed=false);
void compositeCentredFrame(const std::vector<std::uint32_t>& original,std::vector<std::uint32_t>& wide);
constexpr std::uint32_t transparentPixel=0x01000000;
void compositeMaskedObject(const RaceRenderAssets&,unsigned objectId,
    const SpritePlacement&,std::vector<std::uint32_t>& pixels);
// Call after drawing the road into the viewport. Uses original foreground draw
// order, dynamic wheel coordinates, boost flames, and recovered damage pixels.
// Extended keeps the exterior mask clear even where it overlaps the top strip.
void composeCockpit(const RaceRenderAssets&,const CockpitFrame&,
    std::vector<std::uint32_t>& pixels,bool extendedCorners=false,bool includeCage=true);
void composeCage(const RaceRenderAssets&,const CockpitFrame&,
    std::vector<std::uint32_t>& pixels,bool extendedCorners=false);
// Native text using original96-glyph7x8 font. No host font substitution.
void drawRaceText(const RaceRenderAssets&,std::vector<std::uint32_t>& pixels,
    int x,int y,std::string_view text,std::uint8_t colourIndex);
} // namespace scr::runtime
