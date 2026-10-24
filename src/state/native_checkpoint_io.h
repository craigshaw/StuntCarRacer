#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace scr::state::checkpoint {
using Bytes=std::vector<std::uint8_t>;
inline std::uint64_t hash(const std::uint8_t* data,std::size_t size){
    std::uint64_t value=14695981039346656037ULL;
    for(std::size_t n=0;n<size;++n){value^=data[n];value*=1099511628211ULL;}return value;
}
struct Writer {
    Bytes data;
    void u32(std::uint32_t value){for(unsigned n=0;n<4;++n)data.push_back(std::uint8_t(value>>(n*8)));}
    void u64(std::uint64_t value){u32(std::uint32_t(value));u32(std::uint32_t(value>>32));}
    void bytes(const std::uint8_t* value,std::size_t size){if(size)data.insert(data.end(),value,value+size);}
    void text(const std::string& value){u32(std::uint32_t(value.size()));bytes(reinterpret_cast<const std::uint8_t*>(value.data()),value.size());}
};
struct Reader {
    const Bytes& data;std::size_t position{};
    void require(std::size_t size)const{if(size>data.size()-position)throw std::runtime_error("Truncated checkpoint");}
    std::uint32_t u32(){require(4);std::uint32_t value=0;for(unsigned n=0;n<4;++n)value|=std::uint32_t(data[position++])<<(n*8);return value;}
    std::uint64_t u64(){const auto lo=u32();return lo|(std::uint64_t(u32())<<32);}
    void bytes(std::uint8_t* target,std::size_t size){require(size);for(std::size_t n=0;n<size;++n)target[n]=data[position++];}
    std::string text(std::size_t maximum){const auto size=u32();if(size>maximum)throw std::runtime_error("Checkpoint text exceeds limit");require(size);std::string value(data.begin()+position,data.begin()+position+size);position+=size;return value;}
    void end()const{if(position!=data.size())throw std::runtime_error("Trailing checkpoint data");}
};
} // namespace scr::state::checkpoint
