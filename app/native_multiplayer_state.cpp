#include "native_multiplayer_state.h"
#include <algorithm>
#include <stdexcept>

namespace scr::viewer {
namespace {
void validate(const NativeMemory& m){
    if(m[0x57c3c]||!m[0x5eb76]||m[0x5eb76]>7||m[0x1c9ce]>3)
        throw std::invalid_argument("Local championship requires two through eight drivers");
}
void configuration(NativeMemory& m){
    validate(m);m[0x1bc5b]=std::uint8_t(11-m[0x5eb76]);m[0x1bc59]=12;
}
unsigned player(const NativeMemory& m){
    validate(m);const unsigned id=m[0x5eb79];
    if(id<4||id>11)throw std::invalid_argument("Invalid local championship driver");return id;
}
unsigned word(const NativeMemory& m,unsigned a){return (unsigned(m[a])<<8)|m[a+1];}
unsigned longword(const NativeMemory& m,unsigned a){return (word(m,a)<<16)|word(m,a+2);}
void putWord(NativeMemory& m,unsigned a,unsigned value){m[a]=std::uint8_t(value>>8);m[a+1]=std::uint8_t(value);}
void putLong(NativeMemory& m,unsigned a,unsigned value){putWord(m,a,value>>16);putWord(m,a+2,value);}
std::uint8_t randomByte(NativeMemory& m){
    const auto seed=longword(m,0x625a2);
    const auto value=std::uint8_t((std::uint16_t(seed)>>4)^((seed>>16)>>1));
    putLong(m,0x625a2,(seed<<8)|m[0x625a6]);m[0x625a6]=value;return value;
}
void clearTime(NativeMemory& m,unsigned slot){m[0x1c908+slot]=m[0x1c920+slot]=m[0x1c938+slot]=0;}
}
void randomizeNativeMultiplayerCars(NativeMemory& m){
    validate(m);m[0x5eb77]=1;
    for(int car=1;car>=0;--car){
        m[0x1bb1a]=std::uint8_t(car);unsigned choice=unsigned(car);
        if(m[0x1c9ce]){
            choice=std::uint8_t((randomByte(m)&1)+1+m[0x5eb7e]);
            if(choice>=3)choice=std::uint8_t(choice-3);
            m[0x5eb7e]=std::uint8_t(choice);
        }
        choice=std::uint8_t(choice+m[0x5ab42+m[0x1c9ce]]);
        if(choice>=12)throw std::invalid_argument("Invalid original pace car choice");
        m[0x59140+unsigned(car)]=std::uint8_t(choice);m[0x1ed8a+unsigned(car)]=m[0x7a9fa+choice];
        std::copy_n(m.begin()+0x7a91a+16*choice,16,m.begin()+0x1ecaa+16*unsigned(car));
    }
    m[0x5eb78]=std::uint8_t(m[0x5eb76]+1);m[0x5eb7b]=0;
}
void initializeNativeMultiplayerTrack(NativeMemory& m){
    validate(m);
    for(unsigned slot=24;slot>0;){--slot;clearTime(m,slot);if(slot<16)m[0x1c908+slot]=9;}
    m[0x1ca23]=m[0x1ca24]=0;
}
void initializeNativeMultiplayerSeason(NativeMemory& m){
    validate(m);encodeNativeSeason(m);randomizeNativeMultiplayerCars(m);initializeNativeMultiplayerTrack(m);
}
void configureNativeMultiplayerRace(NativeMemory& m){
    configuration(m);
    if(m[0x1ca35]>m[0x5eb76]||m[0x5eb77]>1)throw std::invalid_argument("Invalid local championship fixture");
    m[0x1ca27]=m[0x5eb79]=std::uint8_t(11-m[0x1ca35]);m[0x1ca28]=m[0x5eb77];
}
void prepareNativeMultiplayerRace(NativeMemory& m){
    validate(m);m[0x1bb5f]=m[0x1c9ce];configureNativeMultiplayerRace(m);
    m[0x1ca29]=m[0x1ca28];m[0x1bbb1]=0xc0;m[0x57c62]=0x11;
    const unsigned ordinal=2*m[0x1c9ce]+((m[0x5eb77]^1)&1);
    m[0x1ca33]=m[0x648c2+ordinal];m[0x1ca21]=m[0x648ca+(ordinal^(m[0x1c9d0]?1:0))];
    transferNativeLapRecords(m,true);
}
void loadNativeMultiplayerDamage(NativeMemory& m){m[0x1c9cf]=m[0x1ca36+player(m)];}
void finishNativeMultiplayerRaceExit(NativeMemory& m){
    const unsigned id=player(m);if(!(m[0x1ca22]&0x80))return;
    m[0x1ca36+id]=m[0x1c9cf];if(m[0x1bb20]!=4)clearTime(m,id+12);
    m[0x1c9cf]=m[0x1bbb5];
}
void transferNativeMultiplayerControls(NativeMemory& m,bool load){
    const unsigned offset=5*(player(m)-4);m[0x1bb1b]=load?0x80:0;
    for(unsigned key=0;key<5;++key){
        if(load)m[0x60c88+key]=m[0x5fb3a+offset+key];else m[0x5fb3a+offset+key]=m[0x60c88+key];
    }
}
void sortNativeMultiplayerStandings(NativeMemory& m){
    configuration(m);const unsigned begin=m[0x1bc5b],end=m[0x1bc59];
    for(unsigned slot=begin;slot<end;++slot){
        const auto id=m[0x1c9c2+slot];if(id>=12)throw std::invalid_argument("Invalid local standings driver");
        m[0x1ca0e+slot]=id;m[0x1ca02+id]=std::uint8_t(2*m[0x1c9de+id]+m[0x1c9ea+id]);
    }
    do{
        m[0x1bb19]=0;
        for(unsigned slot=begin;slot+1<end;++slot){
            m[0x1bb18]=std::uint8_t(slot);
            const auto first=m[0x1ca0e+slot],second=m[0x1ca0e+slot+1];
            const auto a=std::int8_t(m[0x1ca02+first]),b=std::int8_t(m[0x1ca02+second]);
            bool swap=a<b;
            if(a==b){const auto firstWins=std::int8_t(m[0x1c9de+first]),secondWins=std::int8_t(m[0x1c9de+second]);
                swap=firstWins<secondWins||(firstWins==secondWins&&first<second);}
            if(swap){m[0x1bb1a]=second;m[0x1ca0e+slot]=second;m[0x1ca0e+slot+1]=first;++m[0x1bb19];}
        }
    }while(m[0x1bb19]);
}
void scoreNativeMultiplayerRace(NativeMemory& m){
    validate(m);
    for(unsigned id=12;id>0;){--id;m[0x1c9d2+id]=std::uint8_t((randomByte(m)&63)+m[0x5aaf4+id]);}
    for(unsigned division=0;division<4;++division){
        m[0x1bb5f]=std::uint8_t(division);if(division!=m[0x1c9ce])continue;
        configureNativeMultiplayerRace(m);
        m[0x1bbf7]=randomByte(m)>=0xa0?0x40:0;
        const auto first=m[0x1ca27],second=m[0x1ca28];
        ++m[0x1c9f6+first];++m[0x1c9f6+second];
        m[0x1bbf7]=m[0x1bbb4]^0xc0;
        m[0x1bb19]=(m[0x1bbf7]&0x80)?first:second;
        m[0x1bb18]=(m[0x1bbf7]&0x40)?first:second;
        ++m[0x1c9de+m[0x1bb19]];++m[0x1c9ea+m[0x1bb18]];
        m[0x1ca26]=m[0x1bb18];m[0x1ca25]=m[0x1bb19];sortNativeMultiplayerStandings(m);
    }
}
void awardNativeMultiplayerTrackBonuses(NativeMemory& m){
    validate(m);
    if(m[0x1ca24]){if(m[0x1ca24]>=12)throw std::invalid_argument("Invalid race record holder");++m[0x1c9de+m[0x1ca24]];}
    if(m[0x1ca23]){if(m[0x1ca23]>=12)throw std::invalid_argument("Invalid lap record holder");++m[0x1c9ea+m[0x1ca23]];}
    sortNativeMultiplayerStandings(m);
}
void forfeitNativeMultiplayerTrack(NativeMemory& m){
    validate(m);m[0x1bb5b]=m[0x1bbb4]=0xc0;
    do{m[0x1bb5f]=m[0x1c9ce];configureNativeMultiplayerRace(m);scoreNativeMultiplayerRace(m);}
    while(advanceNativeMultiplayerRace(m));
}
bool advanceNativeMultiplayerRace(NativeMemory& m){
    validate(m);advanceNativeSeasonRound(m);
    if(m[0x1ca35]<m[0x5eb78])return true;m[0x1ca35]=0;return false;
}
NativeMultiplayerTrackAdvance advanceNativeMultiplayerTrack(NativeMemory& m){
    validate(m);m[0x1bb5b]=0;--m[0x5eb77];
    if(!(m[0x5eb77]&0x80)){initializeNativeMultiplayerTrack(m);return NativeMultiplayerTrackAdvance::NextTrack;}
    ++m[0x57c67];
    if(m[0x57c67]>=4){initializeNativeGameTables(m);return NativeMultiplayerTrackAdvance::ChampionshipReset;}
    return NativeMultiplayerTrackAdvance::ChampionshipComplete;
}
} // namespace scr::viewer
