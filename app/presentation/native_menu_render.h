#pragma once
#include "assets/native_asset_initialization.h"
#include <array>
#include <cstdint>
#include <vector>
#include <string>

namespace scr::runtime {
struct NativeMenuRenderState {
    unsigned menuOffset{},count{3},selected{1},division{3},selectedDivision{3};
    bool menuStrings{},confirmed{};
    unsigned disabledRows{};
};
struct NativeMenuImage {
    std::array<std::uint8_t,320*200> indices{};
    std::array<std::uint32_t,16> palette{};
};
struct NativeMenuTextCursor {
    unsigned column{},row{},yOffset{},horizontalOffset{},colour{1};
};
// Original disk image, palette and seven-pixel font; no host font or capture.
[[nodiscard]] NativeMenuImage makeNativeMenuBackground(const NativeMemory&);
[[nodiscard]] NativeMenuImage makeNativeMenuFrame(const NativeMemory&,unsigned division,bool header=true);
// Strings retain the original 0x1f,column,row positioning and 0xff terminator.
// Menu display mode has an eight-pixel vertical displacement, as in 0x59624.
void drawNativeMenuString(const NativeMemory&,NativeMenuImage&,NativeMenuTextCursor&,
    unsigned tableAddress,unsigned byteOffset);
void drawNativeMenuCharacter(const NativeMemory&,NativeMenuImage&,NativeMenuTextCursor&,unsigned character);
void packNativeMenuPixels(const NativeMenuImage&,std::vector<std::uint32_t>&);
[[nodiscard]] NativeMenuImage makeNativeMenuImage(const NativeMemory&,const NativeMenuRenderState&);
void renderNativeMenu(const NativeMemory&,const NativeMenuRenderState&,std::vector<std::uint32_t>&);
// Authored options reuse the supplied artwork, strips and original glyphs.
NativeMenuImage makeNativeChoiceImage(const NativeMemory&,const std::string& heading,
    const std::vector<std::string>& options,unsigned selected,const std::string& detail={},const std::string& status={});
// Original in-race message glyphs (0x5e540..0x5e64e). The caller composes
// object 51 first and decides when the message is active or blinking.
void drawNativeGameMessage(const NativeMemory&,std::vector<std::uint32_t>&,
    const std::array<std::uint32_t,16>& palette,unsigned index,unsigned mode,bool blink=false);
} // namespace scr::runtime
