/*
 * ARM11 asset resolver.
 *
 * The game keeps the INCBIN stub addresses the linker produced, and every
 * access is remapped to an external payload, supporting exact stubs,
 * `base + offset` members of a concatenated group and interior pointers of a
 * containing asset.
 *
 * The indices are about 0.5 MiB, so they stay resident and every lookup is a
 * binary search in RAM with no I/O at all. Payloads keep a bounded,
 * reclaimable cache.
 */

/* libc first: global.h defines min/max/abs as macros and would break the
 * newlib declarations if it were included before them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_platform.h"
#include "port_log.h"
#include "3ds_assets.h"
#include "3ds_data.h"
#include "3ds_platform.h"

u32 GetDecompressedDataSize(const u32 *ptr);

#define ASSET_INDEX_PATH "romfs:/assets/asset_index.bin"
#define ASSET_PTR_INDEX_PATH "romfs:/assets/asset_ptr_index.bin"
#define ASSET_MAP_PATH "romfs:/assets/asset_map.txt"

struct AssetMapEntry
{
    u32 addr;
    u32 size;
    u32 pathOffset;
};

struct AssetPtrMapEntry
{
    u32 ptr;
    u32 base;
    u32 offset;
};

/* One payload per asset index; `stamp` is a frame number, never a byte count. */
struct AssetPayload
{
    u8 *data;
    u32 size;
    u32 stamp;
};

static struct AssetMapEntry *sEntries;
static struct AssetPtrMapEntry *sPtrEntries;
static struct AssetPayload *sPayloads;
static char *sPathBlob;
static u32 sPathBlobSize;
static u32 sEntryCount;
static u32 sPtrEntryCount;
static bool sLoaded;
static bool sTried;

static struct CtrAssetStats sStats;
static u32 sBudget = CTR_ASSET_CACHE_BUDGET;
static u32 sStamp;

static void *ReadWholeFile(const char *path, u32 *outSize)
{
    FILE *file = fopen(path, "rb");
    long size;
    void *buffer;

    if (file == NULL)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0
     || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return NULL;
    }
    buffer = malloc((size_t)size + 1);
    if (buffer == NULL)
    {
        fclose(file);
        return NULL;
    }
    if (fread(buffer, 1, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        free(buffer);
        return NULL;
    }
    fclose(file);
    ((u8 *)buffer)[size] = 0;
    *outSize = (u32)size;
    return buffer;
}

