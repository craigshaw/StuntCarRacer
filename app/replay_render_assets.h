#pragma once
#include "planar_image.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace scr::viewer {
constexpr unsigned replayWidth=320,replayHeight=200;
struct ReplayViewport { unsigned x=32,y=16,width=256,height=128; };
struct MaskedPixelWord { std::uint16_t preserve{};std::array<std::uint16_t,4> planes{}; };
struct ReplayMaskedObject {
    unsigned widthWords{},sourceRows{};
    std::vector<MaskedPixelWord> words;
};
struct ReplayRenderAssets {
    IndexedImage background;
    std::array<std::uint32_t,16> palette{}; // Packed0x00RRGGBB.
    std::array<ReplayMaskedObject,52> objects;
    std::array<std::uint8_t,96*8> font{};
    ReplayViewport viewport{};
    std::size_t backgroundCompressedBytes{};
};
struct ReplayObjectPlacement { std::uint16_t x{},y{},widthWords{},height{}; };
struct CockpitFrame {
    std::array<ReplayObjectPlacement,52> objects{};
    std::array<std::uint8_t,256*8> damageStrip{};
    std::uint8_t wheelRotation{},boostActive{},flameFrame{};
};
// Caller verifies payload/capture identities before supplying bytes. Payload
// starts at original runtime0xe700; generated software masks come from capture.
ReplayRenderAssets decodeReplayRenderAssets(const std::uint8_t* payload,std::size_t payloadSize,
    const std::uint8_t* chipRam,std::size_t chipSize);
CockpitFrame captureCockpitFrame(const std::uint8_t* chipRam,std::size_t chipSize);
// Advance once at the original gameplay frame's flame-drawing stage. Releasing
// boost holds the phase; render/view queries only consume the completed phase.
void advanceReplayFlames(std::uint8_t boostActive,std::uint8_t& flameFrame);
void composeReplayFlames(const ReplayRenderAssets&,const CockpitFrame&,
    std::vector<std::uint32_t>& pixels);
void makeReplayBackground(const ReplayRenderAssets&,std::vector<std::uint32_t>& pixels);
void compositeMaskedObject(const ReplayRenderAssets&,unsigned objectId,
    const ReplayObjectPlacement&,std::vector<std::uint32_t>& pixels);
// Call after drawing the road into the viewport. Uses original foreground draw
// order, dynamic wheel coordinates, boost flames, and recovered damage pixels.
void composeReplayCockpit(const ReplayRenderAssets&,const CockpitFrame&,
    std::vector<std::uint32_t>& pixels);
// Native text using original96-glyph7x8 font. No host font substitution.
void drawReplayText(const ReplayRenderAssets&,std::vector<std::uint32_t>& pixels,
    int x,int y,std::string_view text,std::uint8_t colourIndex);
} // namespace scr::viewer
