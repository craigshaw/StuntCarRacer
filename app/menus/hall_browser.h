#pragma once
#include "menus/native_menu_navigation.h"
#include "persistence/shared_hall_store.h"

namespace scr::runtime {
enum class HallBrowserFocus {Track,League,Category,Records,Page};
enum class HallBrowserAction {None,Load,Back,Challenge};
// Presentation/navigation state only. No filesystem, native RAM or race rules.
class HallBrowser {
public:
    void open(std::string driver,HallBoardKey initial,const NativeMenuInput& held={});
    HallBrowserAction update(double elapsed,const NativeMenuInput& held);
    void block(const NativeMenuInput& held){clock_.enterScreen(held);}
    void loading(){loading_=true;}
    void apply(const HallBrowserRead&);
    HallBoardKey board()const noexcept{return board_;}
    HallBrowserFocus focus()const noexcept{return focus_;}
    unsigned selected()const noexcept{return selected_;}
    unsigned page()const noexcept{return std::min(selected_/10,pages()-1);}
    unsigned pages()const noexcept{return unsigned(std::max<std::size_t>(1,(entries().size()+9)/10));}
    const std::vector<HallEntry>& entries()const noexcept;
    unsigned rank(unsigned row)const{return hall_->rank(board_,row);}
    bool ghost(unsigned row)const{return row<ghosts_.size()&&ghosts_[row];}
    bool busy()const noexcept{return loading_;}
    bool unavailable()const noexcept{return unavailable_;}
    bool canChallenge()const{return !loading_&&focus_==HallBrowserFocus::Records&&board_.kind==HallRecordKind::Lap&&selected_<entries().size()&&ghost(selected_);}
    const std::string& driver()const noexcept{return driver_;}
private:
    HallBoardKey board_;
    HallBrowserFocus focus_{HallBrowserFocus::Track};
    bool initialised_{},loading_{true},unavailable_{};
    unsigned selected_{};
    std::optional<HallAttemptId> selectedId_;
    std::string driver_;
    NativeMenuInputClock clock_;
    std::shared_ptr<const HallOfFame> hall_;
    std::array<bool,hallEntriesPerBoard> ghosts_{};
    void selection(unsigned);
    void move(int);
    void changePage(int);
};
} // namespace scr::runtime
