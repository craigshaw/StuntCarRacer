#pragma once
#include "native_menu_render.h"

namespace scr::viewer {
// Incremental original name-entry pixels. Only accepted characters and the
// original 0x7f erase operation should be supplied by the input state machine.
// A blank retry retains the screen and begins another attempt on the same row.
class NativeOpeningNameScreen {
public:
    explicit NativeOpeningNameScreen(const NativeMemory&);
    void beginAttempt();
    void drawCharacter(unsigned originalCharacter);
    void complete();
    [[nodiscard]] const NativeMenuImage& image() const { return image_; }
    [[nodiscard]] unsigned column() const { return cursor_.column; }
private:
    const NativeMemory& memory_;
    NativeMenuImage image_;
    NativeMenuTextCursor cursor_;
    unsigned paper_{2};
};

// Original multiplayer league table at the end of startup, after the league
// setup/sort state has been prepared by NativeOpeningFlow.
[[nodiscard]] NativeMenuImage makeNativeOpeningStandingsImage(const NativeMemory&);
} // namespace scr::viewer
