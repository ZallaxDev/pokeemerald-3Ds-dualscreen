#ifndef CTR_ENCOUNTERS_H
#define CTR_ENCOUNTERS_H

#include <stddef.h>
#include <stdint.h>

enum { CTR_TIME_MORNING, CTR_TIME_DAY, CTR_TIME_NIGHT, CTR_TIME_COUNT };

typedef struct
{
    uint8_t mapGroup, mapNum, slot;
    uint16_t expectedSpecies, species[CTR_TIME_COUNT];
} CtrTimedEncounterRule;

unsigned CtrEncounters_Phase(unsigned seconds);
uint16_t CtrEncounters_Resolve(const CtrTimedEncounterRule *rules, size_t count,
                             unsigned mapGroup, unsigned mapNum, unsigned slot,
                             uint16_t original, unsigned phase);
struct WildPokemonInfo;
const struct WildPokemonInfo *CtrEncounters_Land(const struct WildPokemonInfo *original,
                                                unsigned mapGroup, unsigned mapNum);

/* Species shown by encounter previews; overridden by optional randomizers. */
uint16_t CtrEncounters_DisplaySpecies(uint16_t species);

#endif
