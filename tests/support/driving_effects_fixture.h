#pragma once
#include "state/driving_effects_adapter.h"
#include "track_sampling_fixture.h"
#include <stdexcept>
#include <vector>

namespace scr::test::effects {
using namespace scr::state::effects;
#define SCR_DRIVING_EFFECT_WORDS(X) \
 X(enginePitchDelta,0x1bbf4) X(edgeSamplePeriod,0x1b6e2) X(groundSamplePeriod,0x1b732)
#define SCR_DRIVING_EFFECT_BYTES(X) \
 X(randomTail,0x625a6) X(engineNoiseFlag,0x5b646) X(grindSample,0xf360) X(impactLevel,0x1bbd7) \
 X(particleLimit,0x1bba7) X(particleScratch,0x1bbe4) X(bouncePhase,0x1bbda) X(wheelBaseHeight,0x1bbdd) X(spriteUpdatePending,0x69f41) \
 X(damageActive,0x1bb54) X(targetDamage,0x1bbb6) X(carDamage,0x1bb55) X(damageVerticalOffset,0x662aa) X(damageColor,0x662ab) \
 X(majorImpactCooldown,0x1bb73) X(holePosition,0x1c9cf) X(creakVolume,0x1b715) X(blinkCountdown,0x1bb6c) \
 X(gameModeFlags,0x1bbe2) X(postWreck,0x1bca2) X(visualEffectHighByte,0x1bc56) X(messageIndex,0x5e65b) X(messageMode,0x5e65a) X(messageActive,0x1bb8e)
inline const std::vector<unsigned>& outputBytes(){
    static const std::vector<unsigned> result=[] {
        std::vector<unsigned> v;
#define MARK_WORD(field,address) v.push_back(address);v.push_back(address+1);
        SCR_DRIVING_EFFECT_WORDS(MARK_WORD)
#undef MARK_WORD
#define MARK_BYTE(field,address) v.push_back(address);
        SCR_DRIVING_EFFECT_BYTES(MARK_BYTE)
#undef MARK_BYTE
        for(unsigned a=0x625a2;a<0x625a6;++a)v.push_back(a);
        for(unsigned a=0x1c380;a<0x1c480;++a)v.push_back(a);
        for(unsigned a=0x1bb4f;a<0x1bb52;++a)v.push_back(a);
        for(unsigned wheel:{0u,0x30u})for(unsigned part=0;part<3;++part){const unsigned record=0x6a16c+wheel+part*0x10;
            for(unsigned offset:{6u,0xau,0x25au,0x2bau}){v.push_back(record+offset);v.push_back(record+offset+1);}}
        return v;
    }();return result;
}
#undef SCR_DRIVING_EFFECT_WORDS
#undef SCR_DRIVING_EFFECT_BYTES
} // namespace scr::test::effects
