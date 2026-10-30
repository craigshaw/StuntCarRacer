#pragma once
#include "presentation/native_menu_render.h"
#include "menus/native_player_name.h"
#include "menus/native_menu_navigation.h"
#include <array>
#include <memory>

namespace scr::runtime {
enum class NativeSaveMenuAction { None,Changed,ReadDirectory,Select,Cancel };
enum class NativeSaveMenuPhase { InsertDisc,Directory,FormatWarning,DirectionRelease,LoadSelection,SaveName,BlankName,Incompatible,InvalidData,Retry,RetryDelay,Complete,SaveSelection };
struct NativeSaveMenuResult {
    NativeSaveMenuAction action{NativeSaveMenuAction::None};
    std::uint8_t slot{},kind{};
    std::array<std::uint8_t,8> name{};
};
[[nodiscard]] std::uint8_t nativeSaveNameKind(const std::array<std::uint8_t,8>&)noexcept;
void commitNativeSaveDirectory(NativeMemory&,unsigned slot,
    const std::array<std::uint8_t,8>& name,bool write);
class NativeSaveMenu {
public:
    NativeSaveMenu(NativeMemory&,bool write,const NativeMenuImage* previousImage=nullptr);
    [[nodiscard]] NativeSaveMenuResult update(std::uint8_t controls,double elapsedSeconds);
    [[nodiscard]] NativeSaveMenuResult directoryLoaded();
    NativeSaveMenuResult back();
    void setNavigation(NativeMenuNavigation);
    void operationFailed(std::uint8_t originalError);
    void invalidSaveData();
    void complete();
    [[nodiscard]] NativeMenuImage image()const;
    [[nodiscard]] NativeSaveMenuPhase phase()const noexcept{return phase_;}
    [[nodiscard]] unsigned slot()const noexcept{return memory_[0x62bb4];}
    [[nodiscard]] bool writing()const noexcept{return write_;}
private:
    NativeMemory& memory_;
    std::unique_ptr<NativeMemory> drawing_;
    NativeSaveMenuPhase phase_{NativeSaveMenuPhase::InsertDisc};
    NativeSaveMenuAction pending_{NativeSaveMenuAction::ReadDirectory};
    NativeTextInputState text_{};
    bool write_{},release_{true};
    bool directoryReady_{};
    NativeMenuNavigation navigation_{NativeMenuNavigation::Original};
    unsigned invalidReads_{};
    double remaining_{};
    void frame();
    void glyph(unsigned);
    void paper(unsigned);
    void ink(unsigned);
    void position(unsigned,unsigned);
    void message(unsigned);
    void colouredMessage(unsigned);
    void allSlots();
    void displaySlot(bool edited=false);
    void selectSlot();
    void refreshSlot();
    void startText();
    void navigate(std::uint8_t);
    NativeSaveMenuResult selected();
    NativeSaveMenuResult result(NativeSaveMenuAction)const;
    void finish();
    NativeSaveMenuResult updateResponsive(std::uint8_t);
    void returnToSlots();
};
} // namespace scr::runtime
