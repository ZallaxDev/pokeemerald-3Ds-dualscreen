/*
 * Game data backends: RomFS, loose files on the SD card, or one data pack.
 * See include/3ds_data.h for the contract and docs/ASSET_PIPELINE.md for the
 * pack format, which builder/emerald3ds_builder/pak.py writes.
 */

#define _GNU_SOURCE
#include <3ds.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "3ds_data.h"
#include "3ds_pak.h"
#include "3ds_platform.h"
#include "../../include/constants/global.h"

/* 0 = choose at runtime; 1 = RomFS, 2 = loose, 3 = pack. */
#ifndef CTR_DATA_BACKEND
#define CTR_DATA_BACKEND 0
#endif

#define ENGINE_ABI_PATH "engine/abi.bin"

/* The ROM the data pack must come from: the one of the game's compiled
 * language, never another locale's data. */
#if GAME_LANGUAGE == LANGUAGE_SPANISH
static const uint8_t sSupportedRomSha1[20] = {
    0xfe, 0x15, 0x58, 0xa3, 0xdc, 0xb0, 0x36, 0x0a, 0xb5, 0x58,
    0x96, 0x9e, 0x09, 0xb6, 0x90, 0x88, 0x8b, 0x84, 0x6d, 0xd9,
};
#define SUPPORTED_ROM_DETAIL "Usa la ROM de Pokemon Esmeralda\n" \
                             "(Espana, BPES) con este cliente."
#elif GAME_LANGUAGE == LANGUAGE_FRENCH
static const uint8_t sSupportedRomSha1[20] = {
    0xca, 0x66, 0x66, 0x51, 0x37, 0x4d, 0x89, 0xca, 0x43, 0x90,
    0x07, 0xbe, 0xd5, 0x4d, 0x83, 0x9e, 0xb7, 0xbd, 0x14, 0xd0,
};
#define SUPPORTED_ROM_DETAIL "Utilise la ROM de Pokemon Emeraude\n" \
                             "(France, BPEF) avec ce client."
#else
static const uint8_t sSupportedRomSha1[20] = {
    0xf3, 0xae, 0x08, 0x81, 0x81, 0xbf, 0x58, 0x3e, 0x55, 0xda,
    0xf9, 0x62, 0xa9, 0x2b, 0xb4, 0x6f, 0x4f, 0x1d, 0x07, 0xb7,
};
#define SUPPORTED_ROM_DETAIL "Only Pokemon Emerald (USA, Europe) is\n" \
                             "supported. Run the builder with that ROM."
#endif

typedef struct
{
    uint64_t base;
    uint32_t size;
    uint32_t pos;
} PakStream;

static CtrDataBackend sBackend = CTR_DATA_NONE;
static uint32_t sEngineAbi;
static FILE *sPak;
static CtrPakEntry *sEntries;
static uint32_t sEntryCount;
static LightLock sPakLock;
static char sErrorTitle[96];
static char sErrorDetail[512];

static bool FileExists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void SetError(const char *title, const char *detail)
{
    snprintf(sErrorTitle, sizeof(sErrorTitle), "%s", title);
    snprintf(sErrorDetail, sizeof(sErrorDetail), "%s", detail);
    CtrLog_Write(CTR_LOG_ERROR, "data: %s", title);
}

static void ReadEngineAbi(void)
{
    FILE *file = CtrFs_OpenAsset(ENGINE_ABI_PATH);
    uint8_t raw[4] = {0};

    sEngineAbi = 0;
    if (file != NULL)
    {
        if (fread(raw, 1, sizeof(raw), file) == sizeof(raw))
            sEngineAbi = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16)
                       | ((uint32_t)raw[3] << 24);
        fclose(file);
    }
}

static const CtrPakEntry *FindEntry(const char *path)
{
    return CtrPak_Find(sEntries, sEntryCount, path);
}

