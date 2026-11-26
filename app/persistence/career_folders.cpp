#include "persistence/career_folders.h"
#include "persistence/career_file_io.h"
namespace scr::runtime {
CareerFolders::CareerFolders(std::filesystem::path root,NativeCareerStore::Validator validator):
    SavedGameFolders(std::move(root),{L"careers",L"career.scr-career",
        [validator](const auto& path){return NativeCareerStore::readFile(path,1,validator);},
        [](const auto& path){return career_files::readGeneration(path,1);},
        [](const auto& data){return decodeCareerInfo(data);}}){}
} // namespace scr::runtime
