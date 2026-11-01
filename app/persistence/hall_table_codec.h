#pragma once
#include "records/hall_of_fame.h"
#include "state/native_checkpoint_io.h"
#include <stdexcept>

namespace scr::runtime {
constexpr unsigned hallTableFormatVersion=1,hallTableMaximumBytes=65536;
class UnsupportedHallTable:public std::runtime_error {public:using std::runtime_error::runtime_error;};
state::checkpoint::Bytes encodeHallTable(const HallOfFame&);
HallOfFame decodeHallTable(const state::checkpoint::Bytes&,HallCatalog expected);
} // namespace scr::runtime