static bool LoadAssetMap(void)
{
    u32 indexBytes = 0, ptrBytes = 0;
    void *index;
    void *ptrIndex;
    u32 i;

    if (sTried)
        return sLoaded;
    sTried = true;

    index = ReadWholeFile(ASSET_INDEX_PATH, &indexBytes);
    if (index == NULL || indexBytes % sizeof(struct AssetMapEntry) != 0 || indexBytes == 0)
    {
        free(index);
        PORT_LOG("[FS] asset index missing or malformed (%lu bytes)\n", (unsigned long)indexBytes);
        sTried = false;
        return false;
    }
    sPathBlob = ReadWholeFile(ASSET_MAP_PATH, &sPathBlobSize);
    if (sPathBlob == NULL)
    {
        free(index);
        PORT_LOG("[FS] asset path table missing\n");
        sTried = false;
        return false;
    }
    /* asset_map.txt is a line-oriented table; each path has to become its own
     * C string or a lookup returns that path plus the rest of the file. */
    for (i = 0; i < sPathBlobSize; i++)
    {
        if (sPathBlob[i] == '\n' || sPathBlob[i] == '\r')
            sPathBlob[i] = '\0';
    }

    sEntries = index;
    sEntryCount = indexBytes / sizeof(struct AssetMapEntry);

    /* The pointer map is optional: a build with no grouped INCBIN has none. */
    ptrIndex = ReadWholeFile(ASSET_PTR_INDEX_PATH, &ptrBytes);
    if (ptrIndex != NULL && ptrBytes % sizeof(struct AssetPtrMapEntry) == 0)
    {
        sPtrEntries = ptrIndex;
        sPtrEntryCount = ptrBytes / sizeof(struct AssetPtrMapEntry);
    }
    else
    {
        free(ptrIndex);
    }

    sPayloads = calloc(sEntryCount, sizeof(*sPayloads));
    if (sPayloads == NULL)
    {
        PORT_LOG("[FS] asset payload table allocation failed\n");
        sTried = false;
        return false;
    }

    /* A malformed path offset would otherwise be read as arbitrary memory. */
    for (i = 0; i < sEntryCount; i++)
    {
        if (sEntries[i].pathOffset >= sPathBlobSize)
        {
            PORT_LOG("[FS] asset %lu has out-of-range path offset\n", (unsigned long)i);
            sTried = false;
            return false;
        }
        if (i != 0 && sEntries[i].addr < sEntries[i - 1].addr)
        {
            PORT_LOG("[FS] asset index is not sorted at row %lu\n", (unsigned long)i);
            sTried = false;
            return false;
        }
    }

    sStats.entries = sEntryCount;
    sStats.pointerEntries = sPtrEntryCount;
    sStats.indexBytes = indexBytes + ptrBytes + sPathBlobSize
                      + sEntryCount * (u32)sizeof(*sPayloads);
    sLoaded = true;
    PORT_LOG("[FS] assets: %lu entries, %lu ptr rows, %lu KiB resident index\n",
            (unsigned long)sEntryCount, (unsigned long)sPtrEntryCount,
            (unsigned long)(sStats.indexBytes >> 10));
    return true;
}

static const struct AssetMapEntry *GetAssetEntry(u32 idx)
{
    return &sEntries[idx];
}

static const char *GetAssetPathByIndex(u32 idx)
{
    return sPathBlob + sEntries[idx].pathOffset;
}

