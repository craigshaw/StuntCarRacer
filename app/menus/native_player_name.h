#pragma once
#include "assets/native_asset_initialization.h"
#include <cstdint>
#include <vector>

namespace scr::runtime {
// Amiga raw key codes, not translated host text. A key-down interrupt leaves
// 0xb3; scanForInput consumes it. Shift remains held until its key-up interrupt.
void setNativeRawKey(NativeMemory&,unsigned rawCode,bool pressed);
struct NativeInputScan { bool available{};std::uint16_t code{}; };
[[nodiscard]] NativeInputScan scanNativeNameInput(NativeMemory&,std::uint8_t controls);

struct NativeTextInputState {
    std::uint8_t offset{},maximumLength{12},position{};
    bool complete{};
};
struct NativeTextInputUpdate { bool drewCharacter{},complete{};std::uint8_t character{}; };
// One original inputPlayerNameLoop iteration after waitForInputPress returns.
// Exposed separately so the raw scanner and text rules can be checked against
// their original routines independently of screen drawing.
[[nodiscard]] NativeTextInputUpdate applyNativeTextInputCode(
    NativeMemory&,NativeTextInputState&,std::uint16_t code);

struct NativePlayerNameUpdate {
    bool beganAttempt{},retriedBlank{},complete{};
    std::vector<std::uint8_t> characters;
};
class NativePlayerNameEntry {
public:
    explicit NativePlayerNameEntry(NativeMemory& memory):memory_(memory){}
    [[nodiscard]] NativePlayerNameUpdate begin();
    [[nodiscard]] NativePlayerNameUpdate beginResponsive(unsigned length);
    [[nodiscard]] NativePlayerNameUpdate update(std::uint8_t controls);
    [[nodiscard]] bool complete()const noexcept{return phase_==Phase::Done;}
    [[nodiscard]] const NativeTextInputState& state()const noexcept{return state_;}
private:
    enum class Phase { Release,Input,Done };
    NativeMemory& memory_;
    NativeTextInputState state_;
    Phase phase_{Phase::Done};
    void startTextInput();
};
} // namespace scr::runtime