static bool OpenPak(void)
{
    uint8_t header[CTR_PAK_HEADER_BYTES];
    uint8_t *index = NULL;
    CtrPakHeader info;
    CtrPakStatus status;
    char detail[512];

    sPak = fopen(CTR_DATA_PAK_PATH, "rb");
    if (sPak == NULL)
    {
        SetError("The game data pack was not found.",
                 "Run the builder that came with this\n"
                 "release with your own Pokemon Emerald\n"
                 "ROM and install the data pack to:\n\n"
                 "  /3ds/emerald3ds/emerald3ds.pak");
        return false;
    }
    setvbuf(sPak, NULL, _IOFBF, 64 * 1024);
    status = fread(header, 1, sizeof(header), sPak) == sizeof(header)
           ? CtrPak_ParseHeader(header, sEngineAbi, sSupportedRomSha1, &info) : CTR_PAK_BAD_MAGIC;
    switch (status)
    {
    case CTR_PAK_OK:
        break;
    case CTR_PAK_BAD_MAGIC:
        SetError("This is not a game data pack.",
                 "/3ds/emerald3ds/emerald3ds.pak is not a\n"
                 "data pack. Run the builder again.");
        goto fail;
    case CTR_PAK_BAD_SCHEMA:
    case CTR_PAK_BAD_ABI:
        snprintf(detail, sizeof(detail),
                 "It was generated for a different\n"
                 "release. Run the builder that came\n"
                 "with this release again.\n\n"
                 "Engine ABI: %08lX (schema %u)\n"
                 "Pack ABI:   %08lX (schema %lu)",
                 (unsigned long)sEngineAbi, CTR_PAK_SCHEMA_VERSION,
                 (unsigned long)info.engineAbi, (unsigned long)info.schema);
        SetError("The data pack does not match this release.", detail);
        goto fail;
    case CTR_PAK_BAD_ROM:
        SetError("The data pack comes from another ROM.", SUPPORTED_ROM_DETAIL);
        goto fail;
    default:
        SetError("The data pack is damaged.",
                 "It does not pass the integrity check.\n"
                 "Run the builder again.");
        goto fail;
    }
    index = malloc((size_t)info.entryCount * CTR_PAK_ENTRY_BYTES);
    sEntries = malloc((size_t)info.entryCount * sizeof(*sEntries));
    if (index == NULL || sEntries == NULL
     || fseek(sPak, (long)info.indexOffset, SEEK_SET) != 0
     || fread(index, CTR_PAK_ENTRY_BYTES, info.entryCount, sPak) != info.entryCount)
    {
        SetError("The data pack could not be read.", "The SD card could not deliver its index.");
        goto fail;
    }
    if (CtrPak_ParseIndex(index, &info, sEntries) != CTR_PAK_OK)
    {
        SetError("The data pack is damaged.",
                 "Its index does not pass the integrity\n"
                 "check. Run the builder again.");
        goto fail;
    }
    free(index);
    sEntryCount = info.entryCount;
    LightLock_Init(&sPakLock);
    CtrLog_Write(CTR_LOG_FS, "data: pack %lu entries, ABI %08lx",
                 (unsigned long)sEntryCount, (unsigned long)info.engineAbi);
    return true;

fail:
    free(index);
    free(sEntries);
    sEntries = NULL;
    fclose(sPak);
    sPak = NULL;
    return false;
}

bool CtrData_Init(void)
{
    int choice = CTR_DATA_BACKEND;

    ReadEngineAbi();
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/emerald3ds", 0777);
    if (choice == 0)
    {
        if (FileExists(CTR_DATA_LOOSE_MARKER))
            choice = 2;
        else if (FileExists(CTR_DATA_EMBEDDED_MARKER))
            choice = 1;
        else
            choice = 3;
    }
    switch (choice)
    {
    case 1:
        if (!FileExists(CTR_DATA_EMBEDDED_MARKER))
        {
            SetError("This build has no embedded game data.", "Install a data pack or loose data files.");
            return false;
        }
        sBackend = CTR_DATA_ROMFS;
        break;
    case 2:
        sBackend = CTR_DATA_LOOSE;
        break;
    default:
        if (!OpenPak())
            return false;
        sBackend = CTR_DATA_PAK;
        break;
    }
    CtrLog_Write(CTR_LOG_FS, "data: %s backend, engine ABI %08lx",
                 CtrData_BackendName(), (unsigned long)sEngineAbi);
    return true;
}

void CtrData_Shutdown(void)
{
    if (sPak != NULL)
        fclose(sPak);
    sPak = NULL;
    free(sEntries);
    sEntries = NULL;
    sEntryCount = 0;
    sBackend = CTR_DATA_NONE;
}

CtrDataBackend CtrData_GetBackend(void) { return sBackend; }
const char *CtrData_ErrorTitle(void) { return sErrorTitle; }
const char *CtrData_ErrorDetail(void) { return sErrorDetail; }
uint32_t CtrData_EngineAbi(void) { return sEngineAbi; }

const char *CtrData_BackendName(void)
{
    switch (sBackend)
    {
    case CTR_DATA_ROMFS: return "romfs";
    case CTR_DATA_LOOSE: return "loose";
    case CTR_DATA_PAK: return "pak";
    default: return "none";
    }
}

static ssize_t PakRead(void *cookie, char *buffer, size_t size)
{
    PakStream *stream = cookie;
    size_t left = stream->size - stream->pos;
    size_t got;

    if (size > left)
        size = left;
    if (size == 0)
        return 0;
    LightLock_Lock(&sPakLock);
    got = fseek(sPak, (long)(stream->base + stream->pos), SEEK_SET) == 0
        ? fread(buffer, 1, size, sPak) : 0;
    LightLock_Unlock(&sPakLock);
    stream->pos += (uint32_t)got;
    return got == 0 && size != 0 ? -1 : (ssize_t)got;
}

