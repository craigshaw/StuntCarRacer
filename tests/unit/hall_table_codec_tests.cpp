#include "persistence/hall_table_codec.h"
#include "hall_fixture.h"
#include <iostream>

namespace {
using namespace scr::runtime;namespace fixture=scr::test::hall;
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejected(F call){bool caught=false;try{call();}catch(const std::exception&){caught=true;}require(caught,"Invalid shared Hall table was accepted");}
void checksum(scr::state::checkpoint::Bytes& bytes){const auto h=scr::state::checkpoint::hash(bytes.data(),bytes.size()-8);for(unsigned n=0;n<8;++n)bytes[bytes.size()-8+n]=std::uint8_t(h>>(n*8));}
}
int main()try{
    HallOfFame hall(fixture::catalog);
    for(unsigned b=0;b<32;++b)for(unsigned n=0;n<20;++n){auto a=fixture::attempt(1+b*20+n,4200+n,hallBoardKey(b),"ABCDEFGHIJKL");
        if(a.board.kind==HallRecordKind::Lap){a.ghost=HallGhostDigest{};(*a.ghost)[0]=1;}hall.offer(std::move(a));}
    const auto bytes=encodeHallTable(hall);require(bytes.size()<=hallTableMaximumBytes,"Full Hall exceeded format byte bound");
    require(encodeHallTable(decodeHallTable(bytes,fixture::catalog))==bytes,"Full Hall lost identity/time/order/ghost references");
    const auto empty=encodeHallTable(HallOfFame(fixture::catalog));require(decodeHallTable(empty,fixture::catalog).entries({}).empty(),"Fresh Hall gained old records");
    for(std::size_t n=0;n<empty.size();++n){auto truncated=empty;truncated.resize(n);rejected([&]{decodeHallTable(truncated,fixture::catalog);});}
    for(std::size_t offset:{0u,12u,28u,32u,60u}){auto corrupt=bytes;corrupt[offset]^=0xff;rejected([&]{decodeHallTable(corrupt,fixture::catalog);});}
    auto count=bytes;count[28]=21;checksum(count);rejected([&]{decodeHallTable(count,fixture::catalog);});
    auto zeroId=bytes;for(unsigned n=0;n<16;++n)zeroId[32+n]=0;checksum(zeroId);rejected([&]{decodeHallTable(zeroId,fixture::catalog);});
    auto trailing=bytes;trailing.insert(trailing.end()-8,0);checksum(trailing);rejected([&]{decodeHallTable(trailing,fixture::catalog);});
    auto future=bytes;future[4]=2;checksum(future);bool unsupported=false;
    try{decodeHallTable(future,fixture::catalog);}catch(const UnsupportedHallTable&){unsupported=true;}require(unsupported,"Future Hall version was not preserved as unsupported");
    auto wrong=fixture::catalog;++wrong.rules;rejected([&]{decodeHallTable(bytes,wrong);});
    wrong=fixture::catalog;++wrong.trackContent;rejected([&]{decodeHallTable(bytes,wrong);});
    auto huge=bytes;huge.resize(hallTableMaximumBytes+1);rejected([&]{decodeHallTable(huge,fixture::catalog);});
    // A roundtrip/retry must keep first arrival order rather than assigning new
    // rows or insertion order from file generation.
    auto restored=decodeHallTable(bytes,fixture::catalog);const auto first=hall.entries({}).front().attempt;
    require(restored.offer(first)==HallAdmission::Duplicate&&encodeHallTable(restored)==bytes,"Reopened retry changed the table");
    std::cout<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"full_entries\":640,\"maximum_fixture_bytes\":"<<bytes.size()<<",\"format_byte_limit\":"<<hallTableMaximumBytes<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
