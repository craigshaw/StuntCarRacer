#pragma once
#include "native_asset_initialization.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace scr::viewer {
enum class NativeTrackPreviewAction { none,redraw,startRace };
// Original Practice overview. Owns its rendering scratch so preview work never
// changes the live race state. Controls use the original up/down/left/right/fire
// bits 1/2/4/8/16. Persist angle() when leaving the screen.
// The Draw Bridge gets the original screen-entry initializer once; the
// original overview holds that geometry while waiting and rotating.
class NativeTrackPreview {
public:
    NativeTrackPreview();
    ~NativeTrackPreview();
    void load(const NativeMemory&,std::uint8_t angle=0);
    NativeTrackPreviewAction input(std::uint8_t controls);
    [[nodiscard]] const std::vector<std::uint32_t>& pixels() const noexcept;
    [[nodiscard]] const std::vector<std::uint8_t>& indices() const noexcept;
    [[nodiscard]] std::uint8_t angle() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace scr::viewer
