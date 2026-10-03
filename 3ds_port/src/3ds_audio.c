/*
 * Audio transport: the mixer's PCM frames to NDSP.
 *
 * The mixer runs on the processor that talks to the DSP, so this file is only
 * a queue: convert one mixer frame
 * to interleaved 16-bit stereo, hand it to NDSP, and keep the queue from
 * running dry or running away.
 *
 * Drift is the one thing that needs care. The mixer produces exactly one frame
 * per VBlank, so its output rate is 701 * (whatever the console's refresh
 * really is), while NDSP consumes at the rate we ask for. Those are never the
 * same number, and a dropped frame makes the gap worse. Rather than let the
 * queue drain and click every few seconds, the playback rate is nudged by
 * fractions of a percent towards the depth we want to keep. That is inaudible
 * and it never needs a resync.
 */

#include <3ds.h>
#include <string.h>

#include "3ds_audio.h"
#include "3ds_platform.h"

/* Eight frames is 133 ms of headroom; CTR_AUDIO_TARGET_DEPTH is where we sit. */
#define CTR_AUDIO_BUFFERS 8
/* 701 today. The rest is headroom in case the game changes its sound mode. */
#define CTR_AUDIO_MAX_FRAMES 1024
/* Per queued buffer away from the target, as a fraction of the sample rate. */
#define CTR_AUDIO_TRIM      0.0025f
#define CTR_AUDIO_TRIM_MAX  0.0100f

static ndspWaveBuf sWave[CTR_AUDIO_BUFFERS];
static int16_t *sSamples;
static unsigned sNext;
static unsigned sPrimed;
static float sRate;
static bool sReady;
static CtrAudioStats sStats;

bool CtrAudio_Available(void) { return sReady; }
CtrAudioStats *CtrAudio_Stats(void) { return &sStats; }

bool CtrAudio_Init(void)
{
    Result rc;

    if (sReady)
        return true;

    /* NDSP needs the DSP firmware dumped to the SD card. Without it there is
     * no way to make a sound, and reporting success would hide that. */
    rc = ndspInit();
    if (R_FAILED(rc))
    {
        CtrLog_Write(CTR_LOG_ERROR,
                     "ndspInit failed (%08lx): no DSP firmware, audio is off",
                     (unsigned long)rc);
        return false;
    }

    sSamples = linearAlloc(CTR_AUDIO_BUFFERS * CTR_AUDIO_MAX_FRAMES * 2 * sizeof(int16_t));
    if (sSamples == NULL)
    {
        CtrLog_Write(CTR_LOG_ERROR, "no linear memory for %u audio frames",
                     CTR_AUDIO_BUFFERS * CTR_AUDIO_MAX_FRAMES);
        ndspExit();
        return false;
    }
    memset(sSamples, 0, CTR_AUDIO_BUFFERS * CTR_AUDIO_MAX_FRAMES * 2 * sizeof(int16_t));

    sRate = (float)CTR_AUDIO_SAMPLE_RATE;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, sRate);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = 1.0f; /* front left  */
    mix[1] = 1.0f; /* front right */
    ndspChnSetMix(0, mix);

    memset(sWave, 0, sizeof(sWave));
    for (unsigned i = 0; i < CTR_AUDIO_BUFFERS; ++i)
        sWave[i].status = NDSP_WBUF_DONE;

    memset(&sStats, 0, sizeof(sStats));
    sStats.rateHz = sRate;
    sNext = 0;
    sPrimed = 0;
    sReady = true;
    CtrLog_Write(CTR_LOG_AUDIO, "NDSP ready: stereo %d Hz, %d frame buffers",
                 CTR_AUDIO_SAMPLE_RATE, CTR_AUDIO_BUFFERS);
    return true;
}

void CtrAudio_Shutdown(void)
{
    if (!sReady)
        return;
    sReady = false;
    ndspChnWaveBufClear(0);
    ndspExit();
    if (sSamples != NULL)
    {
        linearFree(sSamples);
        sSamples = NULL;
    }
}

static unsigned QueuedBuffers(void)
{
    unsigned queued = 0;

    for (unsigned i = 0; i < CTR_AUDIO_BUFFERS; ++i)
    {
        if (sWave[i].status == NDSP_WBUF_QUEUED || sWave[i].status == NDSP_WBUF_PLAYING)
            ++queued;
    }
    return queued;
}

