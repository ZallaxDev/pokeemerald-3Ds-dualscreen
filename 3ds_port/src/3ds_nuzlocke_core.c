/*
 * The Nuzlocke tracker's state, record and write rules (3ds_nuzlocke_core.h).
 * No SDK, no game header, no I/O.
 */
#include <string.h>
#include "3ds_nuzlocke_core.h"

static const char sMagic[8] = {'E', 'M', '3', 'D', 'N', 'U', 'Z', '2'};
static const uint8_t sGymCaps[8] = {15, 19, 24, 29, 31, 33, 42, 46};
#define CHAMPION_CAP 58u
#define AREAS_AT CTR_NUZ_HEADER_BYTES
#define FALLEN_AT (AREAS_AT + CTR_NUZ_AREA_COUNT)
#define CRC_AT (FALLEN_AT + CTR_NUZ_FALLEN_MAX * 9u)

uint32_t CtrNuz_Crc32(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *bytes = data;

    crc = ~crc;
    while (size-- > 0)
    {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void Put(uint8_t *bytes, uint32_t value, unsigned size)
{
    for (unsigned index = 0; index < size; ++index)
        bytes[index] = (uint8_t)(value >> (index * 8u));
}

static uint32_t Get(const uint8_t *bytes, unsigned size)
{
    uint32_t value = 0;

    for (unsigned index = 0; index < size; ++index)
        value |= (uint32_t)bytes[index] << (index * 8u);
    return value;
}

void CtrNuz_Reset(CtrNuzState *state)
{
    memset(state, 0, sizeof(*state));
}

bool CtrNuz_IsEmpty(const CtrNuzState *state)
{
    if (state->fallenCount != 0)
        return false;
    for (unsigned area = 0; area < CTR_NUZ_AREA_COUNT; ++area)
        if (state->area[area] != CTR_NUZ_UNUSED)
            return false;
    return true;
}

CtrNuzStatus CtrNuz_Area(const CtrNuzState *state, unsigned area)
{
    if (area >= CTR_NUZ_AREA_COUNT || state->area[area] >= CTR_NUZ_STATUS_COUNT)
        return CTR_NUZ_UNUSED;
    return (CtrNuzStatus)state->area[area];
}

bool CtrNuz_SetArea(CtrNuzState *state, unsigned area, CtrNuzStatus status)
{
    if (area >= CTR_NUZ_AREA_COUNT || (unsigned)status >= CTR_NUZ_STATUS_COUNT)
        return false;
    state->area[area] = (uint8_t)status;
    return true;
}

static int FindFallen(const CtrNuzState *state, uint32_t personality, uint32_t otId, bool shedinja)
{
    for (unsigned index = 0; index < state->fallenCount && index < CTR_NUZ_FALLEN_MAX; ++index)
        if (state->fallen[index].personality == personality && state->fallen[index].otId == otId && state->fallen[index].shedinja == shedinja)
            return (int)index;
    return -1;
}

bool CtrNuz_IsFallen(const CtrNuzState *state, uint32_t personality, uint32_t otId, bool shedinja)
{
    return FindFallen(state, personality, otId, shedinja) >= 0;
}

bool CtrNuz_SetFallen(CtrNuzState *state, uint32_t personality, uint32_t otId, bool shedinja, bool fallen)
{
    int index = FindFallen(state, personality, otId, shedinja);

    if (fallen)
    {
        if (index >= 0 || state->fallenCount >= CTR_NUZ_FALLEN_MAX)
            return false;
        state->fallen[state->fallenCount].personality = personality;
        state->fallen[state->fallenCount].otId = otId;
        state->fallen[state->fallenCount].shedinja = shedinja;
        state->fallenCount++;
        return true;
    }
    if (index < 0)
        return false;
    /* Kept packed, the slots past the count zero (the record checks both). */
    memmove(&state->fallen[index], &state->fallen[index + 1],
            (state->fallenCount - (unsigned)index - 1u) * sizeof(state->fallen[0]));
    state->fallenCount--;
    memset(&state->fallen[state->fallenCount], 0, sizeof(state->fallen[0]));
    return true;
}

void CtrNuz_Counts(const CtrNuzState *state, unsigned *caught, unsigned *missed, unsigned *fallen)
{
    unsigned c = 0, m = 0;

    for (unsigned area = 0; area < CTR_NUZ_AREA_COUNT; ++area)
    {
        c += state->area[area] == CTR_NUZ_CAUGHT;
        m += state->area[area] == CTR_NUZ_MISSED;
    }
    *caught = c;
    *missed = m;
    *fallen = state->fallenCount;
}

unsigned CtrNuz_NextCap(unsigned badges, unsigned *leader)
{
    if (badges >= 8)
    {
        *leader = 9;
        return CHAMPION_CAP;
    }
    *leader = badges + 1;
    return sGymCaps[badges];
}

void CtrNuz_Encode(const CtrNuzState *state, uint32_t saveCrc, uint32_t saveSize,
                   uint8_t record[CTR_NUZ_RECORD_BYTES])
{
    memset(record, 0, CTR_NUZ_RECORD_BYTES);
    memcpy(record, sMagic, sizeof(sMagic));
    Put(record + 8, saveCrc, 4);
    Put(record + 12, saveSize, 4);
    Put(record + 16, state->fallenCount, 2);
    memcpy(record + AREAS_AT, state->area, CTR_NUZ_AREA_COUNT);
    for (unsigned index = 0; index < state->fallenCount && index < CTR_NUZ_FALLEN_MAX; ++index)
    {
        Put(record + FALLEN_AT + index * 9u, state->fallen[index].personality, 4);
        Put(record + FALLEN_AT + index * 9u + 4u, state->fallen[index].otId, 4);
        record[FALLEN_AT + index * 9u + 8u] = state->fallen[index].shedinja;
    }
    Put(record + CRC_AT, CtrNuz_Crc32(0, record, CRC_AT), 4);
}

bool CtrNuz_Decode(const uint8_t record[CTR_NUZ_RECORD_BYTES], CtrNuzState *state,
                   uint32_t *saveCrc, uint32_t *saveSize)
{
    return CtrNuz_DecodeSized(record, CTR_NUZ_RECORD_BYTES, state, saveCrc, saveSize);
}

bool CtrNuz_DecodeSized(const uint8_t *record, size_t bytes, CtrNuzState *state,
                        uint32_t *saveCrc, uint32_t *saveSize)
{
    CtrNuzState decoded;
    unsigned count, stride;
    size_t crcAt;

    if (bytes == CTR_NUZ_RECORD_V1_BYTES)
        stride = 8;
    else if (bytes == CTR_NUZ_RECORD_BYTES)
        stride = 9;
    else
        return false;
    crcAt = bytes - 4;
    if (memcmp(record, sMagic, sizeof(sMagic) - 1) != 0
     || record[7] != (stride == 8 ? '1' : '2')
     || Get(record + crcAt, 4) != CtrNuz_Crc32(0, record, crcAt))
        return false;
    for (unsigned index = 18; index < CTR_NUZ_HEADER_BYTES; ++index)
        if (record[index] != 0)
            return false;
    count = Get(record + 16, 2);
    if (count > CTR_NUZ_FALLEN_MAX)
        return false;
    memset(&decoded, 0, sizeof(decoded));
    for (unsigned area = 0; area < CTR_NUZ_AREA_COUNT; ++area)
    {
        if (record[AREAS_AT + area] >= CTR_NUZ_STATUS_COUNT)
            return false;
        decoded.area[area] = record[AREAS_AT + area];
    }
    for (unsigned index = 0; index < CTR_NUZ_FALLEN_MAX; ++index)
    {
        uint32_t personality = Get(record + FALLEN_AT + index * stride, 4);
        uint32_t otId = Get(record + FALLEN_AT + index * stride + 4u, 4);
        uint8_t shedinja = stride == 9 ? record[FALLEN_AT + index * stride + 8u] : 0;

        if (shedinja > 1)
            return false;

        if (index >= count)
        {
            if (personality != 0 || otId != 0 || shedinja != 0)
                return false;
            continue;
        }
        if (CtrNuz_IsFallen(&decoded, personality, otId, shedinja))
            return false;
        decoded.fallen[index].personality = personality;
        decoded.fallen[index].otId = otId;
        decoded.fallen[index].shedinja = shedinja;
        decoded.fallenCount = (uint16_t)(index + 1);
    }
    *state = decoded;
    *saveCrc = Get(record + 8, 4);
    *saveSize = Get(record + 12, 4);
    return true;
}

void CtrNuzSession_NewGame(CtrNuzSession *session)
{
    CtrNuz_Reset(&session->state);
    session->link = CTR_NUZ_LINK_NONE;
    /* The new game's first save replaces the old game's record. */
    session->owned = true;
    session->dirty = false;
    session->writeFailed = false;
}

void CtrNuzSession_Loaded(CtrNuzSession *session, CtrNuzLink link)
{
    if (link != CTR_NUZ_LINK_MATCHED)
        CtrNuz_Reset(&session->state);
    session->link = link;
    session->owned = link == CTR_NUZ_LINK_MATCHED;
    session->dirty = false;
    session->writeFailed = false;
}

void CtrNuzSession_Edited(CtrNuzSession *session)
{
    session->dirty = true;
}

bool CtrNuzSession_ShouldWrite(const CtrNuzSession *session)
{
    return session->owned || session->dirty || !CtrNuz_IsEmpty(&session->state);
}

void CtrNuzSession_Written(CtrNuzSession *session, bool ok)
{
    if (!ok)
    {
        session->writeFailed = true;
        return;
    }
    session->link = CTR_NUZ_LINK_MATCHED;
    session->owned = true;
    session->dirty = false;
    session->writeFailed = false;
}

CtrNuzLabel CtrNuzSession_Label(const CtrNuzSession *session)
{
    if (session->writeFailed)
        return CTR_NUZ_LABEL_WRITE_FAILED;
    if (session->dirty)
        return CTR_NUZ_LABEL_UNSAVED;
    switch (session->link)
    {
    case CTR_NUZ_LINK_MATCHED:
        return CTR_NUZ_LABEL_THIS_SAVE;
    case CTR_NUZ_LINK_OTHER:
        return CTR_NUZ_LABEL_OTHER_SAVE;
    case CTR_NUZ_LINK_DAMAGED:
        return CTR_NUZ_LABEL_DAMAGED;
    case CTR_NUZ_LINK_NONE:
    default:
        return CTR_NUZ_LABEL_NEW;
    }
}