static s32 FindAssetIndexByAddr(u32 addr)
{
    s32 lo = 0;
    s32 hi = (s32)sEntryCount - 1;

    while (lo <= hi)
    {
        s32 mid = lo + ((hi - lo) >> 1);
        u32 midAddr = sEntries[mid].addr;

        if (midAddr == addr)
            return mid;
        if (midAddr < addr)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

static bool RemapPointerViaPtrMap(u32 ptr, u32 *outBase, u32 *outOffset)
{
    s32 lo = 0;
    s32 hi = (s32)sPtrEntryCount - 1;
    s32 first = -1;
    bool found = false;
    u32 bestOffset = 0xFFFFFFFF;

    if (sPtrEntryCount == 0)
        return false;

    while (lo <= hi)
    {
        s32 mid = lo + ((hi - lo) >> 1);

        if (sPtrEntries[mid].ptr >= ptr)
        {
            if (sPtrEntries[mid].ptr == ptr)
                first = mid;
            hi = mid - 1;
        }
        else
        {
            lo = mid + 1;
        }
    }
    if (first < 0)
        return false;

    /* Several groups can start at the same address; keep the tightest match. */
    for (; first < (s32)sPtrEntryCount && sPtrEntries[first].ptr == ptr; first++)
    {
        u32 base = sPtrEntries[first].base;
        u32 offset = sPtrEntries[first].offset;
        s32 assetIdx = FindAssetIndexByAddr(base);

        if (assetIdx < 0 || offset >= GetAssetEntry(assetIdx)->size)
            continue;
        if (!found || offset < bestOffset)
        {
            found = true;
            bestOffset = offset;
            *outBase = base;
            *outOffset = offset;
        }
    }
    return found;
}

static s32 FindAssetIndexForPointer(u32 ptr, u32 *outOffset)
{
    u32 base, extraOffset;
    s32 idx = FindAssetIndexByAddr(ptr);

    if (idx >= 0)
    {
        *outOffset = 0;
        return idx;
    }
    if (RemapPointerViaPtrMap(ptr, &base, &extraOffset))
    {
        idx = FindAssetIndexByAddr(base);
        if (idx >= 0)
        {
            *outOffset = extraOffset;
            return idx;
        }
    }
    return -1;
}

static s32 FindInteriorAssetIndexForSizedPointer(u32 ptr, u32 size, u32 *outOffset)
{
    s32 lo = 0;
    s32 hi = (s32)sEntryCount - 1;
    s32 candidate;

    if (sEntryCount == 0)
        return -1;

    while (lo <= hi)
    {
        s32 mid = lo + ((hi - lo) >> 1);

        if (sEntries[mid].addr <= ptr)
            lo = mid + 1;
        else
            hi = mid - 1;
    }

    for (candidate = hi; candidate >= 0; candidate--)
    {
        u32 base = sEntries[candidate].addr;
        u32 assetSize = sEntries[candidate].size;
        u32 offset;

        if (base > ptr)
            continue;
        if (assetSize == 0 || ptr >= base + assetSize)
            continue;
        offset = ptr - base;
        if (offset == 0)
            continue;
        if (size != 0 && offset + size > assetSize)
            continue;
        *outOffset = offset;
        return candidate;
    }
    return -1;
}

static s32 FindAssetIndexForSizedPointer(u32 ptr, u32 size, u32 *outOffset)
{
    s32 idx = FindAssetIndexByAddr(ptr);

    if (idx >= 0)
    {
        if (size != 0 && size > GetAssetEntry(idx)->size)
            return -1;
        *outOffset = 0;
        return idx;
    }
    idx = FindAssetIndexForPointer(ptr, outOffset);
    if (idx < 0)
        return -1;
    if (size != 0 && *outOffset + size > GetAssetEntry(idx)->size)
        return -1;
    return idx;
}

/* ── Payload cache ──────────────────────────────────────────────────────── */

static u8 *LoadPayload(u32 idx)
{
    const char *path = GetAssetPathByIndex(idx);
    u32 expected = sEntries[idx].size;
    FILE *file;
    u8 *buffer;

    file = CtrData_Open(path);
    if (file == NULL)
    {
        ++sStats.errors;
        PORT_LOG("[ERROR] asset open failed: %s\n", path);
        return NULL;
    }
    buffer = malloc(expected != 0 ? expected : 1);
    if (buffer == NULL)
    {
        fclose(file);
        ++sStats.errors;
        PORT_LOG("[ERROR] asset alloc failed: %s (%lu bytes)\n", path, (unsigned long)expected);
        return NULL;
    }
    if (fread(buffer, 1, expected, file) != expected)
    {
        fclose(file);
        free(buffer);
        ++sStats.errors;
        PORT_LOG("[ERROR] asset short read: %s\n", path);
        return NULL;
    }
    fclose(file);

    sPayloads[idx].data = buffer;
    sPayloads[idx].size = expected;
    sStats.bytes += expected;
    ++sStats.loads;
    if (sStats.bytes > sStats.peakBytes)
        sStats.peakBytes = sStats.bytes;
    return buffer;
}

static u8 *GetPayload(u32 idx)
{
    u8 *data = sPayloads[idx].data;

    if (data == NULL)
    {
        /* A read off the card where the game or the renderer needs it, in
         * the middle of a frame: on an Old 3DS the one kind of CPU spike a
         * frame-time log cannot tell apart, so the slow ones are named. */
        static unsigned sSlowLogged;
        uint64_t start = CtrPlatform_Ticks();
        float ms;

        ++sStats.misses;
        data = LoadPayload(idx);
        ms = CtrPlatform_TickMs(CtrPlatform_Ticks() - start);
        if (ms >= 1.0f && sSlowLogged < 96)
        {
            ++sSlowLogged;
            CtrLog_Write(CTR_LOG_FS, "asset read in frame: %s (%lu bytes) %.1f ms",
                         GetAssetPathByIndex(idx), (unsigned long)sEntries[idx].size, ms);
        }
    }
    else
    {
        ++sStats.hits;
    }
    if (data != NULL)
        sPayloads[idx].stamp = sStamp;
    return data;
}

void CtrAssets_SetBudget(u32 bytes)
{
    sBudget = bytes;
}

const struct CtrAssetStats *CtrAssets_GetStats(void)
{
    sStats.budget = sBudget;
    return &sStats;
}

/*
 * Safe point only. A payload published to the game this frame must never be
 * freed underneath it, so the collector skips anything touched recently and
 * only runs between main-loop callbacks.
 */
void CtrAssets_Collect(void)
{
    u32 i;

    ++sStamp;
    if (!sLoaded || sStats.bytes <= sBudget)
        return;

    for (i = 0; i < sEntryCount && sStats.bytes > sBudget; i++)
    {
        u32 age = sStamp - sPayloads[i].stamp;

        if (sPayloads[i].data == NULL || age < CTR_ASSET_CACHE_GRACE_FRAMES)
            continue;
        sStats.bytes -= sPayloads[i].size;
        free(sPayloads[i].data);
        sPayloads[i].data = NULL;
        sPayloads[i].size = 0;
        ++sStats.evictions;
    }
}

/* ── Public bridge API ──────────────────────────────────────────────────── */

bool Port_IsAssetStub(const void *ptr)
{
    u32 offset;

    return LoadAssetMap() && FindAssetIndexForPointer((u32)ptr, &offset) >= 0;
}

const char *Port_GetAssetPath(const void *ptr)
{
    u32 offset = 0;
    s32 idx;

    if (!LoadAssetMap())
        return NULL;
    idx = FindAssetIndexForPointer((u32)ptr, &offset);
    return idx >= 0 ? GetAssetPathByIndex(idx) : NULL;
}

u32 Port_GetAssetSize(const void *ptr)
{
    u32 offset;
    s32 idx;

    if (!LoadAssetMap())
        return 0;
    idx = FindAssetIndexForPointer((u32)ptr, &offset);
    if (idx < 0 || offset >= GetAssetEntry(idx)->size)
        return 0;
    return GetAssetEntry(idx)->size - offset;
}

u32 Port_GetAssetSizeExact(const void *base)
{
    s32 idx;

    if (base == NULL || !LoadAssetMap())
        return 0;
    idx = FindAssetIndexByAddr((u32)base);
    return idx >= 0 ? GetAssetEntry(idx)->size : 0;
}

u32 Port_GetAssetSizeSized(const void *ptr, u32 size)
{
    u32 offset;
    s32 idx;

    if (!LoadAssetMap())
        return 0;
    idx = FindAssetIndexForSizedPointer((u32)ptr, size, &offset);
    if (idx < 0 || offset >= GetAssetEntry(idx)->size)
        return 0;
    return GetAssetEntry(idx)->size - offset;
}

u32 Port_GetDecompressedAssetSize(const void *ptr)
{
    u32 offset;
    s32 idx;
    const u8 *data;

    if (!LoadAssetMap())
        return 0;
    idx = FindAssetIndexForPointer((u32)ptr, &offset);
    if (idx < 0)
        return GetDecompressedDataSize(ptr);
    data = GetPayload((u32)idx);
    if (data == NULL || offset + 4 > sPayloads[idx].size)
        return 0;
    data += offset;
    return (data[3] << 16) | (data[2] << 8) | data[1];
}

bool Port_LoadAssetToBuffer(const char *path, void *dest, u32 maxSize)
{
    FILE *file = CtrData_Open(path);
    long size;
    bool ok;

    if (file == NULL)
        return false;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0
     || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return false;
    }
    if (maxSize != 0 && (u32)size > maxSize)
        size = (long)maxSize;
    ok = fread(dest, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    return ok;
}

bool Port_LoadAssetPointerToBuffer(const void *ptr, void *dest, u32 maxSize)
{
    u32 offset, size;
    s32 idx;
    const u8 *data;

    if (!LoadAssetMap())
        return false;
    idx = FindAssetIndexForPointer((u32)ptr, &offset);
    if (idx < 0)
        return false;
    data = GetPayload((u32)idx);
    if (data == NULL || offset >= sPayloads[idx].size)
        return false;
    size = sPayloads[idx].size - offset;
    if (maxSize != 0 && size > maxSize)
        size = maxSize;
    memcpy(dest, data + offset, size);
    return true;
}

bool Port_LoadAssetPointerToBufferSized(const void *ptr, void *dest, u32 size)
{
    u32 offset;
    s32 idx;
    const u8 *data;

    if (size == 0 || !LoadAssetMap())
        return false;
    idx = FindAssetIndexForSizedPointer((u32)ptr, size, &offset);
    if (idx < 0)
        return false;
    data = GetPayload((u32)idx);
    if (data == NULL || offset + size > sPayloads[idx].size)
        return false;
    memcpy(dest, data + offset, size);
    return true;
}

bool Port_LoadAssetRangeToBuffer(const void *base, u32 offset, void *dest, u32 size)
{
    s32 idx;
    const u8 *data;

    if (!LoadAssetMap())
        return false;
    idx = FindAssetIndexByAddr((u32)base);
    if (idx < 0 || offset > GetAssetEntry(idx)->size
     || size > GetAssetEntry(idx)->size - offset)
        return false;
    data = GetPayload((u32)idx);
    if (data == NULL || offset + size > sPayloads[idx].size)
        return false;
    memcpy(dest, data + offset, size);
    return true;
}

/* Caller owns the returned block; it is not part of the reclaimable cache. */
bool Port_LoadAssetPointerToHeap(const void *ptr, u8 **outBuf, u32 *outSize)
{
    u32 offset, size;
    s32 idx;
    const u8 *data;
    u8 *copy;

    if (outBuf == NULL || outSize == NULL)
        return false;
    *outBuf = NULL;
    *outSize = 0;
    if (!LoadAssetMap())
        return false;
    idx = FindAssetIndexForPointer((u32)ptr, &offset);
    if (idx < 0)
        return false;
    data = GetPayload((u32)idx);
    if (data == NULL || offset >= sPayloads[idx].size)
        return false;
    size = sPayloads[idx].size - offset;
    copy = malloc(size);
    if (copy == NULL)
        return false;
    memcpy(copy, data + offset, size);
    *outBuf = copy;
    *outSize = size;
    return true;
}

static const void *ResolveLoaded(s32 idx, u32 offset, const void *fallback)
{
    const u8 *data = GetPayload((u32)idx);

    if (data == NULL)
    {
        /*
         * A recognised stub is not graphic data. Handing it back would let the
         * caller read adjacent constants as tiles and hide the I/O failure, so
         * this aborts instead.
         */
        PORT_LOG("[ERROR] unresolved asset %ld (%s)\n", (long)idx, GetAssetPathByIndex((u32)idx));
        CtrAssets_Fatal(GetAssetPathByIndex((u32)idx));
        return fallback;
    }
    return data + offset;
}

const void *Port_ResolveAssetPointer(const void *ptr)
{
    u32 offset;
    s32 idx;
    const void *map = Port_ResolveMapAssetPointer(ptr);

    if (map != ptr)
        return map;
    if (!LoadAssetMap())
        return ptr;
    idx = FindAssetIndexForPointer((u32)ptr, &offset);
    if (idx < 0)
        return ptr;
    return ResolveLoaded(idx, offset, ptr);
}

const void *Port_ResolveAssetPointerSized(const void *ptr, u32 size)
{
    u32 offset;
    s32 idx;

    if (!LoadAssetMap())
        return ptr;
    idx = FindAssetIndexForSizedPointer((u32)ptr, size, &offset);
    if (idx < 0)
        return ptr;
    return ResolveLoaded(idx, offset, ptr);
}

const void *Port_ResolveAssetPointerInContainingAsset(const void *ptr, u32 size)
{
    u32 offset;
    s32 idx;

    if (!LoadAssetMap())
        return ptr;
    idx = FindInteriorAssetIndexForSizedPointer((u32)ptr, size, &offset);
    if (idx < 0)
        return Port_ResolveAssetPointerSized(ptr, size);
    return ResolveLoaded(idx, offset, ptr);
}

u32 Port_GetSpriteFrameSize(const void *base, u32 declaredSize)
{
    s32 idx;

    /* sizeof(INCBIN) describes the stub, not the image, in an external build. */
    if (declaredSize >= 32 || base == NULL || !LoadAssetMap())
        return declaredSize;
    idx = FindAssetIndexByAddr((u32)base);
    return idx >= 0 ? GetAssetEntry(idx)->size : declaredSize;
}

const void *Port_ResolveSpriteFramePointer(const void *base, u32 size, u32 offset)
{
    s32 idx;
    u32 assetSize;

    if (base == NULL || size == 0 || !LoadAssetMap())
        return NULL;
    idx = FindAssetIndexByAddr((u32)base);
    if (idx < 0)
        return (const u8 *)base + offset;
    assetSize = GetAssetEntry(idx)->size;
    if (offset >= assetSize || size > assetSize - offset)
        return NULL;
    return ResolveLoaded(idx, offset, NULL);
}

const void *Port_PeekSpriteFramePointer(const void *base, u32 size, u32 offset)
{
    s32 idx;
    u32 assetSize;

    if (base == NULL || size == 0 || !LoadAssetMap())
        return NULL;
    idx = FindAssetIndexByAddr((u32)base);
    if (idx < 0)
        return (const u8 *)base + offset;
    assetSize = GetAssetEntry(idx)->size;
    if (offset >= assetSize || size > assetSize - offset || sPayloads[idx].data == NULL)
        return NULL;
    return GetPayload((u32)idx) + offset;
}

bool CtrAssets_PrefetchFind(const void *ptr, CtrAssetPrefetch *out)
{
    u32 offset;
    s32 idx;

    if (ptr == NULL || Port_ResolveMapAssetPointer(ptr) != ptr || !LoadAssetMap())
        return false;
    idx = FindAssetIndexForPointer((u32)ptr, &offset);
    if (idx < 0 || sPayloads[idx].data != NULL)
        return false;
    out->index = idx;
    out->size = sEntries[idx].size;
    out->path = GetAssetPathByIndex((u32)idx);
    return true;
}

void *CtrAssets_PrefetchRead(const CtrAssetPrefetch *request)
{
    FILE *file = CtrData_Open(request->path);
    u8 *buffer;

    if (file == NULL)
        return NULL;
    buffer = malloc(request->size != 0 ? request->size : 1);
    if (buffer != NULL && fread(buffer, 1, request->size, file) != request->size)
    {
        free(buffer);
        buffer = NULL;
    }
    fclose(file);
    return buffer;
}

void CtrAssets_PrefetchAdopt(const CtrAssetPrefetch *request, void *payload)
{
    s32 idx = request->index;

    if (payload == NULL)
        return;
    if (idx < 0 || (u32)idx >= sEntryCount || sPayloads[idx].data != NULL)
    {
        free(payload);
        return;
    }
    sPayloads[idx].data = payload;
    sPayloads[idx].size = request->size;
    sPayloads[idx].stamp = sStamp;
    sStats.bytes += request->size;
    ++sStats.loads;
    if (sStats.bytes > sStats.peakBytes)
        sStats.peakBytes = sStats.bytes;
}

void Port_AssetPreload(void)
{
    if (!LoadAssetMap())
        CtrAssets_Fatal("asset index could not be loaded from RomFS");
}