/*
 * Pull the playback rate towards the queue depth we want. One buffer of error
 * is a quarter of a percent, which is about four cents of pitch: below what
 * anyone can hear, and enough to absorb both the refresh-rate mismatch and the
 * occasional frame the game spends too long drawing.
 */
static void CorrectDrift(unsigned queued)
{
    float trim = ((float)queued - (float)CTR_AUDIO_TARGET_DEPTH) * CTR_AUDIO_TRIM;
    float rate;

    if (trim > CTR_AUDIO_TRIM_MAX)
        trim = CTR_AUDIO_TRIM_MAX;
    else if (trim < -CTR_AUDIO_TRIM_MAX)
        trim = -CTR_AUDIO_TRIM_MAX;

    rate = (float)CTR_AUDIO_SAMPLE_RATE * (1.0f + trim);
    if (rate != sRate)
    {
        sRate = rate;
        ndspChnSetRate(0, rate);
    }
    sStats.rateHz = rate;
}

void CtrAudio_Queue(const float *interleaved, int frames)
{
    uint64_t start;
    unsigned queued;
    ndspWaveBuf *buf;
    int16_t *out;
    int32_t peak = 0;

    if (!sReady || interleaved == NULL || frames <= 0)
        return;
    if (frames > CTR_AUDIO_MAX_FRAMES)
    {
        static bool reported;
        if (!reported)
        {
            reported = true;
            CtrLog_Write(CTR_LOG_ERROR, "mixer frame of %d samples exceeds the %d the DSP buffers hold",
                         frames, CTR_AUDIO_MAX_FRAMES);
        }
        frames = CTR_AUDIO_MAX_FRAMES;
    }

    start = svcGetSystemTick();
    queued = QueuedBuffers();

    /* An empty queue once the pipeline is primed means the DSP reached the end
     * of the last buffer before this frame arrived: the speakers went silent. */
    if (queued == 0 && sPrimed >= CTR_AUDIO_TARGET_DEPTH)
        ++sStats.underruns;

    buf = &sWave[sNext];
    if (buf->status != NDSP_WBUF_DONE && buf->status != NDSP_WBUF_FREE)
    {
        /* Every buffer is still pending: the game is ahead of the DSP. Drop
         * this frame rather than overwrite audio that is about to play. */
        ++sStats.drops;
        sStats.queued = queued;
        CorrectDrift(queued);
        return;
    }

    out = sSamples + (size_t)sNext * CTR_AUDIO_MAX_FRAMES * 2;
    for (int i = 0; i < frames * 2; ++i)
    {
        /*
         * Adding 1.5 * 2^23 leaves the rounded integer in the low mantissa bits,
         * so the conversion is one float add and a bit copy instead of a VFP11
         * float-to-int, which stalls the pipeline. A sample is far inside +-2^22
         * (a runaway mix is clipped below, after this).
         */
        union { float f; int32_t i; } magic;
        int32_t sample;

        magic.f = interleaved[i] * 32767.0f + 12582912.0f;
        sample = magic.i - 0x4B400000;
        if (sample > 32767)
            sample = 32767;
        else if (sample < -32768)
            sample = -32768;
        out[i] = (int16_t)sample;
        if (sample < 0)
            sample = -sample;
        if (sample > peak)
            peak = sample;
    }

    DSP_FlushDataCache(out, (size_t)frames * 2 * sizeof(int16_t));

    memset(buf, 0, sizeof(*buf));
    buf->data_vaddr = out;
    buf->nsamples = (u32)frames;
    ndspChnWaveBufAdd(0, buf);

    sNext = (sNext + 1) % CTR_AUDIO_BUFFERS;
    if (sPrimed < CTR_AUDIO_TARGET_DEPTH)
        ++sPrimed;
    ++sStats.frames;
    sStats.peak = (uint32_t)peak;
    sStats.queued = queued + 1;
    CorrectDrift(queued + 1);
    sStats.submitMs = (float)((svcGetSystemTick() - start) * 1000.0 / SYSCLOCK_ARM11);
}
