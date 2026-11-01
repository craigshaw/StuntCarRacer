#pragma once
#include "records/hall_of_fame.h"

namespace scr::test::hall {
inline constexpr runtime::HallCatalog catalog{0x123456789abcdef0ULL,1};
inline runtime::HallAttemptId id(std::uint64_t value){runtime::HallAttemptId result{};for(unsigned n=0;n<8;++n)result[n]=std::uint8_t(value>>(n*8));return result;}
inline std::uint8_t bcd(unsigned n){return std::uint8_t(((n/10)<<4)|(n%10));}
inline PackedLapTime time(unsigned hundredths){return {bcd(hundredths/6000),bcd((hundredths/100)%60),bcd(hundredths%100)};}
inline runtime::HallAttempt attempt(std::uint64_t identity,unsigned hundredths=4217,runtime::HallBoardKey key={},std::string name="Craig"){
    return {id(identity),key,std::move(name),time(hundredths),{}};
}
} // namespace scr::test::hall
