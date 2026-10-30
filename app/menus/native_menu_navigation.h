#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace scr::runtime {
enum class NativeMenuNavigation { Original,Responsive };
struct NativeMenuInput {
    // Original up/down/left/right/fire bits and row-selection keys.
    std::uint8_t controls{},numberedKeys{};
    bool back{};
};

// Host-time input policy only. It never reads or writes recovered game state.
class NativeMenuInputClock {
public:
    NativeMenuInput sample(const NativeMenuInput& held,double elapsed){
        if(!std::isfinite(elapsed)||elapsed<0)throw std::invalid_argument("Invalid navigation elapsed time");
        blocked_&=held.controls;blockedNumbers_&=held.numberedKeys;
        blockedBack_=blockedBack_&&held.back;
        const auto available=std::uint8_t(held.controls&~blocked_);
        const auto fresh=std::uint8_t(available&~previous_);
        NativeMenuInput pulse{std::uint8_t(fresh&16),std::uint8_t(held.numberedKeys&~numbers_&~blockedNumbers_),
            held.back&&!back_&&!blockedBack_};
        unsigned direction=0;
        for(unsigned bit:{1u,2u,4u,8u})if(available&bit){direction=bit;break;}
        if(direction!=direction_){direction_=direction;remaining_=0.3;if(direction)pulse.controls|=std::uint8_t(direction);}
        else if(direction){
            remaining_-=elapsed;
            if(remaining_<=1e-9){pulse.controls|=std::uint8_t(direction);remaining_=0.1-std::fmod(std::max(0.0,-remaining_),0.1);}
        }
        previous_=held.controls;numbers_=held.numberedKeys;back_=held.back;
        if(pulse.back){pulse.controls=0;pulse.numberedKeys=0;}
        return pulse;
    }
    void enterScreen(const NativeMenuInput& held)noexcept{
        blocked_=held.controls;blockedNumbers_=held.numberedKeys;blockedBack_=held.back;
        previous_=held.controls;numbers_=held.numberedKeys;back_=held.back;direction_=0;remaining_=0.3;
    }
    void clear()noexcept{*this={};}
private:
    std::uint8_t previous_{},numbers_{},blocked_{},blockedNumbers_{};
    bool back_{},blockedBack_{};
    unsigned direction_{};
    double remaining_{0.3};
};
} // namespace scr::runtime
