#ifndef CTR_MAP_EVENTS_H
#define CTR_MAP_EVENTS_H
#include <stdint.h>

/*
 * Changes to the field's map, announced by the game (3ds_map_events.c)
 * instead of found by hashing the whole map every frame: phase F1, part E.
 * The version moves when a metatile's drawing may have changed - its id or
 * its elevation, or the whole layout reloaded; a change of collision alone
 * draws nothing new and leaves it. SDK-free: read by the voxel world.
 */
uint32_t CtrMap_Version(void);
/* Changes since start-up, by kind, for the log. */
void CtrMap_Counts(uint32_t *cells, uint32_t *collision, uint32_t *reloads);

#endif
