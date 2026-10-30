#pragma once
#include "input/drive_input.h"
#include <chrono>

namespace scr::runtime {
struct PolledDrivePad {DrivePad pad;bool pausePressed{},resetPressed{},disconnected{};};
class WindowsDriveInput {
public:PolledDrivePad poll();
private:
    unsigned slot_{4};std::uint16_t previousButtons_{};
    std::chrono::steady_clock::time_point nextSearch_{};
};
} // namespace scr::runtime
