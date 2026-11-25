#include "persistence/player_data_layout.h"
#include <stdexcept>

namespace scr::runtime {
PlayerDataLayout playerDataLayout(const std::filesystem::path& directory){
    if(directory.empty())throw std::invalid_argument("The game data directory cannot be empty");
    const auto root=std::filesystem::weakly_canonical(std::filesystem::absolute(directory)).lexically_normal().make_preferred();
    if(root==root.root_path())throw std::invalid_argument("Choose a game data folder below the drive root");
    return {root,root/"assets"/"stunt-car-racer.scr-assets",root/"settings.ini",
        root/"careers",root/"multiplayer",
        {root/"hall-of-fame"/"original",HallDirectoryAccess::Inherited},
        {root/"hall-of-fame"/"enhanced",HallDirectoryAccess::Inherited}};
}
PlayerDataLayout portablePlayerDataLayout(const std::filesystem::path& executable){
    if(!executable.is_absolute()||executable.filename().empty())
        throw std::invalid_argument("Portable data resolution needs an absolute executable filename");
    return playerDataLayout(executable.parent_path()/"data");
}
} // namespace scr::runtime
