/*
 * The field map's changes, announced (include/3ds_map_events.h). Every call
 * the game makes from another file to the map grid's setters, and to what
 * loads a layout whole, comes here first through the linker (--wrap, full.mk),
 * so src/ stays as it is. fieldmap.c never calls its own setters; what it
 * writes itself happens inside the loads wrapped here. A write that comes
 * by none of these ways is still caught: the voxel world hashes the map once
 * a second and logs it (CtrVoxel_Update, "CONTRACT").
 */
#include "global.h"
#include "fieldmap.h"
#include "overworld.h"
#include "battle_pyramid.h"
#include "trainer_hill.h"
#include "3ds_map_events.h"

static uint32_t sVersion, sCells, sCollision, sReloads;

uint32_t CtrMap_Version(void)
{
    return sVersion;
}

void CtrMap_Counts(uint32_t *cells, uint32_t *collision, uint32_t *reloads)
{
    *cells = sCells;
    *collision = sCollision;
    *reloads = sReloads;
}

static u16 *Entry(int x, int y)
{
    if (gBackupMapLayout.map == NULL || x < 0 || y < 0 || x >= gBackupMapLayout.width
        || y >= gBackupMapLayout.height)
        return NULL;
    return &gBackupMapLayout.map[x + gBackupMapLayout.width * y];
}

/* What a setter did to one cell: the drawing's version moves only when more
 * than the collision bits changed. */
static void Changed(u16 before, const u16 *entry)
{
    u16 diff = before ^ *entry;

    if (diff & ~MAPGRID_COLLISION_MASK)
    {
        ++sVersion;
        ++sCells;
    }
    else if (diff != 0)
        ++sCollision;
}

static void Reloaded(void)
{
    ++sVersion;
    ++sReloads;
}

#define CELL_SETTER(name, type)                                \
    void __real_##name(int x, int y, type value);              \
    void __wrap_##name(int x, int y, type value)               \
    {                                                          \
        u16 *entry = Entry(x, y);                              \
        u16 before = entry != NULL ? *entry : 0;               \
                                                               \
        __real_##name(x, y, value);                            \
        if (entry != NULL)                                     \
            Changed(before, entry);                            \
    }

CELL_SETTER(MapGridSetMetatileIdAt, u16)
CELL_SETTER(MapGridSetMetatileEntryAt, u16)
CELL_SETTER(MapGridSetMetatileImpassabilityAt, bool32)

#define RELOAD(name, params, args)                             \
    void __real_##name params;                                 \
    void __wrap_##name params                                  \
    {                                                          \
        __real_##name args;                                    \
        Reloaded();                                            \
    }

RELOAD(InitMap, (void), ())
RELOAD(InitMapFromSavedGame, (void), ())
RELOAD(SetCurrentMapLayout, (u16 mapLayoutId), (mapLayoutId))
RELOAD(GenerateBattlePyramidFloorLayout, (u16 *backupMapData, bool8 setPlayerPosition),
       (backupMapData, setPlayerPosition))
RELOAD(GenerateTrainerHillFloorLayout, (u16 *mapArg), (mapArg))

/* Into a connected map: the layout is the next map's, the saved view of the
 * last one pasted back over it. */
bool8 __real_CameraMove(int x, int y);
bool8 __wrap_CameraMove(int x, int y)
{
    bool8 crossed = __real_CameraMove(x, y);

    if (crossed)
        Reloaded();
    return crossed;
}
