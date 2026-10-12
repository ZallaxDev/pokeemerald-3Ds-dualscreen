/*
 * The engine's own work, timed from outside it: the linker routes every call
 * from another translation unit through these (--wrap, see full.mk), so the
 * game's sources stay as they are. Calls inside the defining unit are not
 * seen, which only matters for decompress.c's own helpers.
 *
 * The copies also say where they wrote (CtrVideo_NotifyWrite): every macro
 * copy and fill of the game ends in one of these BIOS stubs, so this is how
 * the compositor learns which VRAM changed without comparing it all
 * (phase F1, part B).
 */
#include "global.h"
#include "port_prof.h"
#include "port_platform.h"
#include "3ds_video.h"

#define PROF_WRAP_VOID(name, section, params, args)   \
    void __real_##name params;                        \
    void __wrap_##name params                         \
    {                                                 \
        PORT_PROF_BEGIN(call);                        \
        __real_##name args;                           \
        PORT_PROF_END(call, section);                 \
    }

PROF_WRAP_VOID(RunTasks, PORT_PROF_TASKS, (void), ())
PROF_WRAP_VOID(AnimateSprites, PORT_PROF_SPRITES, (void), ())
PROF_WRAP_VOID(BuildOamBuffer, PORT_PROF_SPRITES, (void), ())
PROF_WRAP_VOID(ProcessDma3Requests, PORT_PROF_DMA3, (void), ())
PROF_WRAP_VOID(LZ77UnCompWram, PORT_PROF_LZ, (const u32 *src, void *dest), (src, dest))
PROF_WRAP_VOID(RLUnCompWram, PORT_PROF_LZ, (const u32 *src, void *dest), (src, dest))

/* The size a compressed payload unpacks to, from its header. */
static u32 UnpackedSize(const u32 *src)
{
    const u32 *real = (const u32 *)Port_ResolveAssetPointer(src);

    return real != NULL ? real[0] >> 8 : 0;
}

#define COPY_WRAP(name, section, params, args, dest, bytes) \
    void __real_##name params;                              \
    void __wrap_##name params                               \
    {                                                       \
        PORT_PROF_BEGIN(call);                              \
        __real_##name args;                                 \
        CtrVideo_NotifyWrite((dest), (bytes));              \
        PORT_PROF_END(call, section);                       \
    }

/* CpuSet: a count of half-words, or of words with bit 26. */
COPY_WRAP(CpuSet, PORT_PROF_COPY, (const void *src, void *dest, u32 control), (src, dest, control),
          dest, (control & 0x1FFFFF) * ((control & (1u << 26)) ? 4 : 2))
/* CpuFastSet: always words. */
COPY_WRAP(CpuFastSet, PORT_PROF_COPY, (const void *src, void *dest, u32 control), (src, dest, control),
          dest, (control & 0x1FFFFF) * 4)
/* A DMA's count is 16 bits, 0 meaning 0x10000; bit 26 makes it words. */
COPY_WRAP(DmaSet, PORT_PROF_COPY, (int n, const void *src, void *dest, u32 control), (n, src, dest, control),
          dest, ((control & 0xFFFF) ? (control & 0xFFFF) : 0x10000) * ((control & (1u << 26)) ? 4 : 2))

void __real_LZ77UnCompVram(const u32 *src, void *dest);
void __wrap_LZ77UnCompVram(const u32 *src, void *dest)
{
    PORT_PROF_BEGIN(call);
    __real_LZ77UnCompVram(src, dest);
    CtrVideo_NotifyWrite(dest, UnpackedSize(src));
    PORT_PROF_END(call, PORT_PROF_LZ);
}

void __real_RLUnCompVram(const u32 *src, void *dest);
void __wrap_RLUnCompVram(const u32 *src, void *dest)
{
    PORT_PROF_BEGIN(call);
    __real_RLUnCompVram(src, dest);
    CtrVideo_NotifyWrite(dest, UnpackedSize(src));
    PORT_PROF_END(call, PORT_PROF_LZ);
}

u8 __real_UpdatePaletteFade(void);
u8 __wrap_UpdatePaletteFade(void)
{
    PORT_PROF_BEGIN(call);
    u8 result = __real_UpdatePaletteFade();
    PORT_PROF_END(call, PORT_PROF_FADE);
    return result;
}
