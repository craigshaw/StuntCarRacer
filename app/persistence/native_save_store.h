#pragma once
#include <array>
#include <cstdint>
#include <filesystem>

namespace scr::runtime {
using NativeSaveBlock=std::array<std::uint8_t,512>;

// Local backing for the original directory sector22 and slot sectors23..52.
// An empty path keeps a private, temporary save medium for native regressions.
class NativeSaveStore {
public:
    explicit NativeSaveStore(std::filesystem::path file={});
    [[nodiscard]] std::uint8_t readDirectory(NativeSaveBlock&);
    [[nodiscard]] std::uint8_t readSlot(unsigned slot,NativeSaveBlock&);
    [[nodiscard]] std::uint8_t writeDirectory(const NativeSaveBlock&);
    [[nodiscard]] std::uint8_t writeSlot(unsigned slot,const NativeSaveBlock& directory,const NativeSaveBlock& payload);
private:
    using Image=std::array<NativeSaveBlock,31>;
    std::filesystem::path file_;
    Image temporary_{};
    std::uint8_t read(Image&);
    std::uint8_t commit(const Image&);
};
} // namespace scr::runtime
