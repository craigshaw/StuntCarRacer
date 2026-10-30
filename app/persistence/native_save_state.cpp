#include "persistence/native_save_state.h"
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace scr::runtime {
namespace {
unsigned word(const NativeMemory& m,unsigned a){return (unsigned(m[a])<<8)|m[a+1];}
unsigned longword(const NativeMemory& m,unsigned a){return (word(m,a)<<16)|word(m,a+2);}
void putWord(NativeMemory& m,unsigned a,unsigned v){m[a]=std::uint8_t(v>>8);m[a+1]=std::uint8_t(v);}
void putLong(NativeMemory& m,unsigned a,unsigned v){putWord(m,a,v>>16);putWord(m,a+2,v);}
std::uint8_t randomByte(NativeMemory& m){
    const auto seed=longword(m,0x625a2);
    const auto value=std::uint8_t((std::uint16_t(seed)>>4)^((seed>>16)>>1));
    putLong(m,0x625a2,(seed<<8)|m[0x625a6]);m[0x625a6]=value;return value;
}
bool validKind(NativeSaveKind kind){return kind==NativeSaveKind::Career||kind==NativeSaveKind::Multiplayer||kind==NativeSaveKind::Records;}
bool obfuscateBlock(NativeMemory& m){
    const auto pointer=longword(m,0x1bcc0);
    if(pointer>m.size()-256)throw std::invalid_argument("Invalid original save transfer pointer");
    const bool decode=(m[0x1bb16]&0x80)!=0;
    if(!decode)m[pointer+0xef]=m[pointer+0x1f]=m[pointer+0xff]=0;
    for(unsigned index=0;index<256;++index){
        if(index==15)continue;
        std::uint8_t count=0,value{};
        do{
            m[0x1bb18]=m[pointer+index];m[0x1bb17]=count;
            value=m[0x7a81a+m[0x60c86]];++m[0x60c86];++count;
        }while(decode?count!=m[0x1bb18]:value!=m[0x1bb18]);
        m[pointer+index]=decode?value:count;
        m[0x60c86]=std::uint8_t(m[0x60c86]+m[0x7a81a+count]);
    }
    if(decode&&(m[pointer+0xef]|m[pointer+0xff]|m[pointer+0x1f])){m[0x1bb94]=0x81;return false;}
    return true;
}
bool initializeObfuscation(NativeMemory& m,bool decode){
    m[0x1bb16]=decode?0x80:0;putLong(m,0x1bcc0,0x7a41a);putWord(m,0x5f720,0x683b);
    for(unsigned i=0;i<256;++i){
        const auto state=std::uint16_t(word(m,0x5f720)*5);putWord(m,0x5f720,state);
        m[0x7a81a+i]=std::uint8_t(state>>2);
    }
    if(decode)m[0x60c86]=m[0x7a429];else m[0x7a429]=m[0x60c86];
    return obfuscateBlock(m);
}
bool transferMultiplayer(NativeMemory& m){
    if(m[0x1ca31]){
        std::copy_n(m.begin()+0x1ecea,128,m.begin()+0x7a43a);
        std::copy_n(m.begin()+0x1c9de,60,m.begin()+0x7a4ba);
        std::copy_n(m.begin()+0x1ca36,12,m.begin()+0x7a4fa);
        m[0x7a4f6]=m[0x5eb76];return initializeObfuscation(m,false);
    }
    if(!initializeObfuscation(m,true))return false;
    std::copy_n(m.begin()+0x7a43a,128,m.begin()+0x1ecea);
    std::copy_n(m.begin()+0x7a4ba,60,m.begin()+0x1c9de);
    std::copy_n(m.begin()+0x7a4fa,12,m.begin()+0x1ca36);
    m[0x5eb76]=m[0x7a4f6];return true;
}
bool transferRecords(NativeMemory& m){
    if(m[0x1ca31]){
        std::copy_n(m.begin()+0x7a61a,256,m.begin()+0x7a41a);
        std::copy_n(m.begin()+0x7a71a,256,m.begin()+0x7a51a);
        (void)initializeObfuscation(m,false);
        putLong(m,0x1bcc0,longword(m,0x1bcc0)+256);return obfuscateBlock(m);
    }
    (void)initializeObfuscation(m,true);
    putLong(m,0x1bcc0,longword(m,0x1bcc0)+256);
    if(!obfuscateBlock(m))return false;
    for(unsigned table=0;table<2;++table)for(unsigned slot=0;slot<256;slot+=16){
        const unsigned source=0x7a41a+256*table+slot,destination=source+512;
        bool better=false;
        for(unsigned field=12;field<15;++field){
            if(m[source+field]==m[destination+field])continue;
            better=m[source+field]<m[destination+field];break;
        }
        if(better)std::copy_n(m.begin()+source,16,m.begin()+destination);
    }
    return true;
}
bool validName(const NativeMemory& m,unsigned address){
    for(unsigned i=0;i<12;++i)if(m[address+i]<32||m[address+i]>126)return false;
    return true;
}
bool validCareer(const NativeMemory& m){
    if(m[0x1c9ce]>3||m[0x1c9cf]>10||(m[0x1c9d0]!=0&&m[0x1c9d0]!=11)||!validName(m,0x1ed5b))return false;
    unsigned seen=0;
    for(unsigned i=0;i<12;++i){const auto id=m[0x1c9c2+i];if(id>=12||(seen&(1u<<id)))return false;seen|=1u<<id;}
    return true;
}
bool bcd(unsigned value){return (value&15)<10&&(value>>4)<10;}
bool validRecords(const NativeMemory& m){
    for(unsigned table=0;table<2;++table)for(unsigned entry=0;entry<16;++entry){
        const unsigned address=0x7a41a+256*table+16*entry;
        if(!validName(m,address)||m[address+12]>9||!bcd(m[address+13])||m[address+13]>=0x60||!bcd(m[address+14]))return false;
    }
    return true;
}
bool validMultiplayer(const NativeMemory& m){
    if(!m[0x5eb76]||m[0x5eb76]>7)return false;
    for(unsigned player=0;player<8;++player)if(!validName(m,0x1eceb+16*player))return false;
    for(unsigned player=0;player<12;++player)if(m[0x1ca36+player]>10)return false;
    return true;
}
} // namespace

bool encodeDecodeNativeLeague(NativeMemory& m,std::uint8_t mode){
    const bool decode=(mode&0x80)!=0;
    for(unsigned attempt=0;attempt<256;++attempt){
        m[0x1bb16]=mode;
        if(decode&&!(m[0x1ca34]&0x80))return true;
        for(unsigned i=0;i<5;++i){if(decode)m[0x625a2+i]=m[0x7a039-i];else m[0x7a039-i]=m[0x625a2+i];}
        if(!decode)for(unsigned i=0;i<12;++i)m[0x1c9b6+i]=m[0x1ed5b+i]^0x3b;
        bool failed=false;
        for(unsigned index=27;index>0;){
            --index;
            if(decode){m[0x1bb18]=m[0x7a01a+index];m[0x1bb19]=m[0x7a03f+index];}
            std::uint8_t block=0;m[0x1bb1a]=0;
            while(true){
                ++m[0x1bb1a];if(!m[0x1bb1a]&&++block==128){failed=true;break;}
                m[0x1bb17]=randomByte(m);
                if(decode){
                    if(block!=m[0x1bb18]||m[0x1bb1a]!=m[0x1bb19])continue;
                    m[0x7a41a+index]=m[0x1bb17];break;
                }
                if(m[0x1bb17]!=m[0x1c9b6+index])continue;
                m[0x7a01a+index]=block;m[0x7a03f+index]=m[0x1bb1a];break;
            }
            if(failed)break;
        }
        if(!failed)for(unsigned i=0;i<5;++i){
            if(decode){if(m[0x625a2+i]!=m[0x7a03e-i]){failed=true;break;}}
            else m[0x7a03e-i]=m[0x625a2+i];
        }
        if(failed){m[0x625a5]=0x3b;if(decode)return false;continue;}
        if(decode){
            for(unsigned i=0;i<27;++i)if(!m[0x5eb76]||i>=24)m[0x1c9b6+i]=m[0x7a41a+i];
            for(unsigned i=0;i<12;++i)m[0x1ed5b+i]=m[0x1c9b6+i]^0x3b;
        }
        m[0x1ca34]=0x80;return true;
    }
    throw std::runtime_error("Original league encoder could not find its random sequence");
}

bool syncNativeSaveTransfer(NativeMemory& m,std::uint8_t direction){
    if(direction!=m[0x1ca31]||(m[0x1bb94]&0x80)||(m[0x1bb6b]&0x80))return true;
    if(!m[0x1bb6b]){
        const unsigned source=m[0x1ca31]?0x7a01a:0x7a41a,target=m[0x1ca31]?0x7a41a:0x7a01a;
        std::copy_n(m.begin()+source,192,m.begin()+target);return true;
    }
    return m[0x1bb6b]==0x40?transferRecords(m):transferMultiplayer(m);
}

void beginNativeSaveTransaction(NativeMemory& m,bool write){
    m[0x1ca31]=write?1:0;
    std::copy_n(m.begin()+0x7a01a,256,m.begin()+0x1c280);m[0x1bbd0]=m[0x1ca34];
    (void)encodeDecodeNativeLeague(m,0);
}
void restoreNativeSaveTransaction(NativeMemory& m){
    m[0x1ca34]=m[0x1bbd0];std::copy_n(m.begin()+0x1c280,256,m.begin()+0x7a01a);
}
NativeSavePayload makeNativeSavePayload(NativeMemory& m,NativeSaveKind kind){
    if(!validKind(kind)||m[0x1ca31]!=1)throw std::invalid_argument("Save payload requires an active write transaction");
    m[0x1bb6b]=std::uint8_t(kind);m[0x1bb94]=0;
    if(!syncNativeSaveTransfer(m,1))throw std::runtime_error("Original save encoder rejected the transfer block");
    NativeSavePayload payload{};std::copy_n(m.begin()+0x7a41a,payload.size(),payload.begin());return payload;
}
bool applyNativeSavePayload(NativeMemory& m,NativeSaveKind kind,const NativeSavePayload& payload){
    if(!validKind(kind))return false;
    auto candidate=std::make_unique<NativeMemory>(m);
    auto& state=*candidate;state[0x1ca31]=0;state[0x1bb6b]=std::uint8_t(kind);state[0x1bb94]=0;
    std::copy(payload.begin(),payload.end(),state.begin()+0x7a41a);
    if(!syncNativeSaveTransfer(state,0)||(state[0x1bb94]&0x80))return false;
    if(kind==NativeSaveKind::Career){if(!encodeDecodeNativeLeague(state,0x80)||!validCareer(state))return false;}
    else if(kind==NativeSaveKind::Multiplayer){if(!validMultiplayer(state))return false;}
    else if(!validRecords(state))return false;
    m=std::move(state);return true;
}
bool prepareNativeReplay(NativeMemory& m){
    initializeNativeGameTables(m);
    if(!m[0x5eb76]&&encodeDecodeNativeLeague(m,0x80))return true;
    initializeNativeGameData(m);return false;
}
} // namespace scr::runtime