static int PakSeek(void *cookie, off_t *offset, int whence)
{
    PakStream *stream = cookie;
    int64_t target;

    switch (whence)
    {
    case SEEK_SET: target = *offset; break;
    case SEEK_CUR: target = (int64_t)stream->pos + *offset; break;
    case SEEK_END: target = (int64_t)stream->size + *offset; break;
    default: errno = EINVAL; return -1;
    }
    if (target < 0 || target > stream->size)
    {
        errno = EINVAL;
        return -1;
    }
    stream->pos = (uint32_t)target;
    *offset = target;
    return 0;
}

static int PakClose(void *cookie)
{
    free(cookie);
    return 0;
}

static bool PathValid(const char *path)
{
    return path != NULL && *path && *path != '/' && strstr(path, "..") == NULL && strchr(path, ':') == NULL;
}

FILE *CtrData_Open(const char *path)
{
    char full[512];

    if (!PathValid(path))
        return NULL;
    switch (sBackend)
    {
    case CTR_DATA_ROMFS:
        snprintf(full, sizeof(full), "romfs:/%s", path);
        return fopen(full, "rb");
    case CTR_DATA_LOOSE:
        snprintf(full, sizeof(full), CTR_DATA_LOOSE_DIR "%s", path);
        return fopen(full, "rb");
    case CTR_DATA_PAK:
    {
        const CtrPakEntry *entry = FindEntry(path);
        PakStream *stream;
        cookie_io_functions_t io = { PakRead, NULL, PakSeek, PakClose };
        FILE *file;

        if (entry == NULL || (stream = malloc(sizeof(*stream))) == NULL)
            return NULL;
        stream->base = entry->offset;
        stream->size = entry->rawSize;
        stream->pos = 0;
        file = fopencookie(stream, "rb", io);
        if (file == NULL)
            free(stream);
        return file;
    }
    default:
        return NULL;
    }
}

bool CtrData_Size(const char *path, uint32_t *outSize)
{
    if (sBackend == CTR_DATA_PAK)
    {
        const CtrPakEntry *entry = PathValid(path) ? FindEntry(path) : NULL;
        if (entry != NULL && outSize != NULL)
            *outSize = entry->rawSize;
        return entry != NULL;
    }
    FILE *file = CtrData_Open(path);
    long size;

    if (file == NULL)
        return false;
    size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    fclose(file);
    if (size < 0)
        return false;
    if (outSize != NULL)
        *outSize = (uint32_t)size;
    return true;
}

bool CtrData_Locate(const char *path, uint64_t *outOffset, uint32_t *outSize)
{
    const CtrPakEntry *entry;

    if (sBackend != CTR_DATA_PAK || !PathValid(path) || (entry = FindEntry(path)) == NULL)
        return false;
    *outOffset = entry->offset;
    *outSize = entry->rawSize;
    return true;
}

bool CtrData_ReadRange(uint64_t offset, void *dest, uint32_t size)
{
    bool ok;

    if (sBackend != CTR_DATA_PAK || sPak == NULL)
        return false;
    LightLock_Lock(&sPakLock);
    ok = fseek(sPak, (long)offset, SEEK_SET) == 0 && fread(dest, 1, size, sPak) == size;
    LightLock_Unlock(&sPakLock);
    return ok;
}

bool CtrData_Exists(const char *path)
{
    return CtrData_Size(path, NULL);
}

void *CtrData_Load(const char *path, uint32_t *outSize)
{
    uint32_t size;
    uint8_t *buffer;
    FILE *file;

    if (!CtrData_Size(path, &size) || (file = CtrData_Open(path)) == NULL)
        return NULL;
    buffer = malloc((size_t)size + 1);
    if (buffer == NULL || fread(buffer, 1, size, file) != size)
    {
        fclose(file);
        free(buffer);
        CtrLog_Write(CTR_LOG_ERROR, "data: short read %s", path);
        return NULL;
    }
    fclose(file);
    buffer[size] = 0;
    if (sBackend == CTR_DATA_PAK)
    {
        const CtrPakEntry *entry = FindEntry(path);
        if (entry != NULL && CtrPak_Crc32(0, buffer, size) != entry->crc32)
        {
            CtrLog_Write(CTR_LOG_ERROR, "data: %s fails its CRC; the pack is damaged", path);
            free(buffer);
            return NULL;
        }
    }
    if (outSize != NULL)
        *outSize = size;
    return buffer;
}
